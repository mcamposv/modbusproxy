#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WiFi.h>
#include <ETH.h>
#include <ESP32Ping.h> 
#include <WebServer.h> 
#include <ArduinoOTA.h> 
#include <Preferences.h>
#include "secrets.h"   // Valores default del primer arranque — EXCLUIDO DE GIT
#include <esp_wifi.h>
#include <esp_log.h>

// ====================================================================
// CONFIGURACIÓN PERSISTENTE EN NVS (Non-Volatile Storage)
// Todos los valores arrancan con los defaults del firmware.
// Cualquier cambio desde la web se guarda en flash y sobrevive reinicios.
// ====================================================================
Preferences prefs;

struct AppConfig {
    bool   useEthernet      = false;
    bool   useDHCP          = false;
    bool   rotateScreen     = false;
    char   wifiSSID[64]     = DEFAULT_WIFI_SSID;      // <- secrets.h
    char   wifiPass[64]     = DEFAULT_WIFI_PASSWORD;   // <- secrets.h
    char   modbusIP[16]     = DEFAULT_MODBUS_IP;       // <- secrets.h
    uint16_t modbusPort     = DEFAULT_MODBUS_PORT;     // <- secrets.h
    char   localIP[16]      = "192.168.1.103";
    char   gateway[16]      = "192.168.1.1";
    char   subnet[16]       = "255.255.255.0";
    char   dns[16]          = "192.168.1.1";
    char   otaPassword[32]  = DEFAULT_OTA_PASSWORD;    // <- secrets.h
};

AppConfig cfg;

void loadConfig() {
    prefs.begin("proxycfg", true); // read-only
    cfg.useEthernet  = prefs.getBool  ("useEth",     false);
    cfg.useDHCP      = prefs.getBool  ("useDHCP",    false);
    cfg.rotateScreen = prefs.getBool  ("rotScr",     false);
    prefs.getString("wifiSSID",  cfg.wifiSSID,    sizeof(cfg.wifiSSID));
    prefs.getString("wifiPass",  cfg.wifiPass,    sizeof(cfg.wifiPass));
    prefs.getString("modbusIP",  cfg.modbusIP,    sizeof(cfg.modbusIP));
    cfg.modbusPort   = prefs.getUShort("modbusPort", 502);
    prefs.getString("localIP",   cfg.localIP,     sizeof(cfg.localIP));
    prefs.getString("gateway",   cfg.gateway,     sizeof(cfg.gateway));
    prefs.getString("subnet",    cfg.subnet,      sizeof(cfg.subnet));
    prefs.getString("dns",       cfg.dns,         sizeof(cfg.dns));
    prefs.getString("otaPass",   cfg.otaPassword, sizeof(cfg.otaPassword));
    prefs.end();
}

void saveConfig() {
    prefs.begin("proxycfg", false); // read-write
    prefs.putBool  ("useEth",     cfg.useEthernet);
    prefs.putBool  ("useDHCP",    cfg.useDHCP);
    prefs.putBool  ("rotScr",     cfg.rotateScreen);
    prefs.putString("wifiSSID",   cfg.wifiSSID);
    prefs.putString("wifiPass",   cfg.wifiPass);
    prefs.putString("modbusIP",   cfg.modbusIP);
    prefs.putUShort("modbusPort", cfg.modbusPort);
    prefs.putString("localIP",    cfg.localIP);
    prefs.putString("gateway",    cfg.gateway);
    prefs.putString("subnet",     cfg.subnet);
    prefs.putString("dns",        cfg.dns);
    prefs.putString("otaPass",    cfg.otaPassword);
    prefs.end();
}

// IPs en tiempo de ejecucion (se rellenan desde cfg al arrancar)
IPAddress local_IP;
IPAddress gateway_IP;
IPAddress subnet_IP;
IPAddress dns_IP;
IPAddress targetModbusIP;

const int BOTON_OK    = 4;
const int BOTON_BACK  = 14;
const int BOTON_MAS   = 15;
const int BOTON_MENOS = 39;

const uint8_t  MODBUS_FIXED_ID = 0;
const uint16_t MODBUS_TEST_REG = 30000;
const uint32_t RECONNECT_DELAY = 100;

const String FIRMWARE_VERSION = "4.5.0";
// ====================================================================

// Máquina de estados extendida
enum BackendState { BK_STARTUP_PING, BK_PING_ERR, BK_WAITING, BK_STANDBY, BK_CONNECTED, BK_CON_ERR, BK_PAUSED, BK_SHUTDOWN };
BackendState currentBackendState = BK_STARTUP_PING;

// Variables de Bloqueo y Ping
bool pingSuccess = false;
bool isPaused = false;
bool otaInProgress = false; 
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

// ====================================================================
// REGISTRO CIRCULAR DE TRANSACCIONES MODBUS (LOG)
// ====================================================================
struct ModbusTransaction {
    uint32_t timestamp;      // millis() / 1000
    char     clientIP[16];   // IP origen del cliente
    uint8_t  unitID;         // Unit ID Modbus
    uint8_t  funcCode;       // Código de función
    uint16_t regAddress;     // Dirección de registro (de la request)
    uint16_t regCount;       // Cantidad de registros (de la request)
    char     destIP[16];     // IP destino (EMMA/inversor)
    bool     requestOk;      // Trama de request bien formada
    bool     responseOk;     // Respuesta recibida y válida
    uint8_t  errorCode;      // 0 = sin error; otro = código excepción Modbus
    char     reqHex[80];     // Bytes hex de la request completa
    char     resHex[80];     // Bytes hex de la response completa
};

const int MAX_TX_LOG = 200;
ModbusTransaction txLog[MAX_TX_LOG];
int txLogHead  = 0;   // Índice circular: siguiente posición de escritura
int txLogCount = 0;   // Cuántas entradas válidas hay (0..MAX_TX_LOG)
SemaphoreHandle_t txLogMutex;

// Convierte un bloque de bytes en cadena HEX separada por espacios
void bytesToHexStr(const uint8_t* data, size_t len, char* out, size_t outLen) {
    out[0] = '\0';
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 3 < outLen; i++) {
        uint8_t b = data[i];
        out[pos++] = "0123456789ABCDEF"[b >> 4];
        out[pos++] = "0123456789ABCDEF"[b & 0x0F];
        if (i + 1 < len && pos + 1 < outLen - 2) out[pos++] = ' ';
    }
    out[pos] = '\0';
}

