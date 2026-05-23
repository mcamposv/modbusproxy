#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <ETH.h>
#include <ESP32Ping.h> 
#include <WebServer.h> 
#include "secrets.h"

// ====================================================================
// CONFIGURACIÓN PARAMETRIZABLE
// ====================================================================
const bool USE_ETHERNET       = false;  
const bool USE_DHCP           = true;   
const bool ROTATE_SCREEN      = false;  

const uint8_t MODBUS_FIXED_ID = 0;      // ID 0 (La EMMA)
const uint16_t MODBUS_TEST_REG = 30000; // Registro 30000 (Model Name ASCII)
const uint32_t RECONNECT_DELAY = 5000;  
// ====================================================================

// Máquina de estados extendida
enum BackendState { BK_STARTUP_PING, BK_PING_ERR, BK_WAITING, BK_STANDBY, BK_CONNECTED, BK_CON_ERR, BK_PAUSED };
BackendState currentBackendState = BK_STARTUP_PING;

// Variables de Bloqueo y Ping
bool pingSuccess = false;
bool isPaused = false;
uint32_t lastPingTime = 0;
const uint32_t pingRetryInterval = 30000; 
int pingCountdown = 0;

// Variables para la consulta de primado
String emmaDeviceModel = "BUSCANDO...";
bool checkEmmaStartup = true;
uint32_t lastEmmaCheckTime = 0;

// Variables globales de depuración Web
String emmaDebugStage = "Inactivo";
String emmaDebugHexSent = "-";
String emmaDebugHexReceived = "-";
String emmaDebugError = "Esperando red e inicio de test...";
uint32_t emmaDebugAttempts = 0;

// Configuración de red local estática de rescate
IPAddress local_IP(192, 168, 254, 211);
IPAddress gateway(192, 168, 254, 252);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns_primary(8, 8, 8, 8);
IPAddress targetModbusIP;

const int BOTON_OK    = 4;
const int BOTON_BACK  = 14;
const int BOTON_MAS   = 15;
const int BOTON_MENOS = 39; 

#define DIRECCION_I2C 0x3C
#define ANCHO_PANTALLA 128
#define ALTO_PANTALLA 64
Adafruit_SH1106G display(ANCHO_PANTALLA, ALTO_PANTALLA, &Wire, -1);

WiFiServer proxyServer(502);
WebServer webServer(80); 

const int MAX_CLIENTS = 4;
WiFiClient clients[MAX_CLIENTS];
WiFiClient backendClient;
SemaphoreHandle_t backendMutex;
uint32_t lastBackendConnectAttempt = 0; 
bool pendingShutdown = false; 

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
    MENU_TEST_PING, MENU_TEST_FIXED, MENU_TEST_SCAN, MENU_SHUTDOWN        
};
MenuState currentMenuState = MENU_IDLE; 
int currentMenuOption = 0; 
int selectedClientIdx = 0;
String diagnosticResult = "Pulse OK para Test";

bool okPressed = false; bool backPressed = false;
bool masPressed = false; bool menosPressed = false;
bool lastOkState = HIGH; bool lastBackState = HIGH;
bool lastMasState = HIGH; bool lastMenosState = HIGH;

bool readExact(WiFiClient &client, uint8_t* buffer, size_t length, uint32_t timeoutMs = 500);
void updateClientStats(IPAddress ip);
void taskModbusProxy(void *parameter);
void checkButtons();
void handleNavigation();
void renderUI();
void ejecutarTestPing();
void ejecutarTestModbusFijo();
void ejecutarEscanerModbus();
void ejecutarApagado();
int getActiveClientCount();
void handleWebRoot();
void handleWebShutdown();

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
    display.println("Modbus TCP v3.1 (HA)"); 
    display.display();

    if (USE_ETHERNET) {
        ETH.begin(1, 16, 23, 18, ETH_PHY_LAN8720, ETH_CLOCK_GPIO0_IN);
        if (!USE_DHCP) ETH.config(local_IP, gateway, subnet, dns_primary);
    } else {
        if (!USE_DHCP) WiFi.config(local_IP, gateway, subnet, dns_primary);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    
    webServer.on("/", handleWebRoot);
    webServer.on("/apagar", handleWebShutdown);
    webServer.begin();

    backendMutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(taskModbusProxy, "TaskModbusProxy", 8192, NULL, 1, NULL, 0);
}

void loop() {
    checkButtons(); 
    handleNavigation(); 
    renderUI(); 
    
    webServer.handleClient(); 
    
    if (pendingShutdown) {
        delay(500); 
        ejecutarApagado();
    }
    
    delay(20); 
}

