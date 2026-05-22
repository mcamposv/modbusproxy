#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <ETH.h>
#include "secrets.h"

// ====================================================================
// CONFIGURACIÓN PARAMETRIZABLE DEL PROYECTO
// ====================================================================
const bool USE_ETHERNET   = false;  // true = WT32-ETH01 (Ethernet), false = ESP32 estándar (WiFi)
const bool USE_DHCP       = true;   // true = DHCP, false = IP Fija Estática
const bool ROTATE_SCREEN  = false;  // true = Rota pantalla 180 grados, false = Normal

// Configuración de direccionamiento estático (solo si USE_DHCP = false)
IPAddress local_IP(192, 168, 1, 150);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns_primary(8, 8, 8, 8);
// ====================================================================

// Mapeo físico validado de botones
const int BOTON_OK    = 4;
const int BOTON_BACK  = 14;
const int BOTON_MAS   = 15;
const int BOTON_MENOS = 34; // Requiere pull-up físico a 3V3

// Parámetros Pantalla OLED SH1106
#define DIRECCION_I2C 0x3C
#define ANCHO_PANTALLA 128
#define ALTO_PANTALLA 64
Adafruit_SH1106G display(ANCHO_PANTALLA, ALTO_PANTALLA, &Wire, -1);

// Servidor Proxy Modbus TCP (Puerto estándar 502)
WiFiServer proxyServer(502);
const int MAX_CLIENTS = 4;
WiFiClient clients[MAX_CLIENTS];

// Cliente de salida única hacia el PLC/Servidor de destino
WiFiClient backendClient;
SemaphoreHandle_t backendMutex;

// Estructuras para base de datos de Estadísticas en tiempo real
struct ClientStats {
    IPAddress ip;
    uint32_t requestCount = 0;
    uint32_t lastRequestTimestamp = 0;
    bool isUsed = false;
};
const int MAX_TRACKED_IPS = 10;
ClientStats trackedClients[MAX_TRACKED_IPS];

// Máquina de estados del menú de pantalla
enum MenuState {
    MENU_IDLE,
    MENU_MAIN,
    MENU_CLIENTS_LIST,
    MENU_CLIENTS_DETAIL,
    MENU_TEST_MODBUS
};
MenuState currentMenuState = MENU_IDLE;
int currentMenuOption = 0; // 0: Ver Estadísticas, 1: Test Servidor
int selectedClientIdx = 0;
String modbusTestResult = "Presione OK para Test";

// Variables globales para flancos de botones
bool okPressed = false;
bool backPressed = false;
bool masPressed = false;
bool menosPressed = false;

bool lastOkState = HIGH;
bool lastBackState = HIGH;
bool lastMasState = HIGH;
bool lastMenosState = HIGH;

// Prototipos de funciones auxiliares
bool readExact(WiFiClient &client, uint8_t* buffer, size_t length, uint32_t timeoutMs = 1000);
void updateClientStats(IPAddress ip);
void taskModbusProxy(void *parameter);
void checkButtons();
void handleNavigation();
void renderUI();
void ejecutarTestModbus();
int getActiveClientCount();

void setup() {
    Serial.begin(115200);

    // Configuración eléctrica de pines de entradas digitales (Botones)
    pinMode(BOTON_OK, INPUT_PULLUP);
    pinMode(BOTON_BACK, INPUT_PULLUP);
    pinMode(BOTON_MAS, INPUT_PULLUP);
    pinMode(BOTON_MENOS, INPUT); // Configuración externa física Pull-up a 3V3 ya validada

    // Inicializar bus I2C (SDA=33, SCL=32)
    Wire.begin(33, 32);
    if (!display.begin(DIRECCION_I2C, true)) {
        Serial.println(F("CRÍTICO: No se detecta pantalla OLED SH1106"));
        for (;;);
    }
    
    display.clearDisplay();
    display.setRotation(ROTATE_SCREEN ? 2 : 0);
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 10);
    display.println("INICIANDO PROXY...");
    display.println("Modbus TCP v0.1");
    display.display();

    // Inicializar pila de red según selección paramétrica
    if (USE_ETHERNET) {
        Serial.println("Inicializando interfaz Ethernet (WT32-ETH01)...");
        // Pines nativos del chip LAN8720 incorporado en WT32-ETH01
        ETH.begin(1, 16, 23, 18, ETH_PHY_LAN8720, ETH_CLOCK_GPIO0_IN);
        if (!USE_DHCP) {
            ETH.config(local_IP, gateway, subnet, dns_primary);
        }
    } else {
        Serial.println("Inicializando interfaz Inalámbrica WiFi...");
        if (!USE_DHCP) {
            WiFi.config(local_IP, gateway, subnet, dns_primary);
        }
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
        Serial.println("\nWiFi Conectado!");
    }

    // Inicializar exclusión mutua (Mutex) para la serialización de consultas al Servidor Final
    backendMutex = xSemaphoreCreateMutex();

    // Crear la tarea en segundo plano dedicada exclusivamente a la capa proxy (Ejecución en Core 0)
    xTaskCreatePinnedToCore(
        taskModbusProxy,
        "TaskModbusProxy",
        8192,
        NULL,
        1,
        NULL,
        0
    );

    Serial.println("Iniciando Core de UI y navegación en Core 1...");
}

