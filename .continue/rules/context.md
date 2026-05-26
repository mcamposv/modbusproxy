# 📋 CONTEXTO TÉCNICO: ESP32 Modbus TCP Proxy & Multiplexer (v4.0.0)

Este documento contiene el estado técnico absoluto del proyecto para su transferencia directa al contexto de otra Inteligencia Artificial o repositorio de código. Se omiten discusiones secundarias y se consolida únicamente la arquitectura, lógica y mapeo de hardware del dispositivo.

---

## 1. Problema de Ingeniería y Propósito
* **Target:** Ecosistemas residenciales/industriales Huawei (SmartGuard / EMMA / Inversores SUN2000).
* **Restricción de fábrica:** El firmware de Huawei solo tolera **un único cliente Modbus TCP concurrente en el puerto 502**. Si múltiples clientes (Home Assistant, cargadores de vehículos eléctricos, sistemas de analítica locales) realizan peticiones simultáneas, el equipo responde bloqueando los sockets, generando latencias o aplicando listas negras por DDoS involuntario.
* **Solución desarrollada:** Un firmware de grado industrial ejecutado en un microcontrolador **ESP32 (optimizado para placa WT32-ETH01)** que actúa como un **escudo de red, proxy transparente bajo demanda (On-Demand) y multiplexor de canales Modbus TCP**. Soporta hasta 4 clientes simultáneos abriendo un único canal limpio y controlado hacia Huawei.

---

## 2. Lógica de Control y Máquina de Estados (Core 0)
Para evitar bloqueos, el proxy implementa un protocolo síncrono de seguridad antes de abrir el puerto público 502 al exterior, denominado **"Doble Peaje de Seguridad"**:

### Peaje 1: Test de Vida en Capa 3 (ICMP Ping)
* Tras obtener IP, el backend entra en el estado `BK_STARTUP_PING`. Lanza ráfagas de pings ICMP directos a la IP de Huawei.
* Si el host no responde (reinicio del equipo o caída de enlace), transiciona a `BK_PING_ERR`, bloquea el servidor local y aplica una cuarentena de **30 segundos** antes de reintentar.

### Peaje 2: Primado Atómico en Capa 7 (Modbus Priming)
* **Inyección Quirúrgica:** Abre un único socket TCP hacia Huawei e inyecta una trama hexadecimal para leer exactamente **15 registros desde la dirección 30000** (Model Name ASCII).
* **Integridad Atómica Estricta:** Huawei exige integridad en la lectura de sus variables String. Si se solicita una longitud menor a la del bloque indivisible, devuelve un error de datos `0x03`. Al pedir los 15 registros exactos (30 bytes), la EMMA responde de forma limpia.
* **Criterio de Validación:** Si responde éxito (`0x03` o `0x04`), extrae la identidad (ej: `SmartHEMS`) y libera la pasarela. Si responde con una excepción oficial de error Modbus (`0x83` o `0x84`), **se da el peaje por superado igualmente**, ya que una excepción legítima demuestra que el cortafuegos de Huawei está activo, libre y escuchando.

Una vez superado el doble peaje, el sistema pasa a `BK_WAITING` y habilita el puerto público.

---

## 3. Funcionalidades Clave Implementadas (v4.0.0)
* **Reensamblador Anti-Fragmentación TCP:** Captura paquetes TCP mutilados o divididos por la red local, los unifica en un búfer atómico y los entrega limpios al bus.
* **Actualizaciones Inalámbricas (OTA):** Pila `ArduinoOTA` nativa activa en el puerto `3232`. Cuenta con pantallas de renderizado local para la barra de progreso (`UPDATING (OTA)`) y verificación de éxito (`SUCCESS!!`).
* **Seguridad de Credenciales (Anti-Git):** Inyección de variables locales en PlatformIO combinando directivas de preprocesador en `secrets.h` con un archivo físico `secrets.ini` (excluido en `.gitignore`).
* **Apagado Pasivo Seguro:** Subrutina de desmantelamiento que detiene estrictamente la comunicación Modbus TCP (liberando el slot de Huawei de inmediato), pero **mantiene vivos los hilos de ejecución del Servidor Web, la API REST JSON y el oyente OTA** para no perder la conectividad con la placa.

---

## 4. Interfaces de Monitorización y API

