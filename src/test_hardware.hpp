#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <ETH.h>
#include <WiFi.h>
#include <esp_mac.h> 

// ==========================================
// PINES DE HARDWARE
// ==========================================
#define BOTON_OK    4
#define BOTON_BACK  14
#define BOTON_MAS   15
#define BOTON_MENOS 39  

#define SDA_PIN     33
#define SCL_PIN     32

Adafruit_SH1106G display(128, 64, &Wire, -1);

String eth_mac = "Buscando...";
String wifi_mac = "Buscando...";
String bt_mac = "";
bool has_bluetooth = false;

// Función artesanal para dibujar el logo BT sin consumir memoria en mapas de bits
void drawBluetoothLogo(int x, int y, bool isWorking) {
    display.drawLine(x+4, y, x+4, y+12, SH110X_WHITE);
    display.drawLine(x+4, y, x+8, y+3, SH110X_WHITE);
    display.drawLine(x+8, y+3, x+2, y+9, SH110X_WHITE);
    display.drawLine(x+4, y+12, x+8, y+9, SH110X_WHITE);
    display.drawLine(x+8, y+9, x+2, y+3, SH110X_WHITE);

    if (!isWorking) {
        display.drawCircle(x+4, y+6, 8, SH110X_WHITE);
        display.drawLine(x-2, y-2, x+10, y+14, SH110X_WHITE); 
    }
}

void setup() {
    Serial.begin(115200);

    pinMode(BOTON_OK, INPUT_PULLUP);
    pinMode(BOTON_BACK, INPUT_PULLUP);
    pinMode(BOTON_MAS, INPUT_PULLUP);
    pinMode(BOTON_MENOS, INPUT); 

    Wire.begin(SDA_PIN, SCL_PIN);
    if (!display.begin(0x3C, true)) {
        Serial.println("Fallo al iniciar OLED");
        for (;;); 
    }
    
    display.clearDisplay(); 
    display.setTextColor(SH110X_WHITE); 
    display.setTextSize(1);
    display.setCursor(0, 20);
    display.println("Iniciando chips...");
    display.display();

    // 1. Iniciar Ethernet
    ETH.begin(1, 16, 23, 18, ETH_PHY_LAN8720, ETH_CLOCK_GPIO0_IN);
    eth_mac = ETH.macAddress();

    // 2. Iniciar WiFi
    WiFi.mode(WIFI_MODE_STA);
    wifi_mac = WiFi.macAddress();

    // 3. Comprobar Bluetooth a bajo nivel
    uint8_t macBT[6];
    if (esp_read_mac(macBT, ESP_MAC_BT) == ESP_OK) {
        has_bluetooth = true;
        char btMacStr[18];
        snprintf(btMacStr, sizeof(btMacStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 macBT[0], macBT[1], macBT[2], macBT[3], macBT[4], macBT[5]);
        bt_mac = String(btMacStr);
    }
}

void loop() {
    int ok_pulsado = (digitalRead(BOTON_OK) == LOW) ? 1 : 0;
    int bk_pulsado = (digitalRead(BOTON_BACK) == LOW) ? 1 : 0;
    int mas_pulsado = (digitalRead(BOTON_MAS) == LOW) ? 1 : 0;
    int men_pulsado = (digitalRead(BOTON_MENOS) == LOW) ? 1 : 0;

    display.clearDisplay();
    
    // Título limpio sin rayitas para no pisar el logo
    display.setCursor(0, 0);
    display.println("TEST HARDWARE");
    
    // Dibujar el logo Bluetooth arriba a la derecha
    drawBluetoothLogo(115, 0, has_bluetooth);
    
    // Impresión secuencial ultra-comprimida
    display.setCursor(0, 15);
    display.print("ETH:"); display.print(eth_mac);

    display.setCursor(0, 27);
    display.print("WAN:"); display.print(wifi_mac);
    
    display.setCursor(0, 39);
    display.print("BT:"); 
    if (has_bluetooth) display.print(bt_mac);
    else display.print("No Detectado");

    display.setCursor(0, 54);
    display.printf("OK:%d BK:%d +:%d -:%d", ok_pulsado, bk_pulsado, mas_pulsado, men_pulsado);
    
    display.display();
    delay(50);
}