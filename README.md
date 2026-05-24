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
| Navegador / API    | ----+     +-------------------+      |  [ID 00]: SmartHEMS     |
| (Web y JSON Status)|     |               |                |  [ID 06]: SUN2000 (10kW)|
+--------------------+     |       [Pantalla OLED]          +-------------------------+
                           |       Métricas y Tests
+--------------------+     |
| Pantalla OLED Local| ----+
|  (Menú Diagnóstico)|
+--------------------+

```

---

## 🚀 Historial de Versiones y Novedades

### 🟢 Versión 3.0 (Actual)

* **API REST JSON:** Implementación de un endpoint dedicado (`http://<IP_ESP32>/api/status`) que devuelve la telemetría del proxy, contadores de clientes y diagnóstico en formato JSON puro, ideal para integraciones externas y monitorización remota.
* **Integridad Atómica Estricta:** El protocolo de primado inicial ha sido refinado para pedir bloques exactos de 15 registros al consultar el modelo de la EMMA, evitando excepciones `0x03` y respetando al milímetro las reglas de memoria de Huawei.

### 🔵 Versión 2.0

* **Monitorización Web HTTP:** Inclusión de un dashboard accesible vía navegador en el puerto 80 con estadísticas de estado en tiempo real, registro de IPs clientes, y depuración Modbus.
* **Reensamblador Anti-Fragmentación TCP:** Un motor robusto que detecta paquetes TCP divididos o mutilados por Home Assistant, los junta en el búfer local y los envía a la EMMA de forma limpia y continua.
* **Escudo de Cuarentena Inteligente:** Antes de abrir la pasarela a los clientes, el ESP32 realiza un **Ping ICMP** seguido de un test de lectura Modbus. Si la EMMA está reiniciándose o no hay red, el proxy bloquea a los clientes (Cuarentena) protegiendo a Huawei de recibir un ataque de denegación de servicio (DDoS) involuntario.

---

## ✨ Características Principales

* **Multiplexación Concurrente Segura:** Permite conectar hasta 4 clientes Modbus TCP simultáneos (ej: Home Assistant y scripts de analítica locales corriendo al mismo milisegundo) gestionando las peticiones en cola mediante semáforos mutex (`SemaphoreHandle_t`).
* **Arquitectura Asíncrona Dual-Core:** El motor del proxy Modbus corre de forma aislada en el **Core 0** de FreeRTOS para garantizar latencias mínimas y evitar cortes en la red, mientras que la interfaz gráfica (OLED) y los botones corren de forma independiente en el **Core 1**.
* **Conexiones Persistentes Inteligentes ("Lazy Connections"):** El ESP32 no bombardea a la EMMA por iniciativa propia. Solo abre el canal hacia Huawei bajo demanda cuando recibe una petición entrante legítima. Si hay tráfico continuo (como el de Home Assistant), mantiene el túnel abierto de forma ultra-eficiente.

---

## 💻 Interfaces de Control y Monitorización

### 1. Endpoint API JSON (`/api/status`)

Se puede consultar el estado interno del proxy mediante una llamada GET que devuelve un objeto estructurado para herramientas automatizadas:

```json
{
  "uptime_seconds": 1450,
  "backend_state": "WAITING",
  "device_model": "SmartHEMS",
  "active_clients": 1,
  "max_clients": 4,
  "debug_attempts": 1,
  "debug_stage": "Completado. Pasarela liberada para Home Assistant.",
  "debug_error": "Ninguno (¡Éxito absoluto!)"
}

```

### 2. Panel Web (Dashboard HTTP)

Accediendo a la IP del proxy desde cualquier navegador (`http://<IP_ESP32>/`), se dispone de una vista en vivo con auto-refresco que muestra las IPs rastreadas, el tiempo activo, la depuración hexadecimal Modbus del arranque y un botón de apagado seguro.

### 3. Pantalla Principal y Menú OLED (Local)

La pantalla refleja en tiempo real el pulso de la instalación (`Server: CONECTADO`, clientes activos). Pulsando el botón **OK**, se accede al menú físico de herramientas forenses:

1. **Historial IPs:** Muestra las últimas IPs conectadas y tiempo transcurrido.
2. **Test Ping Red:** Lanza un ping ICMP para verificar la conexión física.
3. **Modbus Fijo:** Inyecta una petición directa para testear respuestas de software de la EMMA.
4. **Escaner Auto-HA:** Imita la fuerza bruta de Home Assistant para detectar qué IDs responden, de forma controlada y pausada.

---

## 🕵️ Hallazgos de Ingeniería Inversa en el Ecosistema Huawei

Durante el desarrollo y la auditoría de tráfico con este proxy, se descubrió la topología exacta del bus interno RS485 gestionado por la EMMA:

* **ID 00 (SmartHEMS / EMMA):** Responde en el registro de texto `30000` devolviendo la cadena de caracteres `SmartHEMS`. Exige una longitud estricta de 15 registros. No cuenta con mapa de registros de inversor.
* **ID 06 (Inversor SUN2000-10K-LC0):** La EMMA desplaza automáticamente al inversor principal a la dirección física **ID 6**. Responde con éxito al registro de texto `30000` (`SUN2000-10K-LC0`) y al registro universal de identificación `30070`.
* **Cuentas Vacías (IDs 1, 2, 3, etc.):** Si se intenta consultar un ID no asignado, la EMMA actúa como pasarela e intercepta la trama devolviendo un código de error Modbus oficial de Excepción **`0x83` con código `04` (Slave Device Failure)**.
* **Corrección de Longitud PDU (Bug Off-By-One):** El firmware corrige un comportamiento del estándar Modbus TCP de Huawei donde la longitud restante devuelta en la cabecera MBAP incluye el Unit ID. El código resta dicho byte (`rLen - 1`) para evitar que el búfer TCP se congele.

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

Declaración automática mediante objetos nativos:

```cpp
#define WIFI_SSID "Tu_Nombre_De_WiFi"
#define WIFI_PASSWORD "Tu_Clave_De_WiFi"
#define MODBUS_SERVER_IP "<IP_EMMA>" // IP Real de la EMMA de Huawei
#define MODBUS_SERVER_PORT 502

```

### 3. Configuración en Home Assistant

Para conectar Home Assistant al ecosistema, se debe apuntar la integración oficial directamente hacia este proxy:

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
| **Botón MENOS (-)** | GPIO 39 | Entrada digital pura |

---

*Desarrollado y depurado de forma quirúrgica para la estabilidad energética local.*
