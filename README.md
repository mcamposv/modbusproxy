```markdown
# 🔋 ESP32 Modbus TCP Proxy & Multiplexer for Huawei EMMA & SUN2000

Este repositorio contiene el firmware definitivo para un dispositivo basado en **ESP32** (compatible con conexión WiFi o Ethernet WT32-ETH01) que actúa como una **pasarela, proxy intermedio y multiplexor de conexiones Modbus TCP** de grado industrial. 

Está diseñado específicamente para solucionar la limitación crítica de los ecosistemas de energía **Huawei (SmartHEMS / EMMA / Inversores SUN2000)**, los cuales solo permiten **un único cliente concurrente** en su puerto `502`, bloqueando o baneando por DDoS a cualquier otro dispositivo secundario que intente leer métricas simultáneamente.

---

## 🗺️ Arquitectura de Red e Intermediación

El ESP32 se sitúa estratégicamente en la red local como un escudo y distribuidor de tráfico transparente ("On-Demand Proxy").

```text
+--------------------+ 
|   Home Assistant   | ----+
| (Integración Solar)|     |     +-------------------+      +-------------------------+
+--------------------+     |     |   ESP32 FIRMWARE  |      |   HUAWEI EMMA BACKEND   |
                           +---> |   Proxy Multiplex | ---> |      (IP: <IP_EMMA>)    |
+--------------------+     |     |    (Puerto 502)   |      |                         |
|   Scripts Locales  | ----+     +-------------------+      |  [ID 00]: SmartHEMS     |
|   (Python / PC)    |     |               |                |  [ID 06]: SUN2000 (10kW)|
+--------------------+     |       [Pantalla OLED]          +-------------------------+
                           |       Métricas y Tests
+--------------------+     |
| Pantalla OLED Local| ----+
|  (Menú Diagnóstico)|
+--------------------+