// ====================================================================
// MOTOR DEL SERVIDOR WEB HTTP
// ====================================================================
void handleWebRoot() {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<meta http-equiv='refresh' content='5'>"; 
    html += "<title>Monitor Proxy Modbus</title>";
    html += "<style>";
    html += "body { background-color: #121212; color: #e0e0e0; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; }";
    html += ".container { max-width: 800px; margin: 0 auto; background-color: #1e1e1e; padding: 20px; border-radius: 10px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }";
    html += "h1 { color: #4da6ff; border-bottom: 1px solid #333; padding-bottom: 10px; }";
    html += "table { width: 100%; border-collapse: collapse; margin-top: 20px; margin-bottom: 30px; }";
    html += "th, td { border: 1px solid #333; padding: 12px; text-align: center; }";
    html += "th { background-color: #2d2d2d; color: #4da6ff; }";
    html += "tr:nth-child(even) { background-color: #1a1a1a; }";
    html += ".status { font-weight: bold; padding: 5px 10px; border-radius: 5px; }";
    html += ".btn-danger { display: inline-block; background-color: #dc3545; color: white; padding: 10px 20px; text-decoration: none; border-radius: 5px; font-weight: bold; border: none; cursor: pointer; }";
    html += ".btn-danger:hover { background-color: #c82333; }";
    html += "code { background-color: #111; padding: 4px 8px; border-radius: 3px; font-family: monospace; font-size: 14px; display: inline-block; word-break: break-all; }";
    html += "</style></head><body>";
    
    html += "<div class='container'>";
    html += "<h1>📊 Monitor Proxy Modbus (HA)</h1>";

    String stateColor = "#888";
    String stateStr = "WAITING";
    if (currentBackendState == BK_STANDBY) { stateStr = "STANDBY"; stateColor = "#f39c12"; }
    else if (currentBackendState == BK_CONNECTED) { stateStr = "CONNECTED"; stateColor = "#28a745"; }
    else if (currentBackendState == BK_CON_ERR) { stateStr = "CON-ERR"; stateColor = "#dc3545"; }
    else if (currentBackendState == BK_STARTUP_PING) { stateStr = "PINGING..."; stateColor = "#17a2b8"; }
    else if (currentBackendState == BK_PING_ERR) { stateStr = "PING ERROR"; stateColor = "#dc3545"; }
    else if (currentBackendState == BK_PAUSED) { stateStr = "PAUSED"; stateColor = "#6c757d"; }

    int activeSockets = getActiveClientCount();
    int totalTracked = 0;
    for (int i = 0; i < MAX_TRACKED_IPS; i++) if (trackedClients[i].isUsed) totalTracked++;

    html += "<h3>Estado del Servidor</h3>";
    html += "<p>Túnel hacia EMMA: <span class='status' style='background-color: " + stateColor + "; color: #fff;'>" + stateStr + "</span></p>";
    
    bool ocultarModelWeb = (currentBackendState == BK_PAUSED || currentBackendState == BK_PING_ERR || currentBackendState == BK_STARTUP_PING);
    String displayModelWeb = ocultarModelWeb ? "" : emmaDeviceModel;
    html += "<p>Dispositivo Identificado: <strong>" + displayModelWeb + "</strong></p>"; 
    html += "<p>Conexiones TCP Activas: <strong>" + String(activeSockets) + " / " + String(MAX_CLIENTS) + "</strong></p>";

    if (checkEmmaStartup) {
        html += "<h3>🔍 Depuración del Primado Inicial (Modbus ID 0)</h3>";
        html += "<div style='background-color: #252525; padding: 15px; border-radius: 5px; border-left: 5px solid #f39c12; margin-bottom: 25px;'>";
        html += "<p>Total de Intentos: <strong>" + String(emmaDebugAttempts) + "</strong></p>";
        html += "<p>Fase de Control: <strong>" + emmaDebugStage + "</strong></p>";
        html += "<p>Trama Enviada (Hex): <code style='color: #4da6ff;'>" + emmaDebugHexSent + "</code></p>";
        html += "<p>Trama Recibida (Hex): <code style='color: #28a745;'>" + emmaDebugHexReceived + "</code></p>";
        html += "<p>Último Diagnóstico: <strong style='color: #ff4d4d;'>" + emmaDebugError + "</strong></p>";
        html += "</div>";
    }

    html += "<h3>Historial de IPs Clientes</h3>";
    html += "<table><tr><th>Dirección IP</th><th>Total Peticiones</th><th>Última Petición</th></tr>";
    
    uint32_t currentUptime = millis() / 1000;
    if (totalTracked == 0) {
        html += "<tr><td colspan='3'>No hay clientes registrados todavía.</td></tr>";
    } else {
        for (int i = 0; i < MAX_TRACKED_IPS; i++) {
            if (trackedClients[i].isUsed) {
                uint32_t elapsed = currentUptime - trackedClients[i].lastRequestTimestamp;
                html += "<tr>";
                html += "<td>" + trackedClients[i].ip.toString() + "</td>";
                html += "<td>" + String(trackedClients[i].requestCount) + "</td>";
                html += "<td>Hace " + String(elapsed) + " segundos</td>";
                html += "</tr>";
            }
        }
    }
    html += "</table>";
    
    html += "<a href='/apagar' class='btn-danger'>🛑 Apagar Proxy de Forma Segura</a>";
    html += "</div></body></html>";
    
    webServer.send(200, "text/html", html);
}

