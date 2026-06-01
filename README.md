# 🔋 ESP32 Modbus TCP Proxy & Multiplexer for Huawei EMMA & SUN2000 (v5.0.0)

Este repositorio contiene el firmware de grado industrial desarrollado para **ESP32** (optimizado para conexión por cable físico mediante la pila Ethernet nativa del chip LAN8720 en placas WT32-ETH01, o mediante su antena WiFi interna). El dispositivo actúa como un **escudo de red, proxy transparente bajo demanda (On-Demand) y multiplexor de canales Modbus TCP**.

Su propósito fundamental es solucionar de forma definitiva el problema crítico de bloqueo y baneo por DDoS en los ecosistemas residenciales e industriales de **Huawei (SmartGuard / EMMA / Inversores SUN2000)**. Estos equipos cuentan con un firmware estricto que solo tolera **un único cliente TCP concurrente en el puerto 502**, tirando la conexión o aplicando listas negras si Home Assistant, cargadores de vehículos eléctricos (ej: V2C) o sistemas de analítica locales intentan leer métricas de forma simultánea.

---

## 💡 Hardware mínimo — funciona sin pantalla ni botones

**Solo necesitas un ESP32 con WiFi** para poner en marcha el proxy. La pantalla OLED y los botones físicos son opcionales: añaden comodidad y diagnóstico local, pero el firmware es completamente funcional sin ellos.

| Componente | ¿Necesario? | Notas |
|---|:---:|---|
| ESP32 (cualquier variante con WiFi) | ✅ Sí | `esp32dev`, NodeMCU-32S, Wemos D1 Mini32, etc. |
| Conexión WiFi o Ethernet a tu red local | ✅ Sí | WiFi integrado en cualquier ESP32 |
| Pantalla OLED SH1106 (I2C) | ❌ Opcional | Menú de diagnóstico local |
| Botones físicos (OK, +, −, BACK) | ❌ Opcional | Solo necesarios si usas la pantalla |
| Placa WT32-ETH01 (Ethernet nativo) | ❌ Opcional | Recomendada para instalación permanente |

Sin pantalla ni botones, toda la gestión se realiza desde el navegador web (`http://<IP_ESP32>/`) y la API REST (`/api/status`). El Modo Setup (configuración inicial) también funciona en modo headless: el ESP32 levanta un punto de acceso WiFi al que te conectas desde el móvil o el ordenador.

> **Para empezar rápido:** flashea el firmware en cualquier ESP32 con WiFi, configura `secrets.h` con tu SSID y la IP de tu EMMA, y conéctate a la web del proxy. No hace falta soldar nada.

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
| Pantalla OLED Local| ----+   (opcional)
|  (Menú Diagnóstico)|
+--------------------+

