#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <ETH.h>
#include <ESP32Ping.h> 
#include "secrets.h"

// ====================================================================
// CONFIGURACIÓN PARAMETRIZABLE
// ====================================================================
const bool USE_ETHERNET       = false;  
const bool USE_DHCP           = true;   
const bool ROTATE_SCREEN      = false;  

const uint8_t MODBUS_FIXED_ID = 2;      // Actualizado por si lo pruebas a mano
const uint16_t MODBUS_TEST_REG = 30070; // Registro infalible: Model ID (1 byte)
const uint32_t RECONNECT_DELAY = 5000;  
// ====================================================================

IPAddress local_IP(192, 168, 254, 211);
IPAddress gateway(192, 168, 254, 252);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns_primary(8, 8, 8, 8);
IPAddress targetModbusIP;

const int BOTON_OK    = 4;
const int BOTON_BACK  = 14;
const int BOTON_MAS   = 15;
const int BOTON_MENOS = 34; 

#define DIRECCION_I2C 0x3C
#define ANCHO_PANTALLA 128
#define ALTO_PANTALLA 64
Adafruit_SH1106G display(ANCHO_PANTALLA, ALTO_PANTALLA, &Wire, -1);

WiFiServer proxyServer(502);
const int MAX_CLIENTS = 4;
WiFiClient clients[MAX_CLIENTS];
WiFiClient backendClient;
SemaphoreHandle_t backendMutex;
uint32_t lastBackendConnectAttempt = 0; 

struct ClientStats {
    IPAddress ip;
    uint32_t requestCount = 0;
    uint32_t lastRequestTimestamp = 0;
    bool isUsed = false;
};
const int MAX_TRACKED_IPS = 10;
ClientStats trackedClients[MAX_TRACKED_IPS];

enum MenuState {
    MENU_IDLE, MENU_MAIN, MENU_CLIENTS_LIST, MENU_CLIENTS_DETAIL,
    MENU_TEST_PING, MENU_TEST_FIXED, MENU_TEST_SCAN        
};
MenuState currentMenuState = MENU_IDLE; 
int currentMenuOption = 0; 
int selectedClientIdx = 0;
String diagnosticResult = "Pulse OK para Test";

bool okPressed = false; bool backPressed = false;
bool masPressed = false; bool menosPressed = false;
bool lastOkState = HIGH; bool lastBackState = HIGH;
bool lastMasState = HIGH; bool lastMenosState = HIGH;

bool readExact(WiFiClient &client, uint8_t* buffer, size_t length, uint32_t timeoutMs = 1000);
void updateClientStats(IPAddress ip);
void taskModbusProxy(void *parameter);
void checkButtons();
void handleNavigation();
void renderUI();
void ejecutarTestPing();
void ejecutarTestModbusFijo();
void ejecutarEscanerModbus();
int getActiveClientCount();

void setup() {
    Serial.begin(115200);
    targetModbusIP.fromString(MODBUS_SERVER_IP);
    pinMode(BOTON_OK, INPUT_PULLUP); pinMode(BOTON_BACK, INPUT_PULLUP);
    pinMode(BOTON_MAS, INPUT_PULLUP); pinMode(BOTON_MENOS, INPUT); 

    Wire.begin(33, 32);
    if (!display.begin(DIRECCION_I2C, true)) for (;;);
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 10);
    display.println("INICIANDO PROXY...");
    display.println("Modbus TCP v1.7 (HA)"); 
    display.display();

    if (USE_ETHERNET) {
        ETH.begin(1, 16, 23, 18, ETH_PHY_LAN8720, ETH_CLOCK_GPIO0_IN);
        if (!USE_DHCP) ETH.config(local_IP, gateway, subnet, dns_primary);
    } else {
        if (!USE_DHCP) WiFi.config(local_IP, gateway, subnet, dns_primary);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    backendMutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(taskModbusProxy, "TaskModbusProxy", 8192, NULL, 1, NULL, 0);
}

void loop() {
    checkButtons(); handleNavigation(); renderUI(); delay(20); 
}