void logTransaction(const ModbusTransaction &tx) {
    if (xSemaphoreTake(txLogMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        txLog[txLogHead] = tx;
        txLogHead = (txLogHead + 1) % MAX_TX_LOG;
        if (txLogCount < MAX_TX_LOG) txLogCount++;
        xSemaphoreGive(txLogMutex);
    }
}


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
    MENU_TEST_PING, MENU_TEST_FIXED, MENU_TEST_SCAN, MENU_SHUTDOWN,
    MENU_ABOUT        
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
void handleApiStatus(); 
void handleWebLog();
void handleWebLogCsv();
void handleWebConfig();
void handleWebConfigSave();
String buildNavBar(const String &activePage);

void setupOTA() {
    ArduinoOTA.setPort(3232);
    ArduinoOTA.setHostname("Proxy-Huawei");
    ArduinoOTA.setPassword(cfg.otaPassword);

    ArduinoOTA.onStart([]() {
        otaInProgress = true; 
        if (backendClient.connected()) backendClient.stop();
        if (proxyServer.hasClient()) proxyServer.available().stop();
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] && clients[i].connected()) clients[i].stop();
        }

        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);
        display.setCursor(0, 10);
        display.println("MODO OTA ACTIVADO");
        display.setCursor(0, 25);
        display.println("Recibiendo firmware...");
        display.display();
    });

    ArduinoOTA.onEnd([]() {
        display.clearDisplay();
        display.setTextSize(2);
        display.setCursor(12, 25);
        display.println("SUCCESS!!");
        display.display();
        delay(1000); 
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        int percent = (progress / (total / 100));
        
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("UPDATING (OTA)");
        
        display.setCursor(0, 20);
        display.print("Progreso: ");
        display.print(percent);
        display.println("%");

        display.drawRect(0, 35, 128, 12, SH110X_WHITE);
        display.fillRect(0, 35, (128 * percent) / 100, 12, SH110X_WHITE);
        
        display.display();
    });

    ArduinoOTA.onError([](ota_error_t error) {
        otaInProgress = false; 
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("ERROR OTA");
        display.setCursor(0, 20);
        if (error == OTA_AUTH_ERROR) display.println("Fallo Autorizacion");
        else if (error == OTA_BEGIN_ERROR) display.println("Fallo Inicio");
        else if (error == OTA_CONNECT_ERROR) display.println("Fallo Conexion");
        else if (error == OTA_RECEIVE_ERROR) display.println("Fallo Recepcion");
        else if (error == OTA_END_ERROR) display.println("Fallo Fin");
        display.display();
        delay(3000);
    });

    ArduinoOTA.begin();
}

void setup() {
    Serial.begin(115200);
    esp_log_level_set("*", ESP_LOG_NONE);

    // Cargar configuracion persistente desde NVS.
    // Si es el primer arranque, el struct ya tiene los defaults del firmware.
    loadConfig();

    // Convertir strings de cfg a objetos IPAddress para el stack de red
    local_IP.fromString(cfg.localIP);
    gateway_IP.fromString(cfg.gateway);
    subnet_IP.fromString(cfg.subnet);
    dns_IP.fromString(cfg.dns);
    targetModbusIP.fromString(cfg.modbusIP);
    pinMode(BOTON_OK, INPUT_PULLUP); pinMode(BOTON_BACK, INPUT_PULLUP);
    pinMode(BOTON_MAS, INPUT_PULLUP); pinMode(BOTON_MENOS, INPUT); 

    Wire.begin(33, 32);
    if (!display.begin(DIRECCION_I2C, true)) for (;;);
    
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(0, 10);
    display.println("INICIANDO PROXY...");
    display.println("Modbus TCP v3.2 (HA)"); 
    display.display();

    if (cfg.useEthernet) {
        ETH.begin(1, 16, 23, 18, ETH_PHY_LAN8720, ETH_CLOCK_GPIO0_IN);
        if (!cfg.useDHCP) ETH.config(local_IP, gateway_IP, subnet_IP, dns_IP);
    } else {
        if (!cfg.useDHCP) WiFi.config(local_IP, gateway_IP, subnet_IP, dns_IP);
        WiFi.begin(cfg.wifiSSID, cfg.wifiPass);
    }
    
    delay(500); 
    setupOTA(); 

    webServer.on("/",            handleWebRoot);
    webServer.on("/apagar",      handleWebShutdown);
    webServer.on("/api/status",  handleApiStatus);
    webServer.on("/log",         handleWebLog);
    webServer.on("/log.csv",     handleWebLogCsv);
    webServer.on("/config",      HTTP_GET,  handleWebConfig);
    webServer.on("/config/save", HTTP_POST, handleWebConfigSave);
    webServer.begin();

    backendMutex = xSemaphoreCreateMutex();
    txLogMutex   = xSemaphoreCreateMutex();

    // ----------------------------------------------------------
    // VOLCADO DE INFORMACIÓN DE RED POR PUERTO SERIE AL ARRANQUE
    // ----------------------------------------------------------
    Serial.println();
    Serial.println("============================================");
    Serial.println("  PROXY MODBUS TCP v" + FIRMWARE_VERSION);
    Serial.println("============================================");

    if (cfg.useEthernet) {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_ETH);
        Serial.printf("  Interfaz : ETHERNET (LAN8720)\n");
        Serial.printf("  MAC      : %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        if (!cfg.useDHCP) {
            Serial.print("  IP       : "); Serial.println(local_IP.toString());
            Serial.print("  Subnet   : "); Serial.println(subnet_IP.toString());
            Serial.print("  Gateway  : "); Serial.println(gateway_IP.toString());
        } else {
            Serial.println("  IP       : Esperando DHCP...");
        }
    } else {
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        Serial.printf("  Interfaz : WIFI (STA)\n");
        Serial.printf("  SSID     : %s\n", cfg.wifiSSID);
        Serial.printf("  MAC      : %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) delay(200);
        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("  IP       : "); Serial.println(WiFi.localIP().toString());
            Serial.print("  Subnet   : "); Serial.println(WiFi.subnetMask().toString());
            Serial.print("  Gateway  : "); Serial.println(WiFi.gatewayIP().toString());
        } else {
            Serial.println("  IP       : Sin conexion WiFi todavia");
        }
    }
    Serial.print("  Destino  : ");
    Serial.print(cfg.modbusIP); Serial.print(":"); Serial.println(cfg.modbusPort);
    Serial.println("============================================");
    Serial.println();

    xTaskCreatePinnedToCore(taskModbusProxy, "TaskModbusProxy", 8192, NULL, 1, NULL, 0);
}