```

---

## 🔧 Configuración local (antes de compilar)

Este proyecto usa **tres archivos locales gitignoreados** para mantener tus credenciales e IPs fuera del repositorio. Cada uno tiene un `.template` de ejemplo que debes copiar y rellenar con tus valores reales.

### 1. `src/secrets.h` — Credenciales del firmware

Necesario para compilar. Define los valores por defecto que se cargan en el primer arranque del ESP32 (antes de configurar desde la web).

```bash
cp src/secrets.h.template src/secrets.h
```

Edita `src/secrets.h`:

```cpp
#define DEFAULT_WIFI_SSID      "TU_SSID_WIFI"
#define DEFAULT_WIFI_PASSWORD  "TU_PASSWORD_WIFI"
#define DEFAULT_MODBUS_IP      "192.168.x.x"   // IP de tu EMMA o inversor
#define DEFAULT_MODBUS_PORT    502
#define DEFAULT_LOCAL_IP       "192.168.x.x"   // IP que tendrá el proxy en tu red
#define DEFAULT_GATEWAY        "192.168.x.1"   // Puerta de enlace de tu router
#define DEFAULT_SUBNET         "255.255.255.0"
#define DEFAULT_DNS            "192.168.x.1"
#define DEFAULT_OTA_PASSWORD   "tu_password_ota"
```

> Una vez que el dispositivo arranca y guarda la configuración desde la web, estos defaults dejan de tener efecto. Solo son necesarios para el primer flash.

---

### 2. `secrets.ini` — Contraseña OTA e IPs de upload (PlatformIO)

Necesario para subir firmware por OTA con los perfiles `PROD_ota` y `TEST_ota`.

```bash
cp secrets.ini.template secrets.ini
```

Edita `secrets.ini`:

```ini
[secrets]
ota_password = tu_password_ota   ; debe coincidir con DEFAULT_OTA_PASSWORD
prod_ip      = 192.168.x.x       ; IP del ESP32 en producción
test_ip      = 192.168.x.x       ; IP del ESP32 de pruebas
```

---

### 3. `scripts/local_config.py` — IPs para los scripts de diagnóstico

Opcional pero recomendado. Sin este archivo los scripts requieren el argumento `--host` en cada ejecución.

```bash
cp scripts/local_config.py.template scripts/local_config.py
```

Edita `scripts/local_config.py`:

```python
DEFAULT_HOST = "192.168.x.x"   # IP por defecto para escaner_huawei y test_emma

SERVIDORES = {
    "emulador":   "192.168.x.x",
    "proxy_test": "192.168.x.x",
    "produccion": "192.168.x.x",
    "emmareal":   "192.168.x.x",
}
```

---

### Resumen de archivos locales

| Archivo | Copia desde | Afecta a |
|---|---|---|
| `src/secrets.h` | `src/secrets.h.template` | Compilación del firmware |
| `secrets.ini` | `secrets.ini.template` | Upload OTA con PlatformIO |
| `scripts/local_config.py` | `scripts/local_config.py.template` | Scripts de diagnóstico Python |

---

## 🚀 Historial de Versiones y Novedades

### 🟢 Versión 4.0.0 (Actual)

* **Actualizaciones Inalámbricas Permanentes (OTA):** Inclusión de la pila `ArduinoOTA` en el puerto de red `3232`. Permite flashear nuevas versiones del código a través del cable Ethernet o WiFi de forma remota. Cuenta con una pantalla de carga dedicada en el OLED local que muestra la barra de progreso en tiempo real (`UPDATING (OTA)`) y verificación final de éxito (`SUCCESS!!`).
* **Seguridad Blindada (Anti-Git):** Sistema de doble autenticación inalámbrica protegido contra filtraciones en repositorios públicos. La contraseña viaja cifrada mediante inyección local en PlatformIO combinando un archivo `secrets.ini` (oculto en `.gitignore`) con las directivas de preprocesador de `secrets.h`.
* **Menú About Centrado por Píxel:** Nueva pantalla de créditos integrada en la interfaz física. Calcula dinámicamente la longitud de la cadena de caracteres en memoria para centrar de forma exacta la versión del firmware. Rinde homenaje al mítico pirata Guybrush Threepwood.
* **Apagado Pasivo con Servicios Vivos:** Rediseño total de la subrutina de desmantelamiento seguro. Al pulsar "Apagar Proxy", el núcleo detiene estrictamente toda la comunicación Modbus TCP (liberando el slot de Huawei de inmediato), pero **mantiene vivos los hilos de execution del Servidor Web, la API JSON y el oyente OTA**. El equipo entra en un modo de bajo consumo Modbus sin quedar incomunicado de la red.

### 🟡 Versión 3.2

* **API REST JSON:** Implementación de un endpoint dedicado (`http://<IP_ESP32>/api/status`) que devuelve la telemetría del proxy, contadores de clientes y diagnóstico en formato JSON puro para integraciones externas.
* **Integridad Atómica Estricta:** El protocolo de primado inicial fue refinado para pedir bloques exactos de 15 registros al consultar el modelo de la EMMA, evitando excepciones de datos y respetando las reglas de memoria de Huawei.