bool readExact(WiFiClient &client, uint8_t* buffer, size_t length, uint32_t timeoutMs) {
    size_t bytesRead = 0; uint32_t startMs = millis();
    while (bytesRead < length) {
        if (!client.connected()) return false;
        if (client.available()) {
            int r = client.read(buffer + bytesRead, length - bytesRead);
            if (r > 0) bytesRead += r;
            else if (r < 0) return false; 
        }
        if (millis() - startMs > timeoutMs) return false; 
        delay(1);
    }
    return true;
}

void updateClientStats(IPAddress ip) {
    for (int i = 0; i < MAX_TRACKED_IPS; i++) {
        if (trackedClients[i].isUsed && trackedClients[i].ip == ip) {
            trackedClients[i].requestCount++; trackedClients[i].lastRequestTimestamp = millis() / 1000; return;
        }
    }
    for (int i = 0; i < MAX_TRACKED_IPS; i++) {
        if (!trackedClients[i].isUsed) {
            trackedClients[i].ip = ip; trackedClients[i].requestCount = 1;
            trackedClients[i].lastRequestTimestamp = millis() / 1000; trackedClients[i].isUsed = true; return;
        }
    }
}

int getActiveClientCount() {
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) if (clients[i] && clients[i].connected()) count++;
    return count;
}

void taskModbusProxy(void *parameter) {
    proxyServer.begin();
    while (true) {
        if (proxyServer.hasClient()) {
            WiFiClient newClient = proxyServer.available();
            bool slotFound = false;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (!clients[i] || !clients[i].connected()) { clients[i] = newClient; slotFound = true; break; }
            }
            if (!slotFound) newClient.stop();
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] && clients[i].connected()) {
                if (clients[i].available() >= 7) { 
                    uint8_t mbap[7];
                    if (readExact(clients[i], mbap, 7)) {
                        uint16_t remainingLength = (mbap[4] << 8) | mbap[5];
                        if (remainingLength > 0 && remainingLength < 260) {
                            uint16_t pduLen = remainingLength - 1; 
                            uint8_t* pdu = new uint8_t[pduLen];
                            
                            if (readExact(clients[i], pdu, pduLen)) {
                                updateClientStats(clients[i].remoteIP());
                                
                                if (xSemaphoreTake(backendMutex, pdMS_TO_TICKS(2500)) == pdTRUE) {
                                    if (!backendClient.connected()) {
                                        if (millis() - lastBackendConnectAttempt >= RECONNECT_DELAY || lastBackendConnectAttempt == 0) {
                                            lastBackendConnectAttempt = millis();
                                            backendClient.connect(targetModbusIP, MODBUS_SERVER_PORT);
                                        }
                                    }

                                    if (backendClient.connected()) {
                                        backendClient.write(mbap, 7);
                                        backendClient.write(pdu, pduLen);
                                        
                                        uint8_t resMbap[7];
                                        if (readExact(backendClient, resMbap, 7, 2000)) {
                                            uint16_t resRemainingLength = (resMbap[4] << 8) | resMbap[5];
                                            if (resRemainingLength > 0 && resRemainingLength < 260) {
                                                uint16_t resPduLen = resRemainingLength - 1; 
                                                uint8_t* resPdu = new uint8_t[resPduLen];
                                                if (readExact(backendClient, resPdu, resPduLen, 1500)) {
                                                    clients[i].write(resMbap, 7);
                                                    clients[i].write(resPdu, resPduLen);
                                                }
                                                delete[] resPdu;
                                            }
                                        } else { backendClient.stop(); }
                                    }
                                    xSemaphoreGive(backendMutex); 
                                }
                            }
                            delete[] pdu;
                        }
                    }
                }
            }
        }
        delay(2); 
    }
}