void loop() {
    checkButtons();
    handleNavigation();
    renderUI();
    delay(20); // Ciclo base de refresco e interacción
}

// Lectura síncrona exacta con tiempo límite para streams TCP fragmentados
bool readExact(WiFiClient &client, uint8_t* buffer, size_t length, uint32_t timeoutMs) {
    size_t bytesRead = 0;
    uint32_t startMs = millis();
    while (bytesRead < length) {
        if (!client.connected()) return false;
        if (client.available()) {
            int r = client.read(buffer + bytesRead, length - bytesRead);
            if (r > 0) {
                bytesRead += r;
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

// BBDD local en RAM de IPs de Clientes y contadores
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

// Devuelve el recuento instantáneo de sockets de lectura abiertos activos
int getActiveClientCount() {
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i].connected()) {
            count++;
        }
    }
    return count;
}

// Tarea en segundo plano del hilo de red (Core 0): Motor de Colas e Intercambio Serializado
void taskModbusProxy(void *parameter) {
    proxyServer.begin();
    while (true) {
        // Verificar si existe una nueva petición de enlace entrante
        if (proxyServer.hasClient()) {
            WiFiClient newClient = proxyServer.available();
            bool slotFound = false;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (!clients[i] || !clients[i].connected()) {
                    clients[i] = newClient;
                    slotFound = true;
                    break;
                }
            }
            if (!slotFound) {
                newClient.stop(); // Rechazar por desbordamiento de conexiones locales
            }
        }

        // Inspeccionar descriptores activos buscando tramas listas
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] && clients[i].connected()) {
                if (clients[i].available() >= 7) { // Esperar al menos el MBAP Header estándar
                    uint8_t mbap[7];
                    if (readExact(clients[i], mbap, 7)) {
                        uint16_t remainingLength = (mbap[4] << 8) | mbap[5];
                        
                        if (remainingLength > 0 && remainingLength < 260) {
                            uint8_t* pdu = new uint8_t[remainingLength];
                            if (readExact(clients[i], pdu, remainingLength)) {
                                
                                // Incrementar métricas de consulta
                                updateClientStats(clients[i].remoteIP());

                                // BLOQUEO MUTEX: Asegura la serialización pura hacia el Servidor Modbus destino
                                if (xSemaphoreTake(backendMutex, pdMS_TO_TICKS(2500)) == pdTRUE) {
                                    
                                    if (!backendClient.connected()) {
                                        backendClient.connect(MODBUS_SERVER_IP, MODBUS_SERVER_PORT);
                                    }

                                    if (backendClient.connected()) {
                                        // Reenviar la petición exacta
                                        backendClient.write(mbap, 7);
                                        backendClient.write(pdu, remainingLength);
                                        
                                        // Absorber y deserializar la respuesta única desde el PLC
                                        uint8_t resMbap[7];
                                        if (readExact(backendClient, resMbap, 7, 1000)) {
                                            uint16_t resRemainingLength = (resMbap[4] << 8) | resMbap[5];
                                            
                                            if (resRemainingLength > 0 && resRemainingLength < 260) {
                                                uint8_t* resPdu = new uint8_t[resRemainingLength];
                                                if (readExact(backendClient, resPdu, resRemainingLength, 1000)) {
                                                    
                                                    // Responder exclusivamente al cliente legítimo que originó la transacción
                                                    clients[i].write(resMbap, 7);
                                                    clients[i].write(resPdu, resRemainingLength);
                                                }
                                                delete[] resPdu;
                                            }
                                        }
                                    }
                                    xSemaphoreGive(backendMutex); // Liberación del bus Modbus
                                }
                            }
                            delete[] pdu;
                        }
                    }
                }
            }
        }
        delay(2); // Ceder control al planificador de tareas de FreeRTOS
    }
}