### 🔵 Versión 2.0

* **Monitorización Web HTTP:** Inclusión de un dashboard accesible vía navegador en el puerto 80 con estadísticas de estado en tiempo real, registro de IPs clientes y depuración Modbus.
* **Reensamblador Anti-Fragmentación TCP:** Un motor robusto que detecta paquetes TCP divididos o mutilados por Home Assistant, los junta en el búfer local y los envía a la EMMA de forma limpia y continua.
* **Escudo de Cuarentena Inteligente:** Antes de abrir la pasarela a los clientes, el ESP32 realiza un **Ping ICMP** seguido de un test de lectura Modbus. Si la EMMA está reiniciándose o no hay red, el proxy bloquea a los clientes (Cuarentena) protegiendo a Huawei de recibir un ataque de denegación de servicio (DDoS) involuntario.

---

## ⚙️ Opciones de Configuración del Código (src/proxy_operativo.hpp)

El comportamiento completo de la placa y la pila de red se administra mediante variables estáticas parametrizables situadas en la parte superior del archivo principal:

* `USE_ETHERNET` (`const bool`): `false` para usar la antena interna WiFi del ESP32. `true` para enrutar todo el tráfico por el cable físico usando la pila Ethernet nativa (ej: LAN8720).
* `USE_DHCP` (`const bool`): `true` para solicitar una IP dinámica al router de la vivienda. `false` para forzar la IP estática de rescate configurada en el firmware.
* `ROTATE_SCREEN` (`const bool`): `true` aplica un giro físico de 180 grados a la visualización del OLED SH1106. `false` mantiene la orientación estándar.
* `MODBUS_FIXED_ID` (`const uint8_t`): Identificador de Unidad (Unit ID) utilizado para el test de arranque y primado inicial (Por defecto `0`, correspondiente a la EMMA).
* `MODBUS_TEST_REG` (`const uint16_t`): Dirección del registro Modbus consultado durante el test de vida del arranque (`30000`, Model Name ASCII).
* `RECONNECT_DELAY` (`const uint32_t`): Tiempo de espera en milisegundos (`5000` ms) que aplica el proxy antes de reintentar una conexión contra el puerto 502 de Huawei si el socket se rompe, evitando baneos por reintentos infinitos en ráfaga (Anti-DDoS).
* `FIRMWARE_VERSION` (`const String`): Almacena la versión semántica del programa activo que se renderizará de forma centrada en el menú local.

---

## 🛡️ Lógica de Arranque y Chequeos de Red (Doble Peaje)

Para garantizar que Home Assistant o cualquier cliente secundario jamás provoquen un baneo en el cortafuegos de Huawei, el proxy implementa un **"Doble Peaje de Seguridad"** asíncrono gestionado por la máquina de estados del backend en el Core 0. Hasta que la placa no supera con éxito ambos peajes, el puerto público `502` del proxy permanece cerrado para el exterior:

### Peaje 1: El Test de Ping ICMP (Capa 3)

* Nada más obtener red local, el proxy entra en el estado `BK_STARTUP_PING`. Lanza ráfagas de pings ICMP directos a la IP configurada de Huawei (`MODBUS_SERVER_IP`).
* Si el host no responde (porque la EMMA se está reiniciándose o el enlace físico está caído), el sistema transiciona a `BK_PING_ERR`, bloquea el servidor local y muestra una cuenta atrás de **30 segundos** antes de volver a comprobarlo. Esto evita saturar de sockets TCP a un equipo que está apagado o incomunicado.

### Peaje 2: El Primado Atómico Modbus (Capa 4 / Capa 7)