void checkButtons() {
    bool currentOk = digitalRead(BOTON_OK); bool currentBack = digitalRead(BOTON_BACK);
    bool currentMas = digitalRead(BOTON_MAS); bool currentMenos = digitalRead(BOTON_MENOS);

    okPressed = (currentOk == LOW && lastOkState == HIGH); backPressed = (currentBack == LOW && lastBackState == HIGH);
    masPressed = (currentMas == LOW && lastMasState == HIGH); menosPressed = (currentMenos == LOW && lastMenosState == HIGH);

    lastOkState = currentOk; lastBackState = currentBack;
    lastMasState = currentMas; lastMenosState = currentMenos;
    if (okPressed || backPressed || masPressed || menosPressed) delay(40); 
}

void handleNavigation() {
    int totalTracked = 0;
    for (int i = 0; i < MAX_TRACKED_IPS; i++) if (trackedClients[i].isUsed) totalTracked++;
    switch (currentMenuState) {
        case MENU_IDLE:
            if (okPressed) { currentMenuState = MENU_MAIN; currentMenuOption = 0; } break;
        case MENU_MAIN:
            if (masPressed) currentMenuOption = (currentMenuOption + 1) % 4;
            if (menosPressed) currentMenuOption = (currentMenuOption - 1 + 4) % 4;
            if (backPressed) currentMenuState = MENU_IDLE;
            if (okPressed) {
                if (currentMenuOption == 0) { currentMenuState = MENU_CLIENTS_LIST; selectedClientIdx = 0; } 
                else if (currentMenuOption == 1) { currentMenuState = MENU_TEST_PING; diagnosticResult = "OK:Iniciar Test"; } 
                else if (currentMenuOption == 2) { currentMenuState = MENU_TEST_FIXED; diagnosticResult = "OK p/ ID " + String(MODBUS_FIXED_ID); } 
                else if (currentMenuOption == 3) { currentMenuState = MENU_TEST_SCAN; diagnosticResult = "OK:Iniciar Scan"; }
            } break;
        case MENU_CLIENTS_LIST:
            if (totalTracked > 0) {
                if (masPressed) selectedClientIdx = (selectedClientIdx + 1) % totalTracked;
                if (menosPressed) selectedClientIdx = (selectedClientIdx - 1 + totalTracked) % totalTracked;
            }
            if (backPressed) currentMenuState = MENU_MAIN;
            if (okPressed && totalTracked > 0) currentMenuState = MENU_CLIENTS_DETAIL; break;
        case MENU_CLIENTS_DETAIL:
            if (backPressed || okPressed) currentMenuState = MENU_CLIENTS_LIST; break;
        case MENU_TEST_PING:
            if (backPressed) currentMenuState = MENU_MAIN; if (okPressed) ejecutarTestPing(); break;
        case MENU_TEST_FIXED:
            if (backPressed) currentMenuState = MENU_MAIN; if (okPressed) ejecutarTestModbusFijo(); break;
        case MENU_TEST_SCAN:
            if (backPressed) currentMenuState = MENU_MAIN; if (okPressed) ejecutarEscanerModbus(); break;
    }
}