// Detección anti-rebote elemental por flanco descendente
void checkButtons() {
    bool currentOk = digitalRead(BOTON_OK);
    bool currentBack = digitalRead(BOTON_BACK);
    bool currentMas = digitalRead(BOTON_MAS);
    bool currentMenos = digitalRead(BOTON_MENOS);

    okPressed = (currentOk == LOW && lastOkState == HIGH);
    backPressed = (currentBack == LOW && lastBackState == HIGH);
    masPressed = (currentMas == LOW && lastMasState == HIGH);
    menosPressed = (currentMenos == LOW && lastMenosState == HIGH);

    lastOkState = currentOk;
    lastBackState = currentBack;
    lastMasState = currentMas;
    lastMenosState = currentMenos;

    if (okPressed || backPressed || masPressed || menosPressed) {
        delay(40); // Blindaje contra transitorios eléctricos
    }
}

// Máquina de estados finitos de navegación por menús
void handleNavigation() {
    int totalTracked = 0;
    for (int i = 0; i < MAX_TRACKED_IPS; i++) {
        if (trackedClients[i].isUsed) totalTracked++;
    }

    switch (currentMenuState) {
        case MENU_IDLE:
            if (okPressed || masPressed || menosPressed) {
                currentMenuState = MENU_MAIN;
                currentMenuOption = 0;
            }
            break;
            
        case MENU_MAIN:
            if (masPressed || menosPressed) {
                currentMenuOption = (currentMenuOption == 0) ? 1 : 0;
            }
            if (backPressed) {
                currentMenuState = MENU_IDLE;
            }
            if (okPressed) {
                if (currentMenuOption == 0) {
                    currentMenuState = MENU_CLIENTS_LIST;
                    selectedClientIdx = 0;
                } else {
                    currentMenuState = MENU_TEST_MODBUS;
                    modbusTestResult = "Presione OK para Test";
                }
            }
            break;
            
        case MENU_CLIENTS_LIST:
            if (totalTracked > 0) {
                if (masPressed) selectedClientIdx = (selectedClientIdx + 1) % totalTracked;
                if (menosPressed) selectedClientIdx = (selectedClientIdx - 1 + totalTracked) % totalTracked;
            }
            if (backPressed) {
                currentMenuState = MENU_MAIN;
            }
            if (okPressed && totalTracked > 0) {
                currentMenuState = MENU_CLIENTS_DETAIL;
            }
            break;
            
        case MENU_CLIENTS_DETAIL:
            if (backPressed || okPressed) {
                currentMenuState = MENU_CLIENTS_LIST;
            }
            break;
            
        case MENU_TEST_MODBUS:
            if (backPressed) {
                currentMenuState = MENU_MAIN;
            }
            if (okPressed) {
                ejecutarTestModbus();
            }
            break;
    }
}