```

---

## ✨ Características Principales

* **Multiplexación Concurrente Segura:** Permite conectar hasta 4 clientes Modbus TCP simultáneos (ej: Home Assistant y scripts de analítica locales corriendo al mismo milisegundo) gestionando las peticiones en cola mediante semáforos mutex (`SemaphoreHandle_t`).
* **Arquitectura Asíncrona Dual-Core:** El motor del proxy Modbus corre de forma aislada en el **Core 0** de FreeRTOS para garantizar latencias mínimas y evitar cortes en la red, mientras que la interfaz gráfica (OLED) y los botones corren de forma independiente en el **Core 1**.
* **Conexiones Persistentes Inteligentes ("Lazy Connections"):** El ESP32 no bombardea a la EMMA por iniciativa propia. Solo abre el canal hacia Huawei bajo demanda cuando recibe una petición entrante legítima. Si hay tráfico continuo (como el de Home Assistant), mantiene el túnel abierto de forma ultra-eficiente. Si el cliente se apaga, el túnel se cierra limpiamente tras un timeout.
* **Inmunidad Anti-Baneo Integrada:** Incluye lógica de *cooldown* (`RECONNECT_DELAY = 5000`) que impide ataques accidentales de denegación de servicio (DDoS) contra el cortafuegos interno de Huawei si se pierde la señal o el equipo destino se reinicia.
* **Sistema de Diagnóstico Forense Integrado:** Menú navegable por hardware que permite aislar fallos de red en Capa 3 (ICMP Puro) y Capa 4/7 (Modbus Excepciones).

---

## 📺 Información en Pantalla y Estados (OLED SH1106)

El dispositivo cuenta con una pantalla OLED local de 128x64 gestionada por máquina de estados que refleja en tiempo real el pulso de la instalación.

### Pantalla Principal (HOME / IDLE)

Al arrancar, muestra el estado general del sistema:

* `=== PROXY MODBUS ===`
* `IP: <IP_ESP32>` -> Muestra la IP obtenida por DHCP o configurada estáticamente (matrícula del proxy).
* `Server: CONECTADO / OFFLINE` -> **CONECTADO** significa que existe un túnel TCP activo, físico y verificado (`backendClient.connected() == true`) con el puerto 502 de la EMMA. **OFFLINE** indica que el servidor Huawei ha cerrado la puerta por inactividad o la red está caída.
* `Clientes: X` -> Contador en tiempo real de sockets abiertos concurrentemente hacia el ESP32 (ej: `1` cuando Home Assistant está conectado).

---

## 🎛️ Menú de Diagnóstico y Herramientas Forenses

Pulsando el botón **OK**, se accede al menú principal para realizar pruebas de aislamiento de problemas independientes del flujo de Home Assistant:

1. **1. Historial IPs:** Registra una base de datos local con las últimas 10 IPs de clientes que han pedido datos al proxy, mostrando su contador total de peticiones y marcas de tiempo para cazar intrusos o saturaciones.
2. **2. Test Ping Red:** Realiza un **Ping ICMP Puro (Capa 3)** directo a la IP de la EMMA saltándose protocolos TCP. Ideal para verificar si el cable o la antena WiFi de Huawei están respondiendo físicamente.
3. **3. Modbus Fijo:** Lanza una petición Modbus quirúrgica estándar de lectura (`0x03`) al ID y registro configurados en el código, aislando si el problema es de software.
4. **4. Escaner Auto-HA:** Imita el algoritmo de fuerza bruta que usa la integración oficial de Home Assistant. Barre los IDs más habituales (`1, 2, 3, 0, 16, 100, 255`) a velocidad controlada y con apertura/cierre de sockets estrictos para **evitar baneos**, buscando qué dispositivo responde.

---

## 🕵️ Hallazgos de Ingeniería Inversa en el Ecosistema Huawei

Durante el desarrollo y la auditoría de tráfico con este proxy, se descubrió la topología exacta del bus interno RS485 gestionado por la EMMA:

* **ID 00 (SmartHEMS / EMMA):** Responde en el registro de texto `30000` devolviendo la cadena de caracteres `SmartHEMS`. No cuenta con mapa de registros de inversor (da excepción `0x03` si se le pide el código de familia).
* **ID 06 (Inversor SUN2000-10K-LC0):** La EMMA desplaza automáticamente al inversor principal a la dirección física **ID 6**. Responde con éxito al registro de texto `30000` (`SUN2000-10K-LC0`) y al registro universal de identificación `30070`, devolviendo el valor hexadecimal `0163` (que equivale al modelo decimal `355` en el manual de Huawei).
* **Cuentas Vacías (IDs 1, 2, 3, etc.):** Si se intenta consultar un ID no asignado, la EMMA actúa como pasarela e intercepta la trama devolviendo un código de error Modbus oficial de Excepción **`0x83` con código `04` (Slave Device Failure)**, confirmando que la EMMA escucha pero el cable hacia el esclavo está desierto.
* **Corrección de Longitud PDU (Bug Off-By-One):** El firmware corrige un comportamiento del estándar Modbus TCP de Huawei donde la longitud restante devuelta en la cabecera MBAP incluye el Unit ID. El código resta dicho byte (`rLen - 1`) para evitar que el búfer TCP se quede congelado esperando un "byte fantasma" inexistente, optimizando la respuesta a microsegundos.

---

## ⚙️ Configuración del Entorno y Despliegue

### 1. Dependencias en `platformio.ini`

Es estrictamente obligatorio incluir la librería `ESP32Ping` para habilitar el aislamiento ICMP en los menús de diagnóstico:

```ini
lib_deps =
  adafruit/Adafruit GFX Library @ ^1.11.9
  adafruit/Adafruit SH110X @ ^2.1.10
  ESP32Ping

```

### 2. Archivo de Credenciales (`include/secrets.h`)

El firmware implementa un parseo automático mediante objetos nativos de red, permitiendo declarar la IP en formato String humano de forma natural:

```cpp
#define WIFI_SSID "Tu_Nombre_De_WiFi"
#define WIFI_PASSWORD "Tu_Clave_De_WiFi"
#define MODBUS_SERVER_IP "<IP_EMMA>" // IP Real de la EMMA de Huawei
#define MODBUS_SERVER_PORT 502

```

### 3. Configuración en Home Assistant

Para conectar Home Assistant al ecosistema, se debe ignorar la IP de la EMMA y apuntar la integración oficial directamente hacia este proxy:

* **Host / IP:** `<IP_ESP32>` *(IP asignada a tu ESP32)*
* **Puerto:** `502`
* **ID de Esclavo (Slave ID):** `6` *(ID cazado del inversor SUN2000)*

---

## 🛠️ Mapeo de Hardware (Pines ESP32)

| Componente | Pin ESP32 | Función |
| --- | --- | --- |
| **OLED SDA** | GPIO 33 | Línea de datos I2C |
| **OLED SCL** | GPIO 32 | Línea de reloj I2C |
| **Botón OK** | GPIO 4 | Entrada con Pull-Up interno |
| **Botón BACK** | GPIO 14 | Entrada con Pull-Up interno |
| **Botón MÁS (+)** | GPIO 15 | Entrada con Pull-Up interno |
| **Botón MENOS (-)** | GPIO 34 | Entrada digital pura (Requiere Pull-Up físico) |

---

*Desarrollado y depurado de forma quirúrgica para la estabilidad energética local.*

```

```