void renderUI() {
    display.clearDisplay();
    String ipStr = "Conectando...";
    if (USE_ETHERNET) {
        if (ETH.localIP() != IPAddress(0,0,0,0)) ipStr = ETH.localIP().toString();
    } else {
        if (WiFi.status() == WL_CONNECTED) ipStr = WiFi.localIP().toString();
        else if (WiFi.status() == WL_NO_SSID_AVAIL) ipStr = "Sin SSID WiFi";
        else ipStr = "Desconectado";
    }
    bool backendCon = backendClient.connected(); int activeSockets = getActiveClientCount();

    switch (currentMenuState) {
        case MENU_IDLE:
            display.setTextSize(1); display.setCursor(0, 0); display.println("=== PROXY MODBUS ===");
            display.setCursor(0, 18); display.print("IP: "); display.println(ipStr);
            display.setCursor(0, 32); display.print("Server: "); display.println(backendCon ? "CONECTADO" : "OFFLINE");
            display.setCursor(0, 46); display.print("Clientes: "); display.println(activeSockets);
            display.setCursor(0, 56); display.print("[ OK:Menu ]"); break;
        case MENU_MAIN:
            display.setCursor(0, 0); display.println("--- MENU PRINCIPAL ---");
            display.setCursor(2, 14); display.print(currentMenuOption == 0 ? "> " : "  "); display.println("1. Historial IPs");
            display.setCursor(2, 24); display.print(currentMenuOption == 1 ? "> " : "  "); display.println("2. Test Ping Red");
            display.setCursor(2, 34); display.print(currentMenuOption == 2 ? "> " : "  "); display.println("3. Modbus Fijo");
            display.setCursor(2, 44); display.print(currentMenuOption == 3 ? "> " : "  "); display.println("4. Escaner Auto-HA");
            display.setCursor(0, 56); display.print("+-:Mover OK:Entrar"); break;
        case MENU_CLIENTS_LIST: {
            display.println("--- HISTORIAL IPS ---");
            int totalTracked = 0; int mappings[MAX_TRACKED_IPS];
            for (int i = 0; i < MAX_TRACKED_IPS; i++) if (trackedClients[i].isUsed) mappings[totalTracked++] = i;
            if (totalTracked == 0) { display.setCursor(0, 28); display.println(" Sin IPs registradas"); display.setCursor(0, 56); display.print("BACK:Atras"); } 
            else { display.setCursor(0, 26); display.print(" -> "); display.println(trackedClients[mappings[selectedClientIdx]].ip.toString()); display.setCursor(0, 56); display.print("OK:Ver " + String(selectedClientIdx+1) + "/" + String(totalTracked)); }
            break; }
        case MENU_CLIENTS_DETAIL: {
            display.println("--- METRICAS IP ---");
            int totalTracked = 0; int mappings[MAX_TRACKED_IPS];
            for (int i = 0; i < MAX_TRACKED_IPS; i++) if (trackedClients[i].isUsed) mappings[totalTracked++] = i;
            if (totalTracked > 0 && selectedClientIdx < totalTracked) {
                ClientStats sc = trackedClients[mappings[selectedClientIdx]];
                display.setCursor(0, 16); display.print("IP: "); display.println(sc.ip.toString());
                display.setCursor(0, 30); display.print("Peticiones: "); display.println(sc.requestCount);
                display.setCursor(0, 44); display.print("Ult. segs: "); display.println(sc.lastRequestTimestamp);
            }
            display.setCursor(0, 56); display.print("BACK:Atras"); break; }
        case MENU_TEST_PING:
            display.println("--- TEST PING ICMP ---"); display.setCursor(0, 26); display.println(diagnosticResult); display.setCursor(0, 56); display.print("OK:Test BACK:Salir"); break;
        case MENU_TEST_FIXED:
            display.println("--- MODBUS FIJO ---"); display.setCursor(0, 20); display.println(diagnosticResult); display.setCursor(0, 56); display.print("OK:Test BACK:Salir"); break;
        case MENU_TEST_SCAN:
            display.println("--- ESCANER AUTO-HA ---"); display.setCursor(0, 26); display.println(diagnosticResult); display.setCursor(0, 56); display.print("OK:Scan BACK:Salir"); break;
    }
    display.display();
}

void ejecutarTestPing() {
    diagnosticResult = "Ping en curso..."; renderUI(); 
    bool exito = Ping.ping(targetModbusIP, 3);
    if (exito) { diagnosticResult = "PING OK! Red Viva.\nTiempo: " + String(Ping.averageTime()) + " ms"; } 
    else { diagnosticResult = "PING FALLIDO!\nHost Inalcanzable"; }
}