// Motor gráfico e impresión en pantalla OLED
void renderUI() {
    display.clearDisplay();
    
    // Lectura dinâmica de IP local del adaptador activo
    String ipStr = USE_ETHERNET ? ETH.localIP().toString() : WiFi.localIP().toString();
    bool backendCon = backendClient.connected();
    int activeSockets = getActiveClientCount();

    switch (currentMenuState) {
        case MENU_IDLE:
            display.setTextSize(1);
            display.setCursor(0, 0);
            display.println("=== PROXY MODBUS ===");
            
            display.setCursor(0, 18);
            display.print("IP Local: "); display.println(ipStr);
            
            display.setCursor(0, 32);
            display.print("Servidor: "); 
            display.println(backendCon ? "CONECTADO" : "DESCONECTADO");
            
            display.setCursor(0, 46);
            display.print("Clientes: "); display.println(activeSockets);
            
            display.setCursor(0, 56);
            display.print("[ OK / Menu principal ]");
            break;
            
        case MENU_MAIN:
            display.setCursor(0, 0);
            display.println("--- MENU PRINCIPAL ---");
            
            display.setCursor(5, 22);
            display.print(currentMenuOption == 0 ? "> " : "  ");
            display.println("1. Historial IPs");
            
            display.setCursor(5, 38);
            display.print(currentMenuOption == 1 ? "> " : "  ");
            display.println("2. Test Diagnostico");
            
            display.setCursor(0, 56);
            display.print("+-:Mover | OK:Seleccionar");
            break;
            
        case MENU_CLIENTS_LIST: {
            display.println("--- HISTORIAL IPS ---");
            int totalTracked = 0;
            int mappings[MAX_TRACKED_IPS];
            for (int i = 0; i < MAX_TRACKED_IPS; i++) {
                if (trackedClients[i].isUsed) mappings[totalTracked++] = i;
            }
            
            if (totalTracked == 0) {
                display.setCursor(0, 28);
                display.println(" Sin IPs registradas");
            } else {
                if (selectedClientIdx >= totalTracked) selectedClientIdx = 0;
                display.setCursor(0, 26);
                display.print(" -> "); 
                display.println(trackedClients[mappings[selectedClientIdx]].ip.toString());
                
                display.setCursor(0, 56);
                display.print("Ver: OK | Item " + String(selectedClientIdx+1) + "/" + String(totalTracked));
            }
            break;
        }
            
        case MENU_CLIENTS_DETAIL: {
            display.println("--- METRICAS DISP ---");
            int totalTracked = 0;
            int mappings[MAX_TRACKED_IPS];
            for (int i = 0; i < MAX_TRACKED_IPS; i++) {
                if (trackedClients[i].isUsed) mappings[totalTracked++] = i;
            }
            
            if (totalTracked > 0 && selectedClientIdx < totalTracked) {
                ClientStats sc = trackedClients[mappings[selectedClientIdx]];
                display.setCursor(0, 16);
                display.print("IP: "); display.println(sc.ip.toString());
                display.setCursor(0, 28);
                display.print("Peticiones: "); display.println(sc.requestCount);
                display.setCursor(0, 40);
                display.print("Ult. segs: "); display.println(sc.lastRequestTimestamp);
            }
            display.setCursor(0, 56);
            display.print("Atras: BACK");
            break;
        }
            
        case MENU_TEST_MODBUS:
            display.println("--- TEST DIAGNOSTICO ---");
            display.setCursor(0, 26);
            display.println(modbusTestResult);
            
            display.setCursor(0, 56);
            display.print("OK: Ejecutar | BACK: Salir");
            break;
    }
    display.display();
}

// Envío de paquete Modbus TCP de prueba forzado bajo exclusión del canal común
void ejecutarTestModbus() {
    modbusTestResult = "Enviando Trama...";
    if (xSemaphoreTake(backendMutex, pdMS_TO_TICKS(3000)) == pdTRUE) {
        if (!backendClient.connected()) {
            backendClient.connect(MODBUS_SERVER_IP, MODBUS_SERVER_PORT);
        }
        if (backendClient.connected()) {
            // Trama Modbus TCP nativa: Read Holding Registers (FC 03), Dirección 0, Cantidad 1, Unit ID 1
            uint8_t reqFrame[] = {
                0x12, 0x34, // Transaction ID
                0x00, 0x00, // Protocol ID
                0x00, 0x06, // Longitud
                0x01,       // Unit ID
                0x03,       // FC
                0x00, 0x00, // Addr 0
                0x00, 0x01  // Cantidad 1
            };
            backendClient.write(reqFrame, 12);
            
            uint8_t resMbap[7];
            if (readExact(backendClient, resMbap, 7, 1500)) {
                uint16_t rLen = (resMbap[4] << 8) | resMbap[5];
                if (rLen >= 3 && rLen < 30) {
                    uint8_t* resPdu = new uint8_t[rLen];
                    if (readExact(backendClient, resPdu, rLen, 1500)) {
                        if (resPdu[1] == 0x03) { // Validación de FC libre de excepción
                            uint16_t valorReg = (resPdu[3] << 8) | resPdu[4];
                            modbusTestResult = "RTA OK! Reg0 = " + String(valorReg);
                        } else {
                            modbusTestResult = "Excep. Modbus: 0x" + String(resPdu[2], HEX);
                        }
                    } else {
                        modbusTestResult = "Error Lectura PDU";
                    }
                    delete[] resPdu;
                } else {
                    modbusTestResult = "Header MBAP Invalido";
                }
            } else {
                modbusTestResult = "Timeout sin Respuesta";
            }
        } else {
            modbusTestResult = "Fallo Enlace IP PLC";
        }
        xSemaphoreGive(backendMutex);
    } else {
        modbusTestResult = "Canal Bus Bloqueado";
    }
}