### Dashboard Web (Puerto 80)
* Servidor HTTP local con refresco automático cada 5 segundos.
* Semáforo visual de estados del backend (`STANDBY`, `CONNECTED`, `CON-ERR`, `PING ERROR`, `APAGADO`).
* Bloque de depuración del primado inicial (intentos, fase de control, última trama hexadecimal enviada/recibida y diagnóstico descriptivo del último error).
* Tabla dinámica de histórico de clientes: registra las últimas 10 IPs únicas conectadas, número de consultas acumuladas y tiempo elástico (segundos transcurridos desde su última petición).

### API REST JSON
* Endpoint público en `GET http://<IP_ESP32>/api/status`.
* Devuelve telemetría pura formateada por concatenación directa en memoria (sin librerías pesadas): `uptime_seconds`, `backend_state`, `device_model`, `active_clients`, `max_clients`, `debug_attempts`, `debug_stage`, y `debug_error`.

---

## 5. Menú Físico Local (Pantalla OLED SH1106)
Interfaz controlada por una máquina de estados de menús (`MENU_IDLE`, `MENU_NAVIGATING`), interactuando mediante interrupciones en botones físicos.
* **1. Historial IPs:** Lista secuencial de clientes. Permite seleccionar una IP para desglosar sus consultas totales y segundos de inactividad.
* **2. Test Ping Red:** Fuerza un diagnóstico instantáneo asíncrono de 3 ráfagas ICMP mostrando latencia en milisegundos.
* **3. Modbus Fijo:** Inyecta manualmente la trama de 15 registros al ID 0 bloque 30000 e imprime la respuesta ASCII limpia en pantalla.
* **4. Escaner Auto-HA:** Bucle de descubrimiento forense secuencial de IDs Modbus sospechosos (`0, 1, 2, 3, 6, 16, 100, 255`). Abre y cierra sockets de manera estricta con un *delay* deliberado para burlar el cortafuegos de Huawei. Captura e imprime el ID activo y modelo localizado (ej: ID 00 EMMA, ID 06 SUN2000).
* **5. Pausar/Reanudar Comms:** Flag `isPaused`. Cierra el cliente backend y tira de inmediato los sockets de clientes externos en el puerto 502 para labores de mantenimiento.
* **6. Apagar Proxy:** Ejecuta la rutina de apagado pasivo seguro y congela la pantalla con los diagnósticos de red estáticos.
* **7. About:** Pantalla de créditos centrada dinámicamente por píxel calculando la longitud del String de versión en memoria.

---

## 6. Mapeo Físico de Hardware (WT32-ETH01)
Orientación: Placa vista de frente con el conector de red **RJ45 hacia la derecha**. La numeración desciende del Pin 1 (EN, arriba izquierda) al Pin 10 (abajo izquierda); y asciende del Pin 11 (abajo derecha) al Pin 20 (arriba derecha).

* **Pin 2 (`CFG / IO32`):** Pantalla OLED — Línea de Reloj **SCL** (I2C).
* **Pin 3 (`485_EN / IO33`):** Pantalla OLED — Línea de Datos **SDA** (I2C).
* **Pin 6 (`GND`):** Masa común y unificada de todo el circuito.
* **Pin 7 (`3V3`):** Entrada VCC **exclusiva para el programador USB-TTL** durante el flasheo de código.
* **Pin 9 (`5V`):** Entrada de alimentación definitiva de trabajo para montaje en caja.
* **Pin 12 (`IO39`):** Pulsador Físico — **Botón MENOS (-)**. *Nota: Este pin es únicamente de entrada (Input Only), carece de resistencia pull-up interna por silicio y REQUIERE una resistencia física externa (Pull-Up) soldada a la línea de 3V3*.
* **Pin 14 (`IO15`):** Pulsador Físico — **Botón MÁS (+)**. (Pull-Up interno por software).
* **Pin 15 (`IO14`):** Pulsador Físico — **Botón BACK**. (Pull-Up interno por software).
* **Pin 18 (`IO4`):** Pulsador Físico — **Botón OK**. (Pull-Up interno por software).
* **Pines Especiales `TX0` / `RX0`:** Puerto serie nativo cruzado hacia el adaptador serie para volcado inicial de firmware (`TX0` -> RX del USB; `RX0` -> TX del USB).

*⚠️ Regla de Seguridad Eléctrica:* Prohibido conectar simultáneamente el Pin 7 (3V3 del USB) y el Pin 9 (5V de la fuente externa) para evitar retornos de corriente destructivos.