* **La inyección quirúrgica:** Abre un único socket TCP hacia el puerto 502 de Huawei e inyecta la trama hexadecimal exacta pidiendo leer 15 registros desde la dirección 30000.
* **El porqué de los 15 registros (Integridad Atómica):** Huawei exige integridad atómica en la lectura de sus cadenas de texto (Strings). El nombre del modelo en el registro 30000 ocupa exactamente 15 registros (30 bytes). Si un cliente le pide una longitud menor (como 10 registros), el firmware de Huawei considera que se está fragmentando una variable indivisible y responde con un código de error de datos `0x03`. Al pedir la longitud exacta de 15, la comunicación se realiza de forma limpia.
* **Criterio de Vida Inteligente (Detector de Excepciones):** El proxy analiza el código de función devuelto. Si recibe un éxito (`0x03` o `0x04`), extrae los bytes del texto (ej: `SmartHEMS`) y libera la pasarela. Pero si el equipo responde con un código de excepción de error legítimo de Huawei (`0x83` o `0x84`), el proxy **da por superado el peaje igualmente**. Esto es debido a que un dispositivo que responde con una excepción Modbus oficial es un equipo activo, libre y cuyo cortafuegos está escuchando perfectamente.

Una vez superados ambos peajes, el estado cambia a `BK_WAITING` y se abren las compuertas para Home Assistant.

---

## 💻 Panel de Control Web (Dashboard HTTP)

El dispositivo levanta un servidor web en el puerto estándar `80` que se refresca automáticamente cada 5 segundos para ofrecer un entorno de monitorización desde cualquier navegador ingresando en `http://<IP_ESP32>/`. El dashboard incluye:

* **Estado del Servidor:** Muestra con códigos de colores semafóricos el estado real del túnel hacia la EMMA (`STANDBY` en naranja, `CONNECTED` en verde, `CON-ERR`/`PING ERROR` en rojo y `APAGADO` en rojo destacado en caso de desconexión segura pasiva).
* **Información de Hardware:** Refleja el string del dispositivo identificado (ej: `SmartHEMS`) y el recuento de conexiones TCP activas de clientes sobre el total permitido (`X / 4`).
* **Caja Naranja de Depuración del Primado Inicial:** Un bloque visual crítico que se muestra si la comprobación inicial está activa, detallando el número total de intentos de conexión, la fase de control de texto exacta, la última trama Hexadecimal enviada, la última trama Hexadecimal recibida del bus y el diagnóstico descriptivo del último error en caso de fallo.
* **Historial Dinámico de IPs Clientes:** Una tabla estructurada que registra las últimas 10 direcciones IP únicas que han atacado el proxy Modbus, sumando el total acumulado de peticiones por cliente y calculando de forma elástica cuántos segundos hace que enviaron su última trama.
* **Apagado Seguro:** Un botón rojo destacado que llama al endpoint `/apagar`, liberando inmediatamente el slot de la EMMA antes de desconectar físicamente el ESP32, manteniendo el servidor web y la OTA disponibles de fondo.

---

## 📊 Monitorización remota vía API REST JSON

Para permitir integraciones nativas con sistemas externos o crear sensores de diagnóstico dedicados en Home Assistant, el firmware expone un endpoint JSON optimizado de solo lectura en la ruta:
👉 `GET http://<IP_ESP32>/api/status`

La respuesta se genera concatenando buffers en memoria para eliminar el uso de librerías pesadas de terceros, garantizando una respuesta en microsegundos bajo la estructura estándar:

```json
{
  "uptime_seconds": 3245,
  "backend_state": "CONNECTED",
  "device_model": "SmartHEMS",
  "active_clients": 1,
  "max_clients": 4,
  "debug_attempts": 1,
  "debug_stage": "Completado. Pasarela liberada para Home Assistant.",
  "debug_error": "Ninguno (¡Éxito absoluto!)"
}

```

---

## 🎛 Manual del Menú de Hardware e Interfaz OLED (Local)