void handleWebShutdown() {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Apagando...</title>";
    html += "<style>body { background-color: #121212; color: #fff; font-family: sans-serif; text-align: center; padding-top: 20%; }</style>";
    html += "</head><body>";
    html += "<h1 style='color: #dc3545;'>🛑 APAGADO INICIADO</h1>";
    html += "<p>El puerto TCP ha sido liberado en la EMMA.</p>";
    html += "<p>Puedes desconectar la alimentación del dispositivo con seguridad.</p>";
    html += "</body></html>";
    
    webServer.send(200, "text/html", html);
    pendingShutdown = true; 
}
// ====================================================================

bool readExact(WiFiClient &client, uint8_t* buffer, size_t length, uint32_t timeoutMs) {
    size_t bytesRead = 0; 
    uint32_t startMs = millis();
    
    while (bytesRead < length) {
        if (!client.connected()) return false;
        
        if (client.available()) {
            int r = client.read(buffer + bytesRead, length - bytesRead);
            if (r > 0) {
                bytesRead += r;
                startMs = millis(); 
            } else if (r < 0) {
                return false; 
            }
        }
        if (millis() - startMs > timeoutMs) {
            return false; 
        }
        delay(1); 
    }
    return true;
}

void updateClientStats(IPAddress ip) {
    for (int i = 0; i < MAX_TRACKED_IPS; i++) {
        if (trackedClients[i].isUsed && trackedClients[i].ip == ip) {
            trackedClients[i].requestCount++; 
            trackedClients[i].lastRequestTimestamp = millis() / 1000; 
            return;
        }
    }
    for (int i = 0; i < MAX_TRACKED_IPS; i++) {
        if (!trackedClients[i].isUsed) {
            trackedClients[i].ip = ip; 
            trackedClients[i].requestCount = 1;
            trackedClients[i].lastRequestTimestamp = millis() / 1000; 
            trackedClients[i].isUsed = true; 
            return;
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
        if (isPaused) {
            if (backendClient.connected()) backendClient.stop();
            if (proxyServer.hasClient()) proxyServer.available().stop();
            currentBackendState = BK_PAUSED;
            delay(100);
            continue; 
        }

        bool tieneNetActiva = false;
        if (USE_ETHERNET) {
            if (ETH.localIP() != IPAddress(0,0,0,0)) tieneNetActiva = true;
        } else {
            if (WiFi.status() == WL_CONNECTED) tieneNetActiva = true;
        }

        // --- PUERTA DE PEAJE 1: PING DE INICIO ---
        if (!pingSuccess && tieneNetActiva) {
            if (millis() - lastPingTime >= pingRetryInterval || lastPingTime == 0) {
                currentBackendState = BK_STARTUP_PING;
                pingCountdown = 0; 
                bool exito = Ping.ping(targetModbusIP, 2); 
                
                if (exito) {
                    pingSuccess = true;
                    currentBackendState = BK_WAITING;
                } else {
                    lastPingTime = millis();
                    currentBackendState = BK_PING_ERR;
                }
            } else {
                pingCountdown = (pingRetryInterval - (millis() - lastPingTime)) / 1000;
                currentBackendState = BK_PING_ERR;
            }

            if (!pingSuccess) {
                if (proxyServer.hasClient()) proxyServer.available().stop();
                delay(100);
                continue;
            }
        }

        // --- PUERTA DE PEAJE 2: COMPROBACIÓN ATÓMICA L4/L7 ---
        if (checkEmmaStartup && tieneNetActiva) {
            if (millis() - lastEmmaCheckTime >= 10000 || lastEmmaCheckTime == 0) {
                lastEmmaCheckTime = millis();
                
                emmaDebugAttempts++;
                emmaDebugStage = "Abriendo Socket TCP hacia " + targetModbusIP.toString() + ":502...";
                emmaDebugHexSent = "-";
                emmaDebugHexReceived = "-";
                emmaDebugError = "En curso...";

                if (xSemaphoreTake(backendMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
                    if (backendClient.connected()) backendClient.stop(); 
                    
                    if (backendClient.connect(targetModbusIP, MODBUS_SERVER_PORT)) {
                        emmaDebugStage = "Conectado TCP con éxito. Inyectando Query Modbus...";
                        
                        uint8_t reqFrame[] = { 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, MODBUS_FIXED_ID, 0x03, 0x75, 0x30, 0x00, 0x0F };
                        emmaDebugHexSent = "00 01 00 00 00 06 00 03 75 30 00 0F";
                        backendClient.write(reqFrame, 12);
                        
                        emmaDebugStage = "Trama enviada. Esperando cabecera MBAP (7 bytes) de la EMMA...";
                        uint8_t resMbap[7];
                        if (readExact(backendClient, resMbap, 7, 500)) { 
                            emmaDebugHexReceived = "";
                            for(int h=0; h<7; h++) {
                                if(resMbap[h] < 0x10) emmaDebugHexReceived += "0";
                                emmaDebugHexReceived += String(resMbap[h], HEX) + " ";
                            }
                            emmaDebugHexReceived.toUpperCase();

                            uint16_t rLen = (resMbap[4] << 8) | resMbap[5];
                            if (rLen >= 3 && rLen < 100) {
                                uint16_t pduLen = rLen - 1;
                                uint8_t* resPdu = new uint8_t[pduLen];
                                
                                emmaDebugStage = "MBAP correcto. Leyendo cuerpo de datos PDU (" + String(pduLen) + " bytes)...";
                                if (readExact(backendClient, resPdu, pduLen, 1000)) {
                                    for(int h=0; h<pduLen; h++) {
                                        if(resPdu[h] < 0x10) emmaDebugHexReceived += "0";
                                        emmaDebugHexReceived += String(resPdu[h], HEX) + " ";
                                    }
                                    emmaDebugHexReceived.toUpperCase();

                                    if (resPdu[0] == 0x03 || resPdu[0] == 0x04) {
                                        uint8_t byteCount = resPdu[1];
                                        if (byteCount > 0 && byteCount <= pduLen - 2) {
                                            char* modelStr = new char[byteCount + 1];
                                            for (int m = 0; m < byteCount; m++) {
                                                char c = (char)resPdu[2 + m];
                                                if (c >= 32 && c <= 126) modelStr[m] = c; 
                                                else modelStr[m] = ' ';
                                            }
                                            modelStr[byteCount] = '\0';
                                            
                                            String cleanStr = String(modelStr);
                                            cleanStr.trim(); 
                                            
                                            if (cleanStr.length() > 0) {
                                                emmaDeviceModel = cleanStr;  
                                            } else {
                                                emmaDeviceModel = "SmartHEMS";
                                            }
                                            delete[] modelStr;
                                        }
                                        checkEmmaStartup = false;    
                                        currentBackendState = BK_WAITING; 
                                        emmaDebugError = "Ninguno (¡Éxito absoluto!)";
                                        emmaDebugStage = "Completado. Pasarela liberada para Home Assistant.";
                                        
                                    } else if (resPdu[0] == 0x83 || resPdu[0] == 0x84) {
                                        emmaDeviceModel = "SmartHEMS (Forzado por Excepcion)";
                                        checkEmmaStartup = false; 
                                        currentBackendState = BK_WAITING;
                                        emmaDebugError = "Ninguno (Superado por respuesta de Excepción Modbus legítima)";
                                        emmaDebugStage = "Completado. Pasarela liberada por respuesta de error viga.";
                                    } else {
                                        String codeHex = String(resPdu[0], HEX);
                                        codeHex.toUpperCase();
                                        emmaDebugError = "Error: Código de función inesperado devuelto por EMMA (0x" + codeHex + ")";
                                    }
                                } else {
                                    emmaDebugError = "Error: Timeout esperando el cuerpo PDU de la EMMA (Puerto abierto pero no completó los bytes)";
                                }
                                delete[] resPdu;
                            } else {
                                emmaDebugError = "Error: Cabecera MBAP corrupta, longitud declarada errónea (" + String(rLen) + " bytes)";
                            }
                        } else {
                            emmaDebugError = "Error: Timeout esperando MBAP (La EMMA aceptó la conexión TCP pero se quedó muda, no mandó bytes)";
                        }
                        backendClient.stop(); 
                    } else {
                        currentBackendState = BK_CON_ERR; 
                        emmaDebugError = "Error: Conexión TCP Rechazada/Timeout (La EMMA sigue con el puerto 502 congelado o su firewall nos está tirando el socket)";
                        emmaDebugStage = "Fallo de conexión en Capa 4 (TCP)";
                    }
                    xSemaphoreGive(backendMutex);
                } else {
                    emmaDebugError = "Error: Semáforo del sistema ocupado.";
                }
            }

            if (checkEmmaStartup) {
                if (proxyServer.hasClient()) proxyServer.available().stop();
                delay(100);
                continue; 
            }
        }

        // 1. Aceptar nuevos clientes entrantes (Home Assistant)
        if (proxyServer.hasClient()) {
            WiFiClient newClient = proxyServer.available();
            bool slotFound = false;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (!clients[i] || !clients[i].connected()) { 
                    clients[i] = newClient; 
                    updateClientStats(newClient.remoteIP()); 
                    slotFound = true; 
                    break; 
                }
            }
            if (!slotFound) newClient.stop();
        }

        // 2. Enrutador Modbus Robusto y Reensamblador
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] && clients[i].connected()) {
                if (clients[i].available() >= 7) { 
                    uint8_t mbap[7];
                    if (readExact(clients[i], mbap, 7, 500)) {
                        uint16_t remainingLength = (mbap[4] << 8) | mbap[5];
                        if (remainingLength > 0 && remainingLength < 260) {
                            uint16_t pduLen = remainingLength - 1; 
                            uint8_t* pdu = new uint8_t[pduLen];
                            
                            if (readExact(clients[i], pdu, pduLen, 500)) {
                                updateClientStats(clients[i].remoteIP());
                                
                                if (xSemaphoreTake(backendMutex, pdMS_TO_TICKS(2500)) == pdTRUE) {
                                    if (!backendClient.connected()) {
                                        if (millis() - lastBackendConnectAttempt >= RECONNECT_DELAY || lastBackendConnectAttempt == 0) {
                                            lastBackendConnectAttempt = millis();
                                            if (backendClient.connect(targetModbusIP, MODBUS_SERVER_PORT)) {
                                                currentBackendState = BK_CONNECTED;
                                            } else {
                                                currentBackendState = BK_CON_ERR; 
                                            }
                                        }
                                    }

                                    if (backendClient.connected()) {
                                        backendClient.write(mbap, 7);
                                        backendClient.write(pdu, pduLen);
                                        
                                        uint8_t resMbap[7];
                                        if (readExact(backendClient, resMbap, 7, 500)) {
                                            uint16_t resRemainingLength = (resMbap[4] << 8) | resMbap[5];
                                            if (resRemainingLength > 0 && resRemainingLength < 260) {
                                                uint16_t resPduLen = resRemainingLength - 1; 
                                                uint8_t* resPdu = new uint8_t[resPduLen];
                                                if (readExact(backendClient, resPdu, resPduLen, 500)) {
                                                    clients[i].write(resMbap, 7);
                                                    clients[i].write(resPdu, resPduLen);
                                                }
                                                delete[] resPdu;
                                            }
                                        } else { 
                                            backendClient.stop(); 
                                            currentBackendState = BK_STANDBY; 
                                        }
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
        
        int activeClients = getActiveClientCount();
        if (activeClients == 0) {
            if (backendClient.connected()) {
                xSemaphoreTake(backendMutex, portMAX_DELAY);
                backendClient.stop();
                xSemaphoreGive(backendMutex);
            }
            if (currentBackendState != BK_STARTUP_PING && currentBackendState != BK_PING_ERR) {
                currentBackendState = BK_WAITING;
            }
        } else {
            if (!backendClient.connected() && currentBackendState != BK_CON_ERR) {
                currentBackendState = BK_STANDBY;
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
            if (menosPressed) currentMenuOption = (currentMenuOption + 1) % 6;
            if (masPressed) currentMenuOption = (currentMenuOption - 1 + 6) % 6;
            if (backPressed) currentMenuState = MENU_IDLE;
            if (okPressed) {
                if (currentMenuOption == 0) { currentMenuState = MENU_CLIENTS_LIST; selectedClientIdx = 0; } 
                else if (currentMenuOption == 1) { currentMenuState = MENU_TEST_PING; diagnosticResult = "OK:Iniciar Test"; } 
                else if (currentMenuOption == 2) { currentMenuState = MENU_TEST_FIXED; diagnosticResult = "OK p/ ID " + String(MODBUS_FIXED_ID); } 
                else if (currentMenuOption == 3) { currentMenuState = MENU_TEST_SCAN; diagnosticResult = "OK:Iniciar Scan"; }
                else if (currentMenuOption == 4) { isPaused = !isPaused; } 
                else if (currentMenuOption == 5) { currentMenuState = MENU_SHUTDOWN; diagnosticResult = "OK:Confirmar"; }
            } break;
        case MENU_CLIENTS_LIST:
            if (totalTracked > 0) {
                if (menosPressed) selectedClientIdx = (selectedClientIdx + 1) % totalTracked;
                if (masPressed) selectedClientIdx = (selectedClientIdx - 1 + totalTracked) % totalTracked;
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
        case MENU_SHUTDOWN:
            if (backPressed) currentMenuState = MENU_MAIN; if (okPressed) ejecutarApagado(); break;
        default:
            break;
    }
}

void renderUI() {
    display.clearDisplay();
    display.setTextSize(1);
    
    auto printBottom = [](String left, String right) {
        display.setCursor(0, 56); 
        display.print(left);
        display.setCursor(128 - (right.length() * 6), 56); 
        display.print(right);
    };

    String ipStr = "Conectando...";
    bool tieneNet = false;
    
    if (USE_ETHERNET) {
        if (ETH.localIP() != IPAddress(0,0,0,0)) {
            ipStr = ETH.localIP().toString();
            tieneNet = true;
        }
    } else {
        if (WiFi.status() == WL_CONNECTED) {
            ipStr = WiFi.localIP().toString();
            tieneNet = true;
        }
        else if (WiFi.status() == WL_NO_SSID_AVAIL) ipStr = "Sin SSID WiFi";
        else ipStr = "Desconectado";
    }
    
    int activeSockets = getActiveClientCount();
    int totalTracked = 0;
    for (int i = 0; i < MAX_TRACKED_IPS; i++) if (trackedClients[i].isUsed) totalTracked++;

    switch (currentMenuState) {
        case MENU_IDLE: {
            display.setCursor(0, 0); display.println("PROXY MODBUS");
            display.setCursor(0, 13); display.print("IP: "); display.println(ipStr);
            
            String serverStatus = "WAITING";
            if (!tieneNet) {
                serverStatus = "OFFLINE";
            } else if (currentBackendState == BK_STARTUP_PING) {
                serverStatus = "PINGING...";
            } else if (currentBackendState == BK_PING_ERR) {
                serverStatus = "PING ERR (" + String(pingCountdown) + "s)";
            } else if (currentBackendState == BK_PAUSED) {
                serverStatus = "PAUSADO";
            } else if (currentBackendState == BK_WAITING) {
                serverStatus = "WAITING";
            } else if (currentBackendState == BK_STANDBY) {
                serverStatus = "STANDBY";
            } else if (currentBackendState == BK_CONNECTED) {
                serverStatus = "CONNECTED";
            } else if (currentBackendState == BK_CON_ERR) {
                serverStatus = "CON-ERR";
            }
            
            display.setCursor(0, 24); display.print("Server: "); display.println(serverStatus);
            display.setCursor(0, 35); display.print("EMMA: "); 
            
            bool ocultarModelOled = (currentBackendState == BK_PAUSED || currentBackendState == BK_PING_ERR || currentBackendState == BK_STARTUP_PING);
            if (ocultarModelOled) {
                display.println("");
            } else {
                display.println(emmaDeviceModel);
            }

            display.setCursor(0, 46); display.print("Clientes: "); 
            display.print(activeSockets); display.print("/"); display.println(totalTracked);
            printBottom("", "OK:Menu"); 
            break;
        }   
        case MENU_MAIN: {
            display.setCursor(0, 0); display.println("MENU PRINCIPAL");
            
            String toggleText = isPaused ? "5. Reanudar Comms" : "5. Pausar Comms";
            
            const String menuItems[] = {
                "1. Historial IPs", 
                "2. Test Ping Red", 
                "3. Modbus Fijo", 
                "4. Escaner Auto-HA", 
                toggleText,
                "6. Apagar Proxy"
            };
            
            int startIdx = currentMenuOption - 2;
            if (startIdx < 0) startIdx = 0;
            if (startIdx > 2) startIdx = 2;
            
            for(int i = 0; i < 4; i++) {
                int itemIdx = startIdx + i;
                display.setCursor(2, 14 + (i * 10)); 
                display.print(currentMenuOption == itemIdx ? "> " : "  "); 
                display.println(menuItems[itemIdx]);
            }
            
            printBottom("+-:Mover", "OK:Entrar"); 
            break; 
        }
        case MENU_CLIENTS_LIST: {
            display.setCursor(0, 0); display.println("HISTORIAL IPS");
            int mappings[MAX_TRACKED_IPS];
            int currentMapped = 0;
            for (int i = 0; i < MAX_TRACKED_IPS; i++) if (trackedClients[i].isUsed) mappings[currentMapped++] = i;
            
            if (totalTracked == 0) { 
                display.setCursor(0, 28); display.println("Sin IPs registradas"); 
                printBottom("BACK:Atras", ""); 
            } else { 
                display.setCursor(0, 26); display.print(" -> "); display.println(trackedClients[mappings[selectedClientIdx]].ip.toString()); 
                String rightTxt = "OK:Ver " + String(selectedClientIdx+1) + "/" + String(totalTracked);
                printBottom("BACK:Atras", rightTxt); 
            }
            break; }
            
        case MENU_CLIENTS_DETAIL: {
            display.setCursor(0, 0); display.println("DETALLES IP");
            int mappings[MAX_TRACKED_IPS];
            int currentMapped = 0;
            for (int i = 0; i < MAX_TRACKED_IPS; i++) if (trackedClients[i].isUsed) mappings[currentMapped++] = i;
            
            if (totalTracked > 0 && selectedClientIdx < totalTracked) {
                ClientStats sc = trackedClients[mappings[selectedClientIdx]];
                uint32_t currentUptime = millis() / 1000;
                uint32_t elapsedSeconds = currentUptime - sc.lastRequestTimestamp;
                
                display.setCursor(0, 16); display.print("IP: "); display.println(sc.ip.toString());
                display.setCursor(0, 30); display.print("Peticiones: "); display.println(sc.requestCount);
                display.setCursor(0, 44); display.print("Hace: "); display.print(elapsedSeconds); display.println(" segs");
            }
            printBottom("BACK:Atras", ""); 
            break; }
            
        case MENU_TEST_PING:
            display.setCursor(0, 0); display.println("TEST PING ICMP"); 
            display.setCursor(0, 26); display.println(diagnosticResult); 
            printBottom("BACK:Salir", "OK:Test"); 
            break;
            
        case MENU_TEST_FIXED:
            display.setCursor(0, 0); display.println("MODBUS FIJO"); 
            display.setCursor(0, 20); display.println(diagnosticResult); 
            printBottom("BACK:Salir", "OK:Test"); 
            break;
            
        case MENU_TEST_SCAN:
            display.setCursor(0, 0); display.println("ESCANER AUTO-HA"); 
            display.setCursor(0, 26); display.println(diagnosticResult); 
            printBottom("BACK:Salir", "OK:Scan"); 
            break;
            
        case MENU_SHUTDOWN:
            display.setCursor(0, 0); display.println("APAGAR PROXY"); 
            display.setCursor(0, 26); display.println(diagnosticResult); 
            printBottom("BACK:Salir", "OK:Aceptar"); 
            break;
        default:
            break;
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
            uint8_t reqFrame[] = { 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, MODBUS_FIXED_ID, 0x03, 0x75, 0x30, 0x00, 0x0F };
            backendClient.write(reqFrame, 12);
            uint8_t resMbap[7];
            if (readExact(backendClient, resMbap, 7, 500)) {
                uint16_t rLen = (resMbap[4] << 8) | resMbap[5];
                if (rLen >= 3 && rLen < 100) {
                    uint16_t pduLen = rLen - 1; 
                    uint8_t* resPdu = new uint8_t[pduLen];
                    if (readExact(backendClient, resPdu, pduLen, 1500)) {
                        if (resPdu[0] == 0x03 || resPdu[0] == 0x04) { 
                            uint8_t bCount = resPdu[1];
                            char* tStr = new char[bCount + 1];
                            for (int m = 0; m < bCount; m++) {
                                char c = (char)resPdu[2 + m];
                                if (c >= 32 && c <= 126) tStr[m] = c;
                                else tStr[m] = ' ';
                            }
                            tStr[bCount] = '\0';
                            diagnosticResult = "OK! Leido:\n" + String(tStr);
                            emmaDeviceModel = String(tStr);
                            emmaDeviceModel.trim();
                            checkEmmaStartup = false;
                            delete[] tStr;
                        } else if (resPdu[0] == 0x83 || resPdu[0] == 0x84) {
                            diagnosticResult = "Modbus Ok!\nRespuesta Excepcion Viva";
                            emmaDeviceModel = "SmartHEMS";
                            checkEmmaStartup = false;
                        } else {
                            diagnosticResult = "Err Modbus: 0x" + String(resPdu[0], HEX);
                        }
                    } else { diagnosticResult = "Error Lectura PDU"; }
                    delete[] resPdu;
                } else { diagnosticResult = "MBAP Invalido"; }
            } else { diagnosticResult = "Timeout sin respuesta"; }
            backendClient.stop();
        } else { diagnosticResult = "Fallo TCP IP:502"; }
        currentBackendState = BK_STANDBY;
        xSemaphoreGive(backendMutex);
    } else { diagnosticResult = "Canal Bus Bloqueado"; }
}

void ejecutarEscanerModbus() {
    uint8_t idsAProbar[] = {1, 2, 3, 0, 16, 100, 255}; 
    bool idEncontrado = false; 
    uint8_t idExitoso = 0; 
    String textoDetectado = "";

    diagnosticResult = "Buscando Inversor..."; 
    renderUI();

    if (xSemaphoreTake(backendMutex, pdMS_TO_TICKS(15000)) == pdTRUE) {
        for (int i = 0; i < 7; i++) {
            uint8_t idActual = idsAProbar[i];
            diagnosticResult = "Probando ID: " + String(idActual) + "..."; 
            renderUI();

            if (backendClient.connected()) backendClient.stop();
            
            if (backendClient.connect(targetModbusIP, MODBUS_SERVER_PORT)) {
                uint8_t reqFrame[] = { 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, idActual, 0x03, 0x75, 0x30, 0x00, 0x0F };
                backendClient.write(reqFrame, 12);
                
                uint8_t resMbap[7];
                if (readExact(backendClient, resMbap, 7, 500)) {
                    uint16_t rLen = (resMbap[4] << 8) | resMbap[5];
                    if (rLen >= 3 && rLen < 50) {
                        uint16_t pduLen = rLen - 1; 
                        uint8_t* resPdu = new uint8_t[pduLen];
                        
                        if (readExact(backendClient, resPdu, pduLen, 500)) {
                            if (resPdu[0] == 0x03 || resPdu[0] == 0x04) { 
                                uint8_t bCount = resPdu[1];
                                char* tempStr = new char[bCount + 1];
                                for(int m=0; m<bCount; m++) {
                                    char c = (char)resPdu[2+m];
                                    if(c >= 32 && c <= 126) tempStr[m] = c;
                                    else tempStr[m] = ' ';
                                }
                                tempStr[bCount] = '\0';
                                textoDetectado = String(tempStr);
                                textoDetectado.trim();
                                idExitoso = idActual;
                                idEncontrado = true;
                                delete[] tempStr;
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
        currentBackendState = BK_STANDBY;
        xSemaphoreGive(backendMutex);

        if (idEncontrado) { 
            diagnosticResult = "ID: " + String(idExitoso) + "\nLeido: " + textoDetectado; 
            emmaDeviceModel = textoDetectado;
            checkEmmaStartup = false;
        } else { 
            diagnosticResult = "Escaneo Fallido.\nNingun ID responde."; 
        }
    } else { 
        diagnosticResult = "Canal Bus Bloqueado"; 
    }
}

void ejecutarApagado() {
    diagnosticResult = "Cerrando TCP..."; 
    renderUI();
    
    if (xSemaphoreTake(backendMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        if (backendClient.connected()) {
            backendClient.stop(); 
        }
        xSemaphoreGive(backendMutex);
    }
    
    proxyServer.end(); 
    webServer.close();
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i].connected()) {
            clients[i].stop();
        }
    }
    
    delay(500); 
    
    display.clearDisplay();
    display.setTextSize(2); 
    display.setTextColor(SH110X_WHITE);
    display.setCursor(18, 20);
    display.println("APAGADO");
    
    display.setTextSize(1);
    display.setCursor(6, 45);
    display.println("Seguro desconectar");
    display.display();
    
    while(true) {
        delay(1000); 
    }
}