void ejecutarTestModbusFijo() {
    diagnosticResult = "Lanzando ID " + String(MODBUS_FIXED_ID) + "..."; renderUI();
    if (xSemaphoreTake(backendMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
        if (backendClient.connected()) backendClient.stop();
        if (backendClient.connect(targetModbusIP, MODBUS_SERVER_PORT)) {
            uint8_t reqFrame[] = { 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, MODBUS_FIXED_ID, 0x03, (uint8_t)(MODBUS_TEST_REG >> 8), (uint8_t)(MODBUS_TEST_REG & 0xFF), 0x00, 0x01 };
            backendClient.write(reqFrame, 12);
            uint8_t resMbap[7];
            if (readExact(backendClient, resMbap, 7, 2500)) {
                uint16_t rLen = (resMbap[4] << 8) | resMbap[5];
                if (rLen >= 2 && rLen < 260) {
                    uint16_t pduLen = rLen - 1; 
                    uint8_t* resPdu = new uint8_t[pduLen];
                    if (readExact(backendClient, resPdu, pduLen, 1500)) {
                        if (resPdu[0] == 0x03) { 
                            uint16_t valorReg = (resPdu[2] << 8) | resPdu[3];
                            diagnosticResult = "MODBUS OK!\nReg" + String(MODBUS_TEST_REG) + "=" + String(valorReg);
                        } else if (resPdu[0] == 0x83) {
                            diagnosticResult = "Err: 83 Ex: 0" + String(resPdu[1], HEX) + "\nReg o ID invalido";
                        } else {
                            diagnosticResult = "Err Modbus: 0x" + String(resPdu[0], HEX);
                        }
                    } else { diagnosticResult = "Error Lectura PDU"; }
                    delete[] resPdu;
                } else { diagnosticResult = "MBAP Invalido"; }
            } else { diagnosticResult = "Timeout sin respuesta"; }
            backendClient.stop();
        } else { diagnosticResult = "Fallo TCP IP:502"; }
        xSemaphoreGive(backendMutex);
    } else { diagnosticResult = "Canal Bus Bloqueado"; }
}

// TEST 4: Escáner Autodescubrimiento (Copia la lógica de Home Assistant)
void ejecutarEscanerModbus() {
    // Escaneamos la misma lista que usa la librería oficial huawei_solar
    uint8_t idsAProbar[] = {1, 2, 3, 0, 16, 100, 255}; 
    bool idEncontrado = false; 
    uint8_t idExitoso = 0; 
    uint16_t valorDetectado = 0;

    diagnosticResult = "Buscando Inversor..."; 
    renderUI();

    if (xSemaphoreTake(backendMutex, pdMS_TO_TICKS(15000)) == pdTRUE) {
        
        for (int i = 0; i < 7; i++) {
            uint8_t idActual = idsAProbar[i];
            diagnosticResult = "Probando ID: " + String(idActual) + "..."; 
            renderUI();

            if (backendClient.connected()) backendClient.stop();
            
            if (backendClient.connect(targetModbusIP, MODBUS_SERVER_PORT)) {
                // Inyectamos el registro 30070 (0x75 0x76), el que no falla nunca
                uint8_t reqFrame[] = { 
                    0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 
                    idActual, 0x03, 0x75, 0x76, 0x00, 0x01 
                };
                
                backendClient.write(reqFrame, 12);
                
                uint8_t resMbap[7];
                if (readExact(backendClient, resMbap, 7, 1000)) {
                    uint16_t rLen = (resMbap[4] << 8) | resMbap[5];
                    if (rLen >= 2 && rLen < 30) {
                        uint16_t pduLen = rLen - 1; 
                        uint8_t* resPdu = new uint8_t[pduLen];
                        
                        if (readExact(backendClient, resPdu, pduLen, 1000)) {
                            // Si responde con 0x03, ignoramos las Excepciones y cantamos victoria
                            if (resPdu[0] == 0x03) { 
                                valorDetectado = (resPdu[2] << 8) | resPdu[3];
                                idExitoso = idActual;
                                idEncontrado = true;
                            }
                        }
                        delete[] resPdu;
                    }
                }
                backendClient.stop();
            }
            if (idEncontrado) break; 
            delay(200); 
        }
        xSemaphoreGive(backendMutex);

        if (idEncontrado) { 
            diagnosticResult = "¡BINGO! ID Caza: " + String(idExitoso) + "\nReg30070=" + String(valorDetectado); 
        } else { 
            diagnosticResult = "Escaneo Fallido.\nNingun ID responde."; 
        }
    } else { 
        diagnosticResult = "Canal Bus Bloqueado"; 
    }
}
// === FIN ===