En su estado en reposo (`MENU_IDLE`), el OLED SH1106 muestra la IP local del proxy, el estado de conexión del backend, el nombre del modelo detectado y el contador de sockets (`X/4`). Al pulsar el botón **OK**, el dispositivo suspende el renderizado de reposo y entra en el menú de diagnóstico avanzado navegable con los botones **[+]**, **[-]** y **[BACK]**:

### 1. Historial IPs

* **Funcionalidad:** Muestra una lista secuencial de todas las direcciones IP de los clientes registrados que han enviado tramas al dispositivo.
* **Operación:** Al seleccionar una IP con el botón **OK**, desglosa la dirección IP, el número total de consultas acumuladas y el tiempo transcurrido en segundos desde que envió su última petición Modbus.

### 2. Test Ping Red

* **Funcionalidad:** Fuerza una auditoría instantánea en Capa 3 contra la EMMA ignorando la máquina de estados general.
* **Operación:** Lanza 3 paquetes de pings ICMP asíncronos. Si el host responde, imprime en pantalla `PING OK!` junto con el tiempo medio de respuesta en milisegundos (`Tiempo: X ms`). Si falla, imprime `PING FALLIDO! Host Inalcanzable`.

### 3. Modbus Fijo

* **Funcionalidad:** Realiza un test de inyección manual de comandos en Capa 7 hacia el dispositivo destino utilizando los parámetros por defecto de los peajes.
* **Operación:** Abre un socket manual, inyecta la trama de 15 registros al ID 0, registro 30000, e inspecciona la respuesta en pantalla. Muestra el texto ASCII limpio devuelto por la EMMA o intercepta el código de excepción en formato legible.

### 4. Escaner Auto-HA

* **Funcionalidad:** Modos de búsqueda forense automáticos para mapear el bus RS485/Modbus de forma desasistida.
* **Operación:** Emula el descubrimiento de integraciones de domótica. Recorre secuencialmente los IDs sospechosos (`0, 1, 2, 3, 6, 16, 100, 255`), incluyendo de manera estricta el **ID 00 (EMMA)** y el **ID 06 (Inversor SUN2000)**. Lanza una petición de lectura de 15 registros al bloque 30000. Para cada ID, abre y cierra el socket de manera estricta con un retraso deliberado para **burlar el cortafuegos de Huawei**. Si localiza un ID válido, detiene el bucle y muestra el resultado en el OLED (ej: *"ID: 6 Leido: SUN2000-10K-LC0"*).

### 5. Pausar / Reanudar Comms

* **Funcionalidad:** Interruptor de aislamiento rápido por software (Modo Mantenimiento).
* **Operación:** Al pulsar **OK** sobre esta opción, el flag global `isPaused` se invierte de estado. Cuando está pausado, el proxy cierra de inmediato el cliente del backend, tira todas las conexiones TCP de los clientes entrantes en el puerto 502 y congela el servicio Modbus, permitiendo al usuario realizar labores de mantenimiento sin que Home Assistant intente reconectar.

### 6. Apagar Proxy

* **Funcionalidad:** Procedimiento de desmantelamiento y aislamiento seguro de la electrónica.
* **Operación:** Al confirmar la selección con **OK**, el ESP32 ejecuta la rutina `ejecutarApagado()`. Desconecta ordenadamente el socket de Huawei y detiene la instancia del `proxyServer` Modbus para liberar los descriptores en red de los clientes activos. Borra la pantalla e imprime un desglose técnico estático:
* **ModbusTCP: DISABLED** (La EMMA queda 100% libre y protegida de tráfico).
* **Dashboard: ENABLED** (La interfaz web sigue sirviendo telemetría histórica).
* **OTA: ENABLED** (Permite sacarlo de este estado de letargo enviando un nuevo firmware por red).



### 7. About

* **Funcionalidad:** Pantalla informativa de autoría y versión.
* **Operación:** Renderiza de forma totalmente simétrica y centrada por píxel la versión semántica activa del programa bajo los créditos del desarrollador Guybrush Threepwood. Al pulsar **BACK** o **OK** regresa inmediatamente al menú principal.