void loop() {
    ArduinoOTA.handle(); 
    
    if (otaInProgress) {
        delay(20);
        return; 
    }

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
// BARRA DE NAVEGACIÓN COMPARTIDA
// ====================================================================
String buildNavBar(const String &activePage) {
    String nav = "<nav style='background:#1a1a2e;padding:10px 20px;display:flex;";
    nav += "gap:12px;align-items:center;border-bottom:2px solid #4da6ff;";
    nav += "position:sticky;top:0;z-index:999;flex-wrap:wrap;'>";
    nav += "<span style='color:#4da6ff;font-weight:700;font-size:15px;margin-right:10px;'>&#9641; PROXY MODBUS</span>";

    auto navLink = [&](const String &href, const String &label, const String &page) -> String {
        bool active = (activePage == page);
        String s = "<a href='" + href + "' style='color:";
        s += active ? "#000;background:#4da6ff;" : "#a0c4ff;background:transparent;";
        s += "padding:6px 14px;border-radius:5px;text-decoration:none;font-size:14px;font-weight:";
        s += active ? "700" : "500";
        s += ";border:1px solid ";
        s += active ? "#4da6ff" : "#2a3a5a";
        s += ";'>" + label + "</a>";
        return s;
    };

    nav += navLink("/",       "Dashboard",     "dashboard");
    nav += navLink("/log",    "Log Modbus",    "log");
    nav += navLink("/config", "Configuracion", "config");
    nav += "</nav>";
    return nav;
}

// ====================================================================
// PÁGINA WEB: CONFIGURACIÓN PERSISTENTE
// ====================================================================

// Escapa caracteres especiales HTML para valores de atributos de formulario
String htmlEscape(const char* s) {
    String out;
    for (size_t i = 0; s[i]; i++) {
        char c = s[i];
        if      (c == '&')  out += "&amp;";
        else if (c == '"')  out += "&quot;";
        else if (c == '<')  out += "&lt;";
        else if (c == '>')  out += "&gt;";
        else                out += c;
    }
    return out;
}

void handleWebConfig() {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Configuracion - Proxy Modbus</title>";
    html += "<style>";
    html += "*{box-sizing:border-box;}";
    html += "body{background:#121212;color:#e0e0e0;font-family:'Segoe UI',sans-serif;margin:0;padding:0;}";
    html += ".wrap{max-width:700px;margin:20px auto;padding:0 16px 40px;}";
    html += "h1{color:#4da6ff;border-bottom:1px solid #333;padding-bottom:8px;margin-top:0;}";
    html += "h3{color:#a0c4ff;border-bottom:1px solid #222;padding-bottom:6px;margin-top:28px;}";
    html += ".form-group{margin-bottom:18px;}";
    html += "label{display:block;font-size:13px;color:#aaa;margin-bottom:5px;font-weight:600;}";
    html += "input[type=text],input[type=password],input[type=number]";
    html += "{width:100%;padding:9px 12px;background:#1e1e1e;border:1px solid #333;";
    html += "color:#e0e0e0;border-radius:5px;font-size:14px;}";
    html += "input:focus{outline:none;border-color:#4da6ff;";
    html += "box-shadow:0 0 0 2px rgba(77,166,255,0.2);}";
    html += ".radio-group{display:flex;gap:20px;margin-top:4px;}";
    html += ".radio-group label{display:flex;align-items:center;gap:6px;font-size:14px;";
    html += "color:#e0e0e0;font-weight:400;cursor:pointer;}";
    html += ".section-note{font-size:12px;color:#666;margin-top:4px;}";
    html += ".btn-save{display:block;width:100%;padding:12px;background:#28a745;color:#fff;";
    html += "border:none;border-radius:6px;font-size:16px;font-weight:700;cursor:pointer;margin-top:28px;}";
    html += ".btn-save:hover{background:#1e8035;}";
    html += ".warn{background:#3d2f00;border:1px solid #f39c12;color:#ffe082;";
    html += "padding:12px 16px;border-radius:6px;margin-bottom:20px;font-size:14px;}";
    html += "</style></head><body>";
    html += buildNavBar("config");
    html += "<div class='wrap'>";
    html += "<h1>&#9881; Configuracion del Proxy</h1>";
    html += "<div class='warn'>&#9888; Los cambios se aplican en el <strong>siguiente reinicio</strong>. ";
    html += "El dispositivo se reiniciara automaticamente al guardar.</div>";

    html += "<form method='POST' action='/config/save'>";

    // ---- SECCIÓN: INTERFAZ DE RED ----
    html += "<h3>Interfaz de Red</h3>";

    html += "<div class='form-group'>";
    html += "<label>Tipo de conexion</label>";
    html += "<div class='radio-group'>";
    html += "<label><input type='radio' name='useEth' value='0'";
    html += (!cfg.useEthernet ? " checked" : "");
    html += "> WiFi (inalambrico)</label>";
    html += "<label><input type='radio' name='useEth' value='1'";
    html += (cfg.useEthernet ? " checked" : "");
    html += "> Ethernet (cable RJ45)</label>";
    html += "</div></div>";

    html += "<div class='form-group'>";
    html += "<label>Asignacion de IP</label>";
    html += "<div class='radio-group'>";
    html += "<label><input type='radio' name='useDHCP' value='0'";
    html += (!cfg.useDHCP ? " checked" : "");
    html += "> IP Estatica</label>";
    html += "<label><input type='radio' name='useDHCP' value='1'";
    html += (cfg.useDHCP ? " checked" : "");
    html += "> DHCP (automatica)</label>";
    html += "</div></div>";

    html += "<div class='form-group'>";
    html += "<label>Rotar pantalla OLED 180 grados</label>";
    html += "<div class='radio-group'>";
    html += "<label><input type='radio' name='rotScr' value='0'";
    html += (!cfg.rotateScreen ? " checked" : "");
    html += "> No</label>";
    html += "<label><input type='radio' name='rotScr' value='1'";
    html += (cfg.rotateScreen ? " checked" : "");
    html += "> Si</label>";
    html += "</div></div>";

    // ---- SECCIÓN: CREDENCIALES WIFI ----
    html += "<h3>Credenciales WiFi</h3>";
    html += "<p class='section-note'>Solo se usan si la conexion seleccionada es WiFi.</p>";

    html += "<div class='form-group'>";
    html += "<label for='wifiSSID'>SSID (nombre de la red WiFi)</label>";
    html += "<input type='text' id='wifiSSID' name='wifiSSID' maxlength='63' value='";
    html += htmlEscape(cfg.wifiSSID);
    html += "'></div>";

    html += "<div class='form-group'>";
    html += "<label for='wifiPass'>Contrasena WiFi</label>";
    html += "<input type='password' id='wifiPass' name='wifiPass' maxlength='63' value='";
    html += htmlEscape(cfg.wifiPass);
    html += "'></div>";

    // ---- SECCIÓN: DESTINO MODBUS ----
    html += "<h3>Destino Modbus TCP (EMMA / Inversor)</h3>";

    html += "<div class='form-group'>";
    html += "<label for='modbusIP'>IP del servidor Modbus</label>";
    html += "<input type='text' id='modbusIP' name='modbusIP' maxlength='15' value='";
    html += htmlEscape(cfg.modbusIP);
    html += "'></div>";

    html += "<div class='form-group'>";
    html += "<label for='modbusPort'>Puerto Modbus TCP</label>";
    html += "<input type='number' id='modbusPort' name='modbusPort' min='1' max='65535' value='";
    html += String(cfg.modbusPort);
    html += "'></div>";

    // ---- SECCIÓN: RED ESTÁTICA ----
    html += "<h3>Red Estatica del Proxy</h3>";
    html += "<p class='section-note'>Solo se usa si la asignacion de IP es Estatica.</p>";

    html += "<div class='form-group'>";
    html += "<label for='localIP'>IP del Proxy (este dispositivo)</label>";
    html += "<input type='text' id='localIP' name='localIP' maxlength='15' value='";
    html += htmlEscape(cfg.localIP);
    html += "'></div>";

    html += "<div class='form-group'>";
    html += "<label for='gw'>Puerta de enlace (Gateway)</label>";
    html += "<input type='text' id='gw' name='gw' maxlength='15' value='";
    html += htmlEscape(cfg.gateway);
    html += "'></div>";

    html += "<div class='form-group'>";
    html += "<label for='sn'>Mascara de subred (Subnet)</label>";
    html += "<input type='text' id='sn' name='sn' maxlength='15' value='";
    html += htmlEscape(cfg.subnet);
    html += "'></div>";

    html += "<div class='form-group'>";
    html += "<label for='dns'>DNS primario</label>";
    html += "<input type='text' id='dns' name='dns' maxlength='15' value='";
    html += htmlEscape(cfg.dns);
    html += "'></div>";

    // ---- SECCIÓN: OTA ----
    html += "<h3>Actualizacion OTA</h3>";

    html += "<div class='form-group'>";
    html += "<label for='otaPass'>Contrasena OTA</label>";
    html += "<input type='password' id='otaPass' name='otaPass' maxlength='31' value='";
    html += htmlEscape(cfg.otaPassword);
    html += "'></div>";

    html += "<button type='submit' class='btn-save'>&#128190; Guardar y Reiniciar</button>";
    html += "</form>";
    html += "</div></body></html>";

    webServer.send(200, "text/html", html);
}

void handleWebConfigSave() {
    // Leer todos los campos POST y volcarlos en cfg
    cfg.useEthernet  = (webServer.arg("useEth")  == "1");
    cfg.useDHCP      = (webServer.arg("useDHCP") == "1");
    cfg.rotateScreen = (webServer.arg("rotScr")  == "1");

    String val;

    val = webServer.arg("wifiSSID"); val.trim();
    if (val.length() > 0) strncpy(cfg.wifiSSID, val.c_str(), sizeof(cfg.wifiSSID) - 1);

    // La contrasena puede estar vacia (redes abiertas)
    val = webServer.arg("wifiPass");
    strncpy(cfg.wifiPass, val.c_str(), sizeof(cfg.wifiPass) - 1);

    val = webServer.arg("modbusIP"); val.trim();
    if (val.length() > 0) strncpy(cfg.modbusIP, val.c_str(), sizeof(cfg.modbusIP) - 1);

    int port = webServer.arg("modbusPort").toInt();
    if (port > 0 && port <= 65535) cfg.modbusPort = (uint16_t)port;

    val = webServer.arg("localIP"); val.trim();
    if (val.length() > 0) strncpy(cfg.localIP, val.c_str(), sizeof(cfg.localIP) - 1);

    val = webServer.arg("gw"); val.trim();
    if (val.length() > 0) strncpy(cfg.gateway, val.c_str(), sizeof(cfg.gateway) - 1);

    val = webServer.arg("sn"); val.trim();
    if (val.length() > 0) strncpy(cfg.subnet, val.c_str(), sizeof(cfg.subnet) - 1);

    val = webServer.arg("dns"); val.trim();
    if (val.length() > 0) strncpy(cfg.dns, val.c_str(), sizeof(cfg.dns) - 1);

    val = webServer.arg("otaPass"); val.trim();
    if (val.length() > 0) strncpy(cfg.otaPassword, val.c_str(), sizeof(cfg.otaPassword) - 1);

    saveConfig();
    Serial.println("[CFG] Configuracion guardada en NVS. Reiniciando...");

    // Responder al navegador antes del reinicio
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<meta http-equiv='refresh' content='6;url=/'>"; // Redirige al dashboard tras 6s
    html += "<title>Guardando...</title>";
    html += "<style>body{background:#121212;color:#fff;font-family:sans-serif;";
    html += "text-align:center;padding-top:15%;}h1{color:#28a745;}</style>";
    html += "</head><body>";
    html += "<h1>&#128190; Configuracion guardada</h1>";
    html += "<p>El dispositivo se esta reiniciando...</p>";
    html += "<p style='color:#888;font-size:13px;'>";
    html += "Seras redirigido al Dashboard en 6 segundos.</p>";
    html += "</body></html>";
    webServer.send(200, "text/html", html);

    delay(1500); // Tiempo para que el navegador reciba la respuesta
    ESP.restart();
}

// ====================================================================
// MOTOR DEL SERVIDOR WEB HTTP Y API
// ====================================================================
void handleWebRoot() {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<meta http-equiv='refresh' content='5'>"; 
    html += "<title>Dashboard - Proxy Modbus</title>";
    html += "<style>";
    html += "*{box-sizing:border-box;}";
    html += "body{background-color:#121212;color:#e0e0e0;font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;margin:0;padding:0;}";
    html += ".container{max-width:860px;margin:20px auto;background-color:#1e1e1e;padding:20px;border-radius:10px;box-shadow:0 4px 6px rgba(0,0,0,0.3);}";
    html += "h1{color:#4da6ff;border-bottom:1px solid #333;padding-bottom:10px;margin-top:0;}";
    html += "table{width:100%;border-collapse:collapse;margin-top:20px;margin-bottom:30px;}";
    html += "th,td{border:1px solid #333;padding:12px;text-align:center;}";
    html += "th{background-color:#2d2d2d;color:#4da6ff;}";
    html += "tr:nth-child(even){background-color:#1a1a1a;}";
    html += ".status{font-weight:bold;padding:5px 10px;border-radius:5px;}";
    html += ".btn-danger{display:inline-block;background-color:#dc3545;color:white;padding:10px 20px;text-decoration:none;border-radius:5px;font-weight:bold;border:none;cursor:pointer;}";
    html += ".btn-danger:hover{background-color:#c82333;}";
    html += "code{background-color:#111;padding:4px 8px;border-radius:3px;font-family:monospace;font-size:14px;display:inline-block;word-break:break-all;}";
    html += "</style></head><body>";
    html += buildNavBar("dashboard");
    
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
    else if (currentBackendState == BK_SHUTDOWN) { stateStr = "APAGADO"; stateColor = "#dc3545"; } 

    int activeSockets = getActiveClientCount();
    int totalTracked = 0;
    for (int i = 0; i < MAX_TRACKED_IPS; i++) if (trackedClients[i].isUsed) totalTracked++;

    html += "<h3>Estado del Servidor</h3>";
    html += "<p>Túnel hacia EMMA: <span class='status' style='background-color: " + stateColor + "; color: #fff;'>" + stateStr + "</span></p>";
    
    bool ocultarModelWeb = (currentBackendState == BK_PAUSED || currentBackendState == BK_PING_ERR || currentBackendState == BK_STARTUP_PING || currentBackendState == BK_SHUTDOWN);
    String displayModelWeb = ocultarModelWeb ? "" : emmaDeviceModel;
    html += "<p>Dispositivo Identificado: <strong>" + displayModelWeb + "</strong></p>"; 
    html += "<p>Conexiones TCP Activas: <strong>" + String(activeSockets) + " / " + String(MAX_CLIENTS) + "</strong></p>";

    if (checkEmmaStartup && currentBackendState != BK_SHUTDOWN) {
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
    
    if (currentBackendState != BK_SHUTDOWN) {
        html += "<a href='/apagar' class='btn-danger'>🛑 Apagar Proxy de Forma Segura</a>";
    } else {
        html += "<p style='color: #dc3545; font-weight: bold;'>Dispositivo en modo pasivo seguro. Listo para desconectar o actualizar por red.</p>";
    }
    html += "</div></body></html>";
    
    webServer.send(200, "text/html", html);
}

void handleWebShutdown() {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Apagando - Proxy Modbus</title>";
    html += "<style>*{box-sizing:border-box;}body{background-color:#121212;color:#fff;";
    html += "font-family:sans-serif;margin:0;padding:0;}";
    html += ".container{max-width:600px;margin:40px auto;text-align:center;";
    html += "background:#1e1e1e;padding:30px;border-radius:10px;}</style>";
    html += "</head><body>";
    html += buildNavBar("dashboard");
    html += "<div class='container'>";
    html += "<h1 style='color:#dc3545;'>APAGADO INICIADO</h1>";
    html += "<p>El puerto TCP ha sido liberado en la EMMA.</p>";
    html += "<p>El Dashboard Web y el servicio OTA continuarán operativos de fondo.</p>";
    html += "</div></body></html>";
    
    webServer.send(200, "text/html", html);
    pendingShutdown = true; 
}

void handleApiStatus() {
    String stateStr = "WAITING";
    if (currentBackendState == BK_STANDBY) stateStr = "STANDBY";
    else if (currentBackendState == BK_CONNECTED) stateStr = "CONNECTED";
    else if (currentBackendState == BK_CON_ERR) stateStr = "CON-ERR";
    else if (currentBackendState == BK_STARTUP_PING) stateStr = "PINGING";
    else if (currentBackendState == BK_PING_ERR) stateStr = "PING ERROR";
    else if (currentBackendState == BK_PAUSED) stateStr = "PAUSED";
    else if (currentBackendState == BK_SHUTDOWN) stateStr = "APAGADO"; 

    int activeSockets = getActiveClientCount();
    uint32_t currentUptime = millis() / 1000;

    String json = "{";
    json += "\"uptime_seconds\":" + String(currentUptime) + ",";
    json += "\"backend_state\":\"" + stateStr + "\",";
    json += "\"device_model\":\"" + emmaDeviceModel + "\",";
    json += "\"active_clients\":" + String(activeSockets) + ",";
    json += "\"max_clients\":" + String(MAX_CLIENTS) + ",";
    json += "\"debug_attempts\":" + String(emmaDebugAttempts) + ",";
    json += "\"debug_stage\":\"" + emmaDebugStage + "\",";
    json += "\"debug_error\":\"" + emmaDebugError + "\"";
    json += "}";

    webServer.send(200, "application/json", json);
}

// ====================================================================
// PÁGINA WEB: LOG DE TRANSACCIONES MODBUS
// ====================================================================
void handleWebLog() {
    String html = "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<meta http-equiv='refresh' content='30'>";
    html += "<title>Log Modbus - Proxy</title>";
    html += "<style>";
    html += "*{box-sizing:border-box;}";
    html += "body{background:#121212;color:#e0e0e0;font-family:'Segoe UI',sans-serif;margin:0;padding:0;}";
    html += ".wrap{max-width:1400px;margin:20px auto;padding:0 12px;}";
    html += "h1{color:#4da6ff;border-bottom:1px solid #333;padding-bottom:8px;margin-top:0;}";
    html += ".toolbar{display:flex;align-items:center;gap:12px;margin-bottom:12px;flex-wrap:wrap;}";
    html += ".btn{display:inline-block;padding:7px 16px;border-radius:5px;text-decoration:none;";
    html += "font-size:13px;font-weight:600;cursor:pointer;border:none;}";
    html += ".btn-blue{background:#4da6ff;color:#000;}.btn-blue:hover{background:#3390ee;}";
    html += ".badge-ok{background:#155724;color:#a3e8b0;padding:3px 8px;border-radius:4px;";
    html += "font-size:12px;font-weight:700;}";
    html += ".badge-err{background:#4a1010;color:#f5b8b8;padding:3px 8px;border-radius:4px;";
    html += "font-size:12px;font-weight:700;}";
    html += ".badge-warn{background:#3d2f00;color:#ffe082;padding:3px 8px;border-radius:4px;";
    html += "font-size:12px;font-weight:700;}";
    html += "table{width:100%;border-collapse:collapse;font-size:12px;}";
    html += "th,td{border:1px solid #2a2a2a;padding:6px 8px;text-align:left;vertical-align:top;}";
    html += "th{background:#1a1a2e;color:#4da6ff;white-space:nowrap;position:sticky;top:50px;z-index:10;}";
    html += "tr:nth-child(even){background:#1a1a1a;}tr:hover{background:#1e2a3a;}";
    html += ".mono{font-family:monospace;font-size:11px;word-break:break-all;color:#a0ffa0;}";
    html += ".fc{font-family:monospace;font-weight:700;color:#ffd700;}";
    html += ".info{color:#888;font-size:13px;}";
    html += "</style></head><body>";
    html += buildNavBar("log");
    html += "<div class='wrap'>";
    html += "<h1>Log de Transacciones Modbus</h1>";

    // Toolbar
    html += "<div class='toolbar'>";
    if (xSemaphoreTake(txLogMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        html += "<span class='info'>Entradas: <strong>" + String(txLogCount) + " / " + String(MAX_TX_LOG) + "</strong></span>";
        xSemaphoreGive(txLogMutex);
    }
    html += "<a href='/log.csv' class='btn btn-blue'>&#11015; Exportar CSV</a>";
    html += "<span class='info' style='margin-left:auto;'>Auto-refresco: 30s</span>";
    html += "</div>";

    // Cabecera de tabla
    html += "<table><tr>";
    html += "<th>#</th><th>Tiempo(s)</th><th>Origen</th><th>Destino</th>";
    html += "<th>Unit ID</th><th>Función</th><th>Registro</th><th>Cant.</th>";
    html += "<th>Request</th><th>Response</th><th>Excepción</th>";
    html += "<th>Bytes Request (HEX)</th><th>Bytes Response (HEX)</th>";
    html += "</tr>";

    if (xSemaphoreTake(txLogMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (txLogCount == 0) {
            html += "<tr><td colspan='13' style='text-align:center;color:#666;padding:20px;'>";
            html += "Sin transacciones registradas todavía.</td></tr>";
        } else {
            // Iterar en orden cronológico inverso (más reciente primero)
            for (int n = 0; n < txLogCount; n++) {
                int idx = ((txLogHead - 1 - n) % MAX_TX_LOG + MAX_TX_LOG) % MAX_TX_LOG;
                const ModbusTransaction &t = txLog[idx];

                // Nombre legible de la función Modbus
                String funcStr;
                switch (t.funcCode) {
                    case 0x01: funcStr = "01 ReadCoils";   break;
                    case 0x02: funcStr = "02 ReadDI";      break;
                    case 0x03: funcStr = "03 ReadHR";      break;
                    case 0x04: funcStr = "04 ReadIR";      break;
                    case 0x05: funcStr = "05 WriteCoil";   break;
                    case 0x06: funcStr = "06 WriteReg";    break;
                    case 0x0F: funcStr = "15 WriteCoils";  break;
                    case 0x10: funcStr = "16 WriteRegs";   break;
                    default: {
                        char tmp[8];
                        snprintf(tmp, sizeof(tmp), "0x%02X", t.funcCode);
                        funcStr = String(tmp);
                    }
                }

                // Badges de estado
                String reqBadge = t.requestOk
                    ? "<span class='badge-ok'>OK</span>"
                    : "<span class='badge-err'>MALFORMED</span>";

                String resBadge;
                if (!t.responseOk) {
                    resBadge = "<span class='badge-err'>NO RESP</span>";
                } else if (t.errorCode != 0) {
                    char tmp[20];
                    snprintf(tmp, sizeof(tmp), "EXC 0x%02X", t.errorCode);
                    resBadge = "<span class='badge-warn'>" + String(tmp) + "</span>";
                } else {
                    resBadge = "<span class='badge-ok'>OK</span>";
                }

                String excStr;
                if (t.errorCode != 0) {
                    char tmp[8];
                    snprintf(tmp, sizeof(tmp), "0x%02X", t.errorCode);
                    excStr = "<span style='color:#f5b8b8;font-family:monospace;'>" + String(tmp) + "</span>";
                } else {
                    excStr = "<span style='color:#555;'>-</span>";
                }

                html += "<tr>";
                html += "<td style='color:#555;'>" + String(txLogCount - n) + "</td>";
                html += "<td>" + String(t.timestamp) + "s</td>";
                html += "<td>" + String(t.clientIP) + "</td>";
                html += "<td>" + String(t.destIP) + ":502</td>";
                html += "<td style='text-align:center;'>" + String(t.unitID) + "</td>";
                html += "<td class='fc'>" + funcStr + "</td>";
                html += "<td style='text-align:center;font-family:monospace;'>" + String(t.regAddress) + "</td>";
                html += "<td style='text-align:center;'>" + String(t.regCount) + "</td>";
                html += "<td style='text-align:center;'>" + reqBadge + "</td>";
                html += "<td style='text-align:center;'>" + resBadge + "</td>";
                html += "<td style='text-align:center;'>" + excStr + "</td>";
                html += "<td class='mono'>" + String(t.reqHex) + "</td>";
                html += "<td class='mono'>" + String(t.resHex) + "</td>";
                html += "</tr>";
            }
        }
        xSemaphoreGive(txLogMutex);
    } else {
        html += "<tr><td colspan='13' style='text-align:center;color:#f5b8b8;'>";
        html += "Error al acceder al log (mutex ocupado).</td></tr>";
    }

    html += "</table></div></body></html>";
    webServer.send(200, "text/html", html);
}

// Exportación del log en formato CSV
void handleWebLogCsv() {
    String csv = "#,Tiempo_s,Origen,Destino,UnitID,FuncCode,Registro,Cantidad,";
    csv += "RequestOK,ResponseOK,ExcCode,BytesRequest,BytesResponse\r\n";

    if (xSemaphoreTake(txLogMutex, pdMS_TO_TICKS(300)) == pdTRUE) {
        for (int n = 0; n < txLogCount; n++) {
            int idx = ((txLogHead - 1 - n) % MAX_TX_LOG + MAX_TX_LOG) % MAX_TX_LOG;
            const ModbusTransaction &t = txLog[idx];

            char fcHex[8];
            snprintf(fcHex, sizeof(fcHex), "0x%02X", t.funcCode);

            String resStatus;
            if (!t.responseOk) resStatus = "NO_RESP";
            else if (t.errorCode != 0) {
                char tmp[10]; snprintf(tmp, sizeof(tmp), "EXC_0x%02X", t.errorCode);
                resStatus = String(tmp);
            } else resStatus = "OK";

            char excHex[8];
            if (t.errorCode) snprintf(excHex, sizeof(excHex), "0x%02X", t.errorCode);
            else              snprintf(excHex, sizeof(excHex), "-");

            csv += String(txLogCount - n) + ",";
            csv += String(t.timestamp) + ",";
            csv += String(t.clientIP) + ",";
            csv += String(t.destIP)   + ":502,";
            csv += String(t.unitID)   + ",";
            csv += String(fcHex)      + ",";
            csv += String(t.regAddress) + ",";
            csv += String(t.regCount)   + ",";
            csv += (t.requestOk ? "OK" : "MALFORMED") + String(",");
            csv += resStatus + ",";
            csv += String(excHex) + ",";
            csv += "\"" + String(t.reqHex) + "\",";
            csv += "\"" + String(t.resHex) + "\"";
            csv += "\r\n";
        }
        xSemaphoreGive(txLogMutex);
    }

    webServer.sendHeader("Content-Disposition", "attachment; filename=modbus_log.csv");
    webServer.send(200, "text/csv", csv);
}

// ====================================================================
// FUNCIONES DE BAJO NIVEL DE RED
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
        if (currentBackendState == BK_SHUTDOWN) {
            delay(100);
            continue;
        }

        if (otaInProgress) {
            delay(100);
            continue; 
        }

        if (isPaused) {
            if (backendClient.connected()) backendClient.stop();
            if (proxyServer.hasClient()) proxyServer.available().stop();
            currentBackendState = BK_PAUSED;
            delay(100);
            continue; 
        }

        bool tieneNetActiva = false;
        if (cfg.useEthernet) {
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
                    
                    if (backendClient.connect(targetModbusIP, cfg.modbusPort)) {
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

                                            // --- CIRUGÍA ESTÉTICA: Cortar en el primer espacio ---
                                            int posEspacio = cleanStr.indexOf(' '); 
                                            if (posEspacio > 0) {
                                                cleanStr = cleanStr.substring(0, posEspacio); // Corta el texto
                                            }
                                            // -----------------------------------------------------

                                            cleanStr.trim(); 

                                            if (cleanStr.length() > 0) {
                                                emmaDeviceModel = cleanStr;  
                                            } else {
                                                emmaDeviceModel = "UNKNOWN";
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

        // 2. Enrutador Modbus - Anti-fragmentación bidireccional + Log de transacciones
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i] && clients[i].connected()) {
                if (clients[i].available() >= 7) { 

                    // ---- REQUEST: leer MBAP completo (7 bytes) ----
                    uint8_t mbap[7];
                    bool reqMbapOk = readExact(clients[i], mbap, 7, 500);

                    // Preparar entrada de log para esta transacción
                    ModbusTransaction txEntry;
                    memset(&txEntry, 0, sizeof(txEntry));
                    txEntry.timestamp  = millis() / 1000;
                    txEntry.requestOk  = false;
                    txEntry.responseOk = false;
                    txEntry.errorCode  = 0;
                    strncpy(txEntry.clientIP, clients[i].remoteIP().toString().c_str(), 15);
                    strncpy(txEntry.destIP,   targetModbusIP.toString().c_str(), 15);

                    if (reqMbapOk) {
                        uint16_t remainingLength = (mbap[4] << 8) | mbap[5];
                        bool reqLenOk = (remainingLength > 0 && remainingLength < 260);

                        if (reqLenOk) {
                            uint16_t pduLen = remainingLength - 1;
                            uint8_t* pdu    = new uint8_t[pduLen];

                            // ---- REQUEST: leer PDU completa (anti-fragmentación) ----
                            if (readExact(clients[i], pdu, pduLen, 500)) {
                                // Extraer metadatos Modbus de la request
                                txEntry.requestOk  = true;
                                txEntry.unitID     = mbap[6];
                                txEntry.funcCode   = (pduLen >= 1) ? pdu[0] : 0;
                                txEntry.regAddress = (pduLen >= 3) ? ((uint16_t)(pdu[1] << 8) | pdu[2]) : 0;
                                txEntry.regCount   = (pduLen >= 5) ? ((uint16_t)(pdu[3] << 8) | pdu[4]) : 0;

                                // Ensamblar trama request completa (atómica)
                                uint16_t totalReqLen = 7 + pduLen;
                                uint8_t* totalReq    = new uint8_t[totalReqLen];
                                memcpy(totalReq,     mbap, 7);
                                memcpy(totalReq + 7, pdu,  pduLen);
                                bytesToHexStr(totalReq, totalReqLen, txEntry.reqHex, sizeof(txEntry.reqHex));

                                updateClientStats(clients[i].remoteIP());
                                
                                if (xSemaphoreTake(backendMutex, pdMS_TO_TICKS(2500)) == pdTRUE) {
                                    // ---- RECONEXION AL BACKEND si es necesario ----
                                    if (!backendClient.connected()) {
                                        if (millis() - lastBackendConnectAttempt >= RECONNECT_DELAY
                                            || lastBackendConnectAttempt == 0) {
                                            lastBackendConnectAttempt = millis();
                                            if (backendClient.connect(targetModbusIP, cfg.modbusPort)) {
                                                currentBackendState = BK_CONNECTED;
                                            } else {
                                                currentBackendState = BK_CON_ERR; 
                                            }
                                        }
                                    }

                                    if (backendClient.connected()) {
                                        // ---- ENVIO AL BACKEND: trama atómica completa ----
                                        backendClient.write(totalReq, totalReqLen);
                                        
                                        // ---- RESPUESTA BACKEND: leer MBAP (7 bytes) ----
                                        uint8_t resMbap[7];
                                        if (readExact(backendClient, resMbap, 7, 1000)) {
                                            uint16_t resRemLen = (resMbap[4] << 8) | resMbap[5];

                                            // Validar longitud MBAP respuesta
                                            if (resRemLen > 0 && resRemLen < 260) {
                                                uint16_t resPduLen = resRemLen - 1;
                                                uint8_t* resPdu    = new uint8_t[resPduLen];

                                                // ---- RESPUESTA BACKEND: leer PDU completa (anti-fragmentación) ----
                                                if (readExact(backendClient, resPdu, resPduLen, 1000)) {
                                                    txEntry.responseOk = true;

                                                    // Detectar excepción Modbus (bit 7 del funcCode activo)
                                                    if (resPduLen >= 2 && (resPdu[0] & 0x80)) {
                                                        txEntry.errorCode = resPdu[1];
                                                    }

                                                    // Ensamblar trama respuesta completa (atómica)
                                                    uint16_t totalResLen = 7 + resPduLen;
                                                    uint8_t* totalRes    = new uint8_t[totalResLen];
                                                    memcpy(totalRes,     resMbap, 7);
                                                    memcpy(totalRes + 7, resPdu,  resPduLen);
                                                    bytesToHexStr(totalRes, totalResLen, txEntry.resHex, sizeof(txEntry.resHex));

                                                    // ---- REENVIO AL CLIENTE: trama atómica completa ----
                                                    clients[i].write(totalRes, totalResLen);
                                                    delete[] totalRes;
                                                } else {
                                                    // Timeout leyendo PDU de respuesta del backend
                                                    txEntry.responseOk = false;
                                                    backendClient.stop();
                                                    currentBackendState = BK_STANDBY;
                                                }
                                                delete[] resPdu;
                                            } else {
                                                // MBAP de respuesta inválido
                                                txEntry.responseOk = false;
                                                backendClient.stop();
                                                currentBackendState = BK_STANDBY;
                                            }
                                        } else {
                                            // Timeout esperando MBAP de respuesta del backend
                                            txEntry.responseOk = false;
                                            backendClient.stop();
                                            currentBackendState = BK_STANDBY;
                                        }
                                    }
                                    xSemaphoreGive(backendMutex); 
                                }
                                delete[] totalReq;
                            } else {
                                // No se pudo leer la PDU completa de la request
                                txEntry.requestOk = false;
                                bytesToHexStr(mbap, 7, txEntry.reqHex, sizeof(txEntry.reqHex));
                            }
                            delete[] pdu;
                        } else {
                            // Longitud MBAP inválida
                            txEntry.requestOk = false;
                            bytesToHexStr(mbap, 7, txEntry.reqHex, sizeof(txEntry.reqHex));
                        }
                    } else {
                        // No se pudo leer el MBAP de la request
                        txEntry.requestOk = false;
                    }

                    // Registrar transacción en el log circular
                    logTransaction(txEntry);
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
            if (menosPressed) currentMenuOption = (currentMenuOption + 1) % 7;
            if (masPressed) currentMenuOption = (currentMenuOption - 1 + 7) % 7;
            if (backPressed) currentMenuState = MENU_IDLE;
            if (okPressed) {
                if (currentMenuOption == 0) { currentMenuState = MENU_CLIENTS_LIST; selectedClientIdx = 0; } 
                else if (currentMenuOption == 1) { currentMenuState = MENU_TEST_PING; diagnosticResult = "OK:Iniciar Test"; } 
                else if (currentMenuOption == 2) { currentMenuState = MENU_TEST_FIXED; diagnosticResult = "OK p/ ID " + String(MODBUS_FIXED_ID); } 
                else if (currentMenuOption == 3) { currentMenuState = MENU_TEST_SCAN; diagnosticResult = "OK:Iniciar Scan"; }
                else if (currentMenuOption == 4) { isPaused = !isPaused; } 
                else if (currentMenuOption == 5) { currentMenuState = MENU_SHUTDOWN; diagnosticResult = "OK:Confirmar"; }
                else if (currentMenuOption == 6) { currentMenuState = MENU_ABOUT; } 
            } break;
        case MENU_CLIENTS_LIST:
            if (totalTracked > 0) {
                if (menosPressed) selectedClientIdx = (selectedClientIdx + 1) % totalTracked;
                if (masPressed) selectedClientIdx = (selectedClientIdx - 1 + totalTracked) % totalTracked;
            }
            if (backPressed) currentMenuState = MENU_MAIN;
            if (okPressed && totalTracked > 0) currentMenuState = MENU_CLIENTS_DETAIL; break;
        case MENU_CLIENTS_DETAIL:
        case MENU_ABOUT: 
            if (backPressed || okPressed) currentMenuState = MENU_MAIN; break;
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
    
    if (cfg.useEthernet) {
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
                "6. Apagar Proxy",
                "7. About"
            };
            
            int startIdx = currentMenuOption - 2;
            if (startIdx < 0) startIdx = 0;
            if (startIdx > 3) startIdx = 3; 
            
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
            
        // ====================================================================
        // REVISIÓN SUSTANCIAL: INTERFAZ DE CRÉDITOS 100% CENTRADA POR PÍXEL
        // ====================================================================
        case MENU_ABOUT: {
            // Centrar título "ABOUT" (5 letras * 6px = 30px -> X = (128-30)/2 = 49)
            display.setCursor(49, 0); display.println("ABOUT");
            
            // Centrar etiqueta "Firmware version:" (17 letras * 6px = 102px -> X = (128-102)/2 = 13)
            display.setCursor(13, 12); display.println("Firmware version:");
            
            // Centrar variable de versión dinámicamente según su número de dígitos
            int versionX = (128 - (FIRMWARE_VERSION.length() * 6)) / 2;
            display.setCursor(versionX, 22); display.println(FIRMWARE_VERSION); 
            
            // Centrar etiqueta "By" (2 letras * 6px = 12px -> X = (128-12)/2 = 58)
            display.setCursor(58, 36); display.println("By");     
            
            // Centrar firma "Guybrush Threepwood" (19 letras * 6px = 114px -> X = (128-114)/2 = 7)
            display.setCursor(7, 46); display.println("Guybrush Threepwood");
            
            printBottom("BACK:Atras", "");
            break;
        }

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
        if (backendClient.connect(targetModbusIP, cfg.modbusPort)) {
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
    uint8_t idsAProbar[] = {0, 1, 2, 3, 6, 16, 100, 255}; 
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
            
            if (backendClient.connect(targetModbusIP, cfg.modbusPort)) {
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
    currentBackendState = BK_SHUTDOWN; 
    renderUI();
    
    if (xSemaphoreTake(backendMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        if (backendClient.connected()) {
            backendClient.stop(); 
        }
        xSemaphoreGive(backendMutex);
    }
    
    proxyServer.end(); 
    
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i].connected()) {
            clients[i].stop();
        }
    }
    
    delay(500); 
    
    display.clearDisplay();
    
    display.setTextSize(2); 
    display.setTextColor(SH110X_WHITE);
    display.setCursor(18, 0);
    display.println("APAGADO");
    
    display.setTextSize(1);
    display.setCursor(12, 18);
    display.println("Safe to disconnect");
    
    display.drawLine(10, 28, 118, 28, SH110X_WHITE);
    
    display.setCursor(10, 33);
    display.println("ModbusTCP: DISABLED");
    display.setCursor(10, 43);
    display.println("Dashboard: ENABLED");
    display.setCursor(10, 53);
    display.println("OTA:       ENABLED");
    
    display.display();
    
    while(true) {
        ArduinoOTA.handle();      
        webServer.handleClient(); 
        delay(10); 
    }
}