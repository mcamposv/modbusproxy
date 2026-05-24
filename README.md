# 🔋 ESP32 Modbus TCP Proxy & Multiplexer for Huawei EMMA & SUN2000 (v3.2)

Este repositorio contiene el firmware de grado industrial desarrollado para **ESP32** (compatible con conexión WiFi o Ethernet nativa mediante controladores como el LAN8720/WT32-ETH01). El dispositivo actúa como un **escudo de red, proxy transparente bajo demanda (On-Demand) y multiplexor de canales Modbus TCP**.

Su propósito fundamental es solucionar de forma definitiva el problema crítico de bloqueo y baneo por DDoS en los ecosistemas residenciales e industriales de **Huawei (SmartGuard / EMMA / Inversores SUN2000)**, los cuales tienen un firmware estricto que solo tolera **un único cliente TCP concurrente en el puerto 502**, tirando la conexión o aplicando listas negras si Home Assistant, cargadores de vehículos eléctricos (ej: V2C) o sistemas de analítica locales intentan leer métricas simultáneamente.

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

### 🟢 Versión 3.2 (Actual)

* **API REST JSON:** Implementación de un endpoint dedicado (`http://<IP_ESP32>/api/status`) que devuelve la telemetría del proxy, contadores de clientes y diagnóstico en formato JSON puro.
* **Integridad Atómica Estricta:** El protocolo de primado inicial ha sido refinado para pedir bloques exactos de 15 registros al consultar el modelo de la EMMA, evitando excepciones `0x03` y respetando al milímetro las reglas de memoria de Huawei.

### 🔵 Versión 2.0

* **Monitorización Web HTTP:** Inclusión de un dashboard accesible vía navegador en el puerto 80 con estadísticas de estado en tiempo real, registro de IPs clientes, y depuración Modbus.
* **Reensamblador Anti-Fragmentación TCP:** Un motor robusto que detecta paquetes TCP divididos o mutilados por Home Assistant, los junta en el búfer local y los envía a la EMMA de forma limpia y continua.
* **Escudo de Cuarentena Inteligente:** Antes de abrir la pasarela a los clientes, el ESP32 realiza un **Ping ICMP** seguido de un test de lectura Modbus. Si la EMMA está reiniciándose o no hay red, el proxy bloquea a los clientes (Cuarentena) protegiendo a Huawei de recibir un ataque de denegación de servicio (DDoS) involuntario.

---

## ⚙️ Opciones de Configuración del Código (src/main.cpp)

El comportamiento completo de la placa y la pila de red se administra mediante variables estáticas parametrizables situadas en la parte superior del archivo `src/main.cpp`:

* `USE_ETHERNET` (`const bool`): `false` para usar la antena interna WiFi del ESP32. `true` para enrutar todo el tráfico por cable físico usando la pila Ethernet nativa (ej: LAN8720).
* `USE_DHCP` (`const bool`): `true` para solicitar una IP dinámica al router de la vivienda. `false` para forzar la IP estática de rescate configurada en el firmware.
* `ROTATE_SCREEN` (`const bool`): `true` aplica un giro físico de 180 grados a la visualización del OLED SH1106. `false` mantiene la orientación estándar.
* `MODBUS_FIXED_ID` (`const uint8_t`): Identificador de Unidad (Unit ID) utilizado para el test de arranque y primado inicial (Por defecto `0`, correspondiente a la EMMA).
* `MODBUS_TEST_REG` (`const uint16_t`): Dirección del registro Modbus consultado durante el test de vida del arranque (`30000`, Model Name ASCII).
* `RECONNECT_DELAY` (`const uint32_t`): Tiempo de espera en milisegundos (`5000` ms) que aplica el proxy antes de reintentar una conexión contra el puerto 502 de Huawei si el socket se rompe, evitando baneos por reintentos infinitos en ráfaga (Anti-DDoS).

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

El dispositivo levanta un servidor web en el puerto estándar `80` que se refresca automáticamente cada 5 segundos mediante código inline para ofrecer un entorno de monitorización centralizado desde cualquier navegador ingresando en `http://<IP_ESP32>/`. El dashboard incluye:

* **Estado del Servidor:** Muestra con códigos de colores semafóricos el estado real del túnel hacia la EMMA (`STANDBY` en naranja, `CONNECTED` en verde, `CON-ERR`/`PING ERROR` en rojo).
* **Información de Hardware:** Refleja el string del dispositivo identificado (ej: `SmartHEMS` o `SmartHEMS (Forzado por Excepcion)`) y el recuento de conexiones TCP activas de clientes sobre el total permitido (`X / 4`).
* **Caja Naranja de Depuración del Primado Inicial:** Un bloque visual crítico que se muestra si la comprobación inicial está activa, detallando el número total de intentos de conexión, la fase de control de texto exacta (ej: *"Leyendo cuerpo de datos PDU"*), la última trama Hexadecimal enviada, la última trama Hexadecimal recibida del bus y el diagnóstico descriptivo del último error en caso de fallo.
* **Historial Dinámico de IPs Clientes:** Una tabla estructurada que registra las últimas 10 direcciones IP únicas que han atacado el proxy Modbus, sumando el total acumulado de peticiones por cliente y calculando de forma elástica cuántos segundos hace que enviaron su última trama.
* **Apagado Seguro:** Un botón rojo destacado que llama al endpoint `/apagar`, cerrando todos los sockets abiertos con Huawei de forma ordenada para liberar inmediatamente el slot de la EMMA antes de desconectar físicamente el ESP32.

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

## 🎛 ... Manual del Menú de Hardware e Interfaz OLED (Local)

En su estado en reposo (`MENU_IDLE`), el OLED SH1106 muestra la IP local del proxy, el estado de conexión del backend, el nombre del modelo detectado y el contador de sockets (`X/4`). Al pulsar el botón **OK**, el dispositivo suspende el renderizado de reposo y entra en el menú de diagnóstico avanzado navegable con los botones **[+]**, **[-]** y **[BACK]**:

### 1. Historial IPs

* **Funcionalidad:** Muestra una lista secuencial de todas las direcciones IP de los clientes registrados que han enviado tramas al dispositivo.
* **Operación:** Al seleccionar una IP con el botón **OK**, desglosa la dirección IP, el número total de consultas acumuladas y el tiempo transcurrido en segundos desde que envió su última petición Modbus.

### 2. Test Ping Red

* **Funcionalidad:** Fuerza una auditoría instantánea en Capa 3 contra la EMMA ignorando la máquina de estados general.
* **Operación:** Lanza 3 paquetes de pings ICMP asíncronos. Si el host responde, imprime en pantalla `PING OK!` junto con el tiempo medio de respuesta en milisegundos (`Tiempo: X ms`). Si falla, imprime `PING FALLIDO! Host Inalcanzable`.

### 3. Modbus Fijo

* **Funcionalidad:** Realiza un test de inyección manual de comandos en Capa 7 hacia el dispositivo destino utilizando los parámetros por defecto de los peajes.
* **Operación:** Abre un socket manual, inyecta la trama de 15 registros al ID 0, registro 30000, e inspecciona la respuesta en pantalla. Muestra el texto ASCII limpio devuelto por la EMMA o intercepta el código de excepción en formato legible (ej: `Modbus Ok! Respuesta Excepcion Viva`).

### 4. Escaner Auto-HA

* **Funcionalidad:** Modos de búsqueda forense automáticos para mapear el bus RS485/Modbus.
* **Operación:** Emula el algoritmo de descubrimiento de integraciones de domótica. Recorre de forma secuencial una lista de IDs sospechosos, incluyendo de manera estricta el **ID 00 (SmartHEMS / EMMA)** y el **ID 06 (Inversor SUN2000)** (`0, 1, 2, 3, 6, 16, 100, 255`). Envía a cada uno una petición de lectura de 15 registros al bloque 30000. Para cada ID, abre y cierra el socket de manera estricta y con un retraso deliberado para **burlar el cortafuegos de Huawei**. Si localiza un ID que responde con datos de texto válidos, detiene el bucle, guarda el ID con éxito y muestra el resultado en el OLED (ej: *"ID: 6 Leido: SUN2000-10K-LC0"*).

### 5. Pausar / Reanudar Comms

* **Funcionalidad:** Interruptor de aislamiento rápido por software (Modo Mantenimiento).
* **Operación:** Al pulsar **OK** sobre esta opción, el flag global `isPaused` se invierte de estado. Cuando está pausado, el proxy cierra de inmediato el cliente del backend, tira todas las conexiones TCP de los clientes entrantes en el puerto 502 y congela el servicio Modbus, permitiendo al usuario realizar labores de mantenimiento sin que Home Assistant intente reconectar.

### 6. Apagar Proxy

* **Funcionalidad:** Procedimiento de desmantelamiento y apagado seguro de la electrónica.
* **Operación:** Al confirmar la selección con **OK**, el ESP32 ejecuta la rutina `ejecutarApagado()`. Desconecta ordenadamente el socket de Huawei, detiene y destruye las instancias del `proxyServer` Modbus y del `webServer` HTTP para liberar los descriptores en red, corta la comunicación de los sockets de clientes activos, borra la pantalla e imprime un cartel estático en letras grandes: **APAGADO - Seguro desconectar**. El procesador entra entonces en un bucle infinito de seguridad para evitar reconexiones involuntarias hasta que se le corte la alimentación eléctrica.

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