---

## 🛠️ Mapeo de Hardware y Distribución de Pines (WT32-ETH01)

Para orientarse físicamente en la placa base: mirar el módulo WT32-ETH01 por la cara superior con el conector de red **RJ45 hacia la derecha**. El **Pin 1 (EN)** es el primero de arriba a la izquierda, bajando en línea recta hasta el **Pin 10**. El **Pin 11** comienza abajo a la derecha y sube en vertical hasta el **Pin 20** (arriba a la derecha).

| Nº Pin Físico | Etiqueta (Silk) | Componente Destino | Tipo de Conexión / Resistencia Requerida |
| --- | --- | --- | --- |
| **2** | `CFG (IO32)` | Pantalla OLED — Pin **SCL** | Línea de reloj síncrono I2C |
| **3** | `485_EN (IO33)` | Pantalla OLED — Pin **SDA** | Línea de datos bidireccional I2C |
| **6** | `GND` | Masa Común (OLED, Teclado, USB) | **Punto de tierra unificado del circuito** |
| **7** | `3V3` | **SÓLO Adaptador Serial USB-TTL** | **¡PRECAUCIÓN!** Inyección de energía exclusiva de flasheo por cable. |
| **9** | `5V` | **Línea de Alimentación Definitiva** | Entrada de corriente continua (VCC) para el funcionamiento en caja. |
| **12** | `IO39` | Pulsador Físico — **Botón MENOS (-)** | **⚠️ REQUIERE RESISTENCIA EXTERNA PULL-UP a 3V3** (Pin *Input Only*). |
| **14** | `IO15` | Pulsador Físico — **Botón MÁS (+)** | Configurado con Pull-Up interno por Software. |
| **15** | `IO14` | Pulsador Físico — **Botón BACK** | Configurado con Pull-Up interno por Software. |
| **18** | `IO4` | Pulsador Físico — **Botón OK** | Configurado con Pull-Up interno por Software. |
| **1** | `EN` | Pulsador Físico — **Botón Reset** | conexion a gnd para resetear . |
| **18** | `IO4` | Pulsador Físico — **Botón OK** | Configurado con Pull-Up interno por Software. |


### 💾 Pines Especiales de Flasheo (Puerto Serie de Depuración)

Para la carga inicial del firmware o recuperación de emergencia usando un programador externo por cable (ej: CH341T), las líneas serie cruzadas se mapean en los pads del transceptor:

* **Pin `TX0` (Transmisión):** Conectar al pin **RX** del programador USB-TTL.
* **Pin `RX0` (Recepción):** Conectar al pin **TX** del programador USB-TTL.

+-------------------+
               |   WIFI ANTENNA    |
               +-------------------+
         TX0  [13] o           o [14]  EN
         RX0  [12] o           o [15]  GND
         IO0  [11] o           o [16]  3V3
         GND  [10] o           o [17]  EN
        IO39  [09] o   ESP32   o [18]  CFG
        IO36  [08] o    ETH    o [19]  485_EN
        IO15  [07] o   RS485   o [20]  RXD
        IO14  [06] o           o [21]  TXD
        IO12  [05] o           o [22]  GND
        IO30  [04] o           o [23]  3V3
         IO4  [03] o           o [24]  GND
         IO2  [02] o           o [25]  5V
         GND  [01] o           o [26]  LINK
               +-------------------+
               |     ETH0 PORT     |
               +-------------------+
> ⚠️ **REGLA DE ORO DE SEGURIDAD ELÉCTRICA:** Jamás se deben conectar simultáneamente la alimentación de 5V de trabajo (Pin 9) y la alimentación de 3.3V del programador serie (Pin 7). Romper esta norma provocará un retorno de corriente que puede dañar de forma permanente el aislamiento del puerto USB de tu ordenador o fundir el regulador del ESP32.

---

## 🧙 Configuración Inicial — Modo Setup

A partir de la versión 5.0.0 el dispositivo incluye un **asistente de configuración inicial** que evita tener que editar `secrets.h` y recompilar para cada instalación nueva.

### ¿Cómo funciona?

El firmware almacena una variable `runSetup` en la memoria no volátil (NVS) del ESP32:

| `runSetup` en NVS | Qué ocurre al arrancar |
|:-:|---|
| `true` (o no existe) | Entra en **Modo Setup** |
| `false` | Arranca en **Modo Proxy** normal |

Cuando la NVS está vacía (dispositivo recién flasheado o flash borrada), `runSetup` no existe y el dispositivo entra en Modo Setup automáticamente.

---

### Qué hace el Modo Setup

Al entrar en Modo Setup el ESP32:

1. Levanta un **punto de acceso WiFi abierto** con el nombre `modbusproxy-5.0.0` (sin contraseña).
2. Asigna la IP `192.168.1.1` a su propia interfaz.
3. Cualquier dispositivo que se conecte a esa WiFi y abra un navegador es **redirigido automáticamente** a la página de configuración (portal cautivo — funciona igual que los WiFi de hoteles).
4. El asistente permite configurar:
   - Tipo de conexión: WiFi o Ethernet
   - Escaneo de redes WiFi visibles con indicador de señal
   - DHCP o IP estática (con IP, máscara, gateway y DNS)
   - IP y puerto del servidor Modbus (EMMA / inversor)
5. Al pulsar **Guardar**, los valores se escriben en la NVS, `runSetup` se pone a `false` y el dispositivo **reinicia en modo proxy** normal.

> La pantalla OLED muestra el nombre del AP y la IP `192.168.1.1` mientras el Modo Setup está activo.

---

### Recuperar el Modo Setup (Factory Reset)

Si el dispositivo ya está configurado pero necesitas volver al asistente — porque cambiaste de red, olvidaste la IP, o quieres reconfigurarlo desde cero — tienes tres opciones:

---

#### Opción A — Factory Reset desde la web *(la más fácil)*

Si todavía tienes acceso a la interfaz web del proxy:

1. Abre `http://<IP_del_proxy>/config` en el navegador.
2. Baja hasta la sección **Zona de Peligro**.
3. Pulsa **Factory Reset (Entrar en Modo Setup)** y confirma.
4. El dispositivo reinicia y levanta el AP `modbusproxy-5.0.0`.

---

#### Opción B — Script de flasheo `flash.py` *(con PlatformIO)*

El repositorio incluye el script `flash.py` en la raíz del proyecto. Compila, flashea y opcionalmente borra la NVS para forzar Modo Setup, todo en un solo paso.

**Requisitos:** Python 3 y PlatformIO instalados.

```bash
python flash.py
```

El script pregunta interactivamente:

```
╔══════════════════════════════════════════╗
║     FLASH — Proxy Modbus (USB)           ║
╚══════════════════════════════════════════╝
  Entorno : esp32_usb

¿Forzar modo Setup tras el flash? (s/N):
```

- **N (por defecto):** flashea el firmware conservando la NVS. El dispositivo arranca con la configuración guardada.
- **s:** flashea el firmware y borra la partición NVS. El dispositivo entra en Modo Setup al arrancar.

> Para cambiar el entorno de compilación: `python flash.py -e PROD_ota`

---

#### Opción C — Borrado manual con Arduino IDE *(sin PlatformIO)*

Arduino IDE no incluye el script `flash.py`, pero puedes conseguir el mismo resultado en dos pasos:

**Paso 1 — Borrar la flash completa desde Arduino IDE:**

En Arduino IDE 2.x, con la placa ESP32 seleccionada y el puerto conectado:

```
Herramientas > Erase Flash > All Flash Contents
```

Esto borra el firmware **y** la NVS. El dispositivo quedará en blanco.

**Paso 2 — Flashear el firmware:**

Abre el proyecto y pulsa **Subir** (Upload) normalmente. Como la NVS está vacía, `runSetup` no existe y el dispositivo entra en Modo Setup.

---

**Alternativa: borrar solo la NVS con esptool** *(sin borrar el firmware)*

Si quieres borrar únicamente la NVS sin tocar el firmware, puedes usar `esptool.py` directamente desde la línea de comandos. Esta herramienta viene incluida con el paquete de placas ESP32 de Arduino IDE.

```bash
esptool.py --port <PUERTO> erase_region 0x9000 0x5000
```

Sustituye `<PUERTO>` por el puerto serie de tu ESP32:
- Windows: `COM3`, `COM4`, etc.
- Linux / macOS: `/dev/ttyUSB0`, `/dev/cu.usbserial-xxxx`, etc.

La ubicación de `esptool.py` según el sistema operativo:

| SO | Ruta habitual |
|---|---|
| Windows | `%LOCALAPPDATA%\Arduino15\packages\esp32\tools\esptool_py\<ver>\esptool.exe` |
| macOS | `~/Library/Arduino15/packages/esp32/tools/esptool_py/<ver>/esptool.py` |
| Linux | `~/.arduino15/packages/esp32/tools/esptool_py/<ver>/esptool.py` |

> Si tienes Python instalado también puedes usar: `python -m esptool --port <PUERTO> erase_region 0x9000 0x5000`

Tras ejecutar el comando, el dispositivo reinicia automáticamente y entra en Modo Setup.

---

### Variable `SETUP_NEEDED` para desarrolladores

En `src/proxy_operativo.hpp` existe la constante:

```cpp
// Cambia a true para que el proximo arranque entre en modo Setup.
// Solo afecta a dispositivos sin NVS configurada (virgen o borrada).
const bool SETUP_NEEDED = false;
```

Si la pones a `true` y borras la NVS antes de flashear (o usas `flash.py` con la opción de forzar Setup), el dispositivo entrará en Modo Setup. Una vez que el usuario completa la configuración, `runSetup` se escribe a `false` en la NVS y el dispositivo arranca normalmente en los siguientes reinicios — aunque `SETUP_NEEDED` siga siendo `true` en el código.

---

## ⚠️ Aviso legal y responsabilidad

Este proyecto es software libre desarrollado y probado en un entorno doméstico particular con equipos Huawei EMMA y SUN2000 específicos. Se publica con la esperanza de que sea útil, pero **sin ninguna garantía de ningún tipo**.

- El autor no se hace responsable de daños en tus equipos, pérdida de datos, interrupciones del suministro eléctrico, baneos de dispositivos Huawei, ni de ningún otro perjuicio derivado del uso de este firmware.
- **Úsalo bajo tu propia responsabilidad.** Asegúrate de entender qué hace el código antes de desplegarlo en un entorno de producción o en instalaciones fotovoltaicas reales.
- Este software no está afiliado ni respaldado por Huawei Technologies Co., Ltd.
- Los nombres Huawei, EMMA, SUN2000 y SmartGuard son marcas registradas de sus respectivos propietarios.

---

## 📄 Licencia

Copyright (C) 2024 mcamposv

Este programa es software libre: puedes redistribuirlo y/o modificarlo bajo los términos de la **GNU General Public License** publicada por la Free Software Foundation, en su versión 3 o (a tu elección) cualquier versión posterior.

Este programa se distribuye con la esperanza de que sea útil, pero **SIN NINGUNA GARANTÍA**; ni siquiera la garantía implícita de COMERCIABILIDAD o IDONEIDAD PARA UN PROPÓSITO PARTICULAR. Consulta la GNU General Public License para más detalles.

Deberías haber recibido una copia de la GNU General Public License junto con este programa. Si no, consulta <https://www.gnu.org/licenses/>.

---
