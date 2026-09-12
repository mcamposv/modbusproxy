# 🔋 ESP32 Modbus TCP Proxy & Multiplexer for Huawei EMMA & SUN2000 (v6.0-0)

This repository contains industrial-grade firmware developed for **ESP32** (optimized for wired connection via the native Ethernet stack of the LAN8720 chip on WT32-ETH01 boards, or via its internal WiFi antenna). The device acts as a **network shield, transparent on-demand proxy, and Modbus TCP channel multiplexer**.

Its core purpose is to permanently solve the critical blocking and DDoS-ban problem in residential and industrial **Huawei (SmartGuard / EMMA / SUN2000 Inverters)** ecosystems. These devices run strict firmware that only tolerates **a single concurrent TCP client on port 502**, dropping connections or blacklisting clients when Home Assistant, EV chargers (e.g. V2C), or local analytics systems try to read metrics simultaneously.

---

## 💡 Minimum Hardware — works without screen or buttons

**All you need is an ESP32 with WiFi** to get the proxy running. The OLED display and physical buttons are optional: they add convenience and local diagnostics, but the firmware is fully functional without them.

| Component | Required? | Notes |
|---|:---:|---|
| ESP32 (any WiFi-capable variant) | ✅ Yes | `esp32dev`, NodeMCU-32S, Wemos D1 Mini32, etc. |
| WiFi or Ethernet connection to your local network | ✅ Yes | Built-in WiFi on any ESP32 |
| OLED SH1106 display (I2C) | ❌ Optional | Local diagnostic menu |
| Physical buttons (OK, +, −, BACK) | ❌ Optional | Only needed if using the display |
| WT32-ETH01 board (native Ethernet) | ❌ Optional | Recommended for permanent installations |

Without display or buttons, all management is done from the web browser (`http://<ESP32_IP>/`) and the REST API (`/api/status`). Setup Mode (initial configuration) also works headless: the ESP32 creates a WiFi access point you connect to from your phone or computer.

> **Quick start:** flash the firmware onto any WiFi-capable ESP32, configure `secrets.h` with your SSID and your EMMA's IP, and open the proxy's web interface. No soldering required.

---

## 🗺️ Network Architecture and Mediation

The ESP32 sits strategically on the local network as a transparent traffic shield and distributor ("On-Demand Proxy").

```text
+--------------------+ 
|   Home Assistant   | ----+
| (Solar Integration)|     |     +-------------------+      +-------------------------+
+--------------------+     |     |   ESP32 FIRMWARE  |      |   HUAWEI EMMA BACKEND   |
                           +---> |   Proxy Multiplex | ---> |      (IP: <EMMA_IP>)    |
+--------------------+     |     |    (Port 502)     |      |                         |
| Browser / API      | ----+     +-------------------+      |  [ID 00]: SmartHEMS     |
| (Web & JSON Status)|     |               |                |  [ID 06]: SUN2000 (10kW)|
+--------------------+     |       [OLED Display]           +-------------------------+
                           |       Metrics & Tests
+--------------------+     |
| Local OLED Display | ----+   (optional)
|  (Diagnostic Menu) |
+--------------------+
```

---

## 🔧 Local Configuration (before compiling)

This project uses **three locally gitignored files** to keep your credentials and IPs out of the repository. Each one has an example `.template` file you must copy and fill in with your actual values.

### 1. `src/secrets.h` — Firmware credentials

Required to compile. Defines the default values loaded on the ESP32's first boot (before configuring from the web interface).

```bash
cp src/secrets.h.template src/secrets.h
```

Edit `src/secrets.h`:

```cpp
#define DEFAULT_WIFI_SSID      "YOUR_WIFI_SSID"
#define DEFAULT_WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"
#define DEFAULT_MODBUS_IP      "192.168.x.x"   // IP of your EMMA or inverter
#define DEFAULT_MODBUS_PORT    502
#define DEFAULT_LOCAL_IP       "192.168.x.x"   // IP the proxy will use on your network
#define DEFAULT_GATEWAY        "192.168.x.1"   // Your router's gateway
#define DEFAULT_SUBNET         "255.255.255.0"
#define DEFAULT_DNS            "192.168.x.1"
#define DEFAULT_OTA_PASSWORD   "your_ota_password"
```

> Once the device boots and saves its configuration from the web interface, these defaults no longer apply. They are only needed for the first flash.

---

### Recommended deployment workflow

Always verify new firmware in the test environment before touching production:

```bash
# 1. Point secrets.h to the test environment (emulator + IP .213) and upload
pio run -e TEST_ota -t upload

# 2. Verify the test proxy boots and connects to the emulator
curl http://192.168.254.213/api/status

# 3. Only if everything looks good: point secrets.h to production (real EMMA + IP .211) and upload
pio run -e PROD_ota -t upload
```

> **Rule:** never update production without first testing the same firmware version in the test environment.

---

### 2. `secrets.ini` — OTA password and upload IPs (PlatformIO)

Required to upload firmware via OTA using the `PROD_ota` and `TEST_ota` profiles.

```bash
cp secrets.ini.template secrets.ini
```

Edit `secrets.ini`:

```ini
[secrets]
ota_password = your_ota_password   ; must match DEFAULT_OTA_PASSWORD
prod_ip      = 192.168.x.x         ; IP of the production ESP32
test_ip      = 192.168.x.x         ; IP of the test ESP32
```

---

### 3. `scripts/local_config.py` — IPs for diagnostic scripts

Optional but recommended. Without this file the scripts require the `--host` argument on every run.

```bash
cp scripts/local_config.py.template scripts/local_config.py
```

Edit `scripts/local_config.py`:

```python
DEFAULT_HOST = "192.168.x.x"   # Default IP for escaner_huawei and test_emma

SERVIDORES = {
    "emulador":   "192.168.x.x",
    "proxy_test": "192.168.x.x",
    "produccion": "192.168.x.x",
    "emmareal":   "192.168.x.x",
}
```

---

### Local files summary

| File | Copy from | Affects |
|---|---|---|
| `src/secrets.h` | `src/secrets.h.template` | Firmware compilation |
| `secrets.ini` | `secrets.ini.template` | OTA upload with PlatformIO |
| `scripts/local_config.py` | `scripts/local_config.py.template` | Python diagnostic scripts |

---

## 🚀 Version History and Changelog

### 🟢 Version 6.0-0 (Current)

* **Multilingual web interface (i18n):** The proxy's web interface now supports multiple languages dynamically. The user can select the language from the Settings page without needing to recompile the firmware. The chosen language is saved to the ESP32's NVS and persists across reboots.
* **Included languages:** Spanish (default), English, and German. All three languages cover 100% of the interface texts: setup portal, dashboard, Modbus log, settings page, shutdown page, and reboot page.
* **C++ struct-based translation architecture:** All strings are stored in flash (`.rodata`) inside `const LangStrings` structs — zero RAM overhead. The active selector is a global pointer `L` set at boot via `i18nInit(cfg.lang)`.
* **Community-extensible:** Adding a new language means creating a single `.hpp` file under `src/i18n/` following the `strings.hpp` template, including it in `i18n.hpp`, and adding the corresponding case in `i18nInit()`. No web handler code needs to be touched.

### 🟡 Version 4.0.0

* **Permanent Wireless Updates (OTA):** Inclusion of the `ArduinoOTA` stack on network port `3232`. Allows flashing new firmware versions remotely over Ethernet or WiFi. Features a dedicated loading screen on the local OLED showing a real-time progress bar (`UPDATING (OTA)`) and a final success confirmation (`SUCCESS!!`).
* **Hardened Security (Anti-Git):** Dual wireless authentication system protected against leaks in public repositories. The password travels encrypted via local injection in PlatformIO, combining a `secrets.ini` file (hidden in `.gitignore`) with `secrets.h` preprocessor directives.
* **Pixel-Perfect Centered About Menu:** New credits screen integrated into the physical interface. Dynamically calculates character string length in memory to precisely center the firmware version. Pays tribute to the legendary pirate Guybrush Threepwood.
* **Passive Shutdown with Live Services:** Complete redesign of the safe teardown subroutine. When pressing "Shut Down Proxy", the core strictly stops all Modbus TCP communication (immediately releasing Huawei's slot), but **keeps the Web Server, JSON API, and OTA listener threads alive**. The device enters a low Modbus consumption mode without becoming unreachable on the network.

### 🟡 Version 3.2

* **REST JSON API:** Implementation of a dedicated endpoint (`http://<ESP32_IP>/api/status`) returning proxy telemetry, client counters, and diagnostics in pure JSON format for external integrations.
* **Strict Atomic Integrity:** The initial priming protocol was refined to request exact blocks of 15 registers when querying the EMMA model, avoiding data exceptions and respecting Huawei's memory rules.

### 🔵 Version 2.0

* **HTTP Web Monitoring:** Inclusion of a browser-accessible dashboard on port 80 with real-time status statistics, client IP logging, and Modbus debugging.
* **Anti-Fragmentation TCP Reassembler:** A robust engine that detects TCP packets split or mangled by Home Assistant, joins them in the local buffer, and forwards them to the EMMA cleanly and continuously.
* **Intelligent Quarantine Shield:** Before opening the gateway to clients, the ESP32 performs an **ICMP Ping** followed by a Modbus read test. If the EMMA is rebooting or the network is down, the proxy blocks clients (Quarantine), protecting Huawei from receiving an involuntary denial-of-service attack (DDoS).

---

## ⚙️ Code Configuration Options (src/proxy_operativo.hpp)

The complete behavior of the board and network stack is managed via parameterizable static variables at the top of the main file:

* `USE_ETHERNET` (`const bool`): `false` to use the ESP32's internal WiFi antenna. `true` to route all traffic over a physical cable using the native Ethernet stack (e.g. LAN8720).
* `USE_DHCP` (`const bool`): `true` to request a dynamic IP from the home router. `false` to force the static rescue IP configured in the firmware.
* `ROTATE_SCREEN` (`const bool`): `true` applies a physical 180-degree rotation to the SH1106 OLED display. `false` keeps the standard orientation.
* `MODBUS_FIXED_ID` (`const uint8_t`): Unit ID used for the boot test and initial priming (default `0`, corresponding to the EMMA).
* `MODBUS_TEST_REG` (`const uint16_t`): Modbus register address queried during the boot liveness test (`30000`, Model Name ASCII).
* `RECONNECT_DELAY` (`const uint32_t`): Wait time in milliseconds (`5000` ms) the proxy applies before retrying a connection to Huawei's port 502 if the socket breaks, preventing bans from burst infinite retries (Anti-DDoS).
* `FIRMWARE_VERSION` (`const String`): Stores the semantic version of the active program, rendered centered in the local menu. Also used as the suffix of the WiFi AP name in Setup Mode (`modbusproxy-<version>`).

---

## 🛡️ Boot Logic and Network Checks (Double Toll)

To guarantee that Home Assistant or any secondary client never triggers a ban on Huawei's firewall, the proxy implements an asynchronous **"Double Security Toll"** managed by the backend state machine on Core 0. Until the board successfully passes both tolls, the proxy's public port `502` remains closed to the outside world:

### Toll 1: The ICMP Ping Test (Layer 3)

* Right after obtaining a network connection, the proxy enters the `BK_STARTUP_PING` state. It sends bursts of direct ICMP pings to the configured Huawei IP (`MODBUS_SERVER_IP`).
* If the host does not respond (because the EMMA is rebooting or the physical link is down), the system transitions to `BK_PING_ERR`, blocks the local server, and shows a **30-second** countdown before retrying. This prevents flooding a powered-off or unreachable device with TCP sockets.

### Toll 2: The Atomic Modbus Priming (Layer 4 / Layer 7)

* **The surgical injection:** Opens a single TCP socket to Huawei's port 502 and injects the exact hexadecimal frame requesting 15 registers starting from address 30000.
* **Why 15 registers (Atomic Integrity):** Huawei requires atomic integrity when reading its text strings. The model name at register 30000 occupies exactly 15 registers (30 bytes). If a client requests a shorter length (e.g. 10 registers), Huawei's firmware considers it an attempt to fragment an indivisible variable and responds with data error code `0x03`. Requesting exactly 15 results in clean communication.
* **Intelligent Liveness Criterion (Exception Detector):** The proxy analyzes the returned function code. If it receives a success (`0x03` or `0x04`), it extracts the text bytes (e.g. `SmartHEMS`) and releases the gateway. But if the device responds with a legitimate Huawei Modbus exception code (`0x83` or `0x84`), the proxy **considers the toll passed anyway**. This is because a device responding with an official Modbus exception is an active device whose firewall is listening correctly.

Once both tolls are cleared, the state changes to `BK_WAITING` and the gates open for Home Assistant.

---

## 💻 Web Control Panel (HTTP Dashboard)

The device runs a web server on standard port `80` that auto-refreshes every 5 seconds, providing a monitoring environment accessible from any browser at `http://<ESP32_IP>/`. The dashboard includes:

* **Server Status:** Shows the real-time tunnel state to the EMMA using traffic-light color coding (`STANDBY` in orange, `CONNECTED` in green, `CON-ERR`/`PING ERROR` in red, and `APAGADO` in highlighted red for safe passive disconnection).
* **Hardware Info:** Displays the identified device string (e.g. `SmartHEMS`) and the count of active TCP client connections over the total allowed (`X / 4`).
* **Orange Priming Debug Box:** A critical visual block shown when the initial check is active, detailing the total number of connection attempts, the exact text control phase, the last hexadecimal frame sent, the last hexadecimal frame received from the bus, and a descriptive diagnosis of the last error on failure.
* **Dynamic Client IP History:** A structured table logging the last 10 unique IP addresses that have hit the Modbus proxy, summing the total accumulated requests per client and elastically computing how many seconds ago they sent their last frame.
* **Safe Shutdown:** A highlighted red button calling the `/apagar` endpoint, immediately releasing the EMMA's slot before cleanly disconnecting the ESP32, keeping the web server and OTA available in the background.

---

## 📊 Remote Monitoring via REST JSON API

To enable native integrations with external systems or create dedicated diagnostic sensors in Home Assistant, the firmware exposes a read-only optimized JSON endpoint at:
👉 `GET http://<ESP32_IP>/api/status`

The response is generated by concatenating memory buffers to eliminate heavy third-party libraries, guaranteeing a response in microseconds under the standard structure:

```json
{
  "uptime_seconds": 3245,
  "backend_state": "CONNECTED",
  "device_model": "SmartHEMS",
  "active_clients": 1,
  "max_clients": 4,
  "debug_attempts": 1,
  "debug_stage": "Completed. Gateway released for Home Assistant.",
  "debug_error": "None (Absolute success!)"
}
```

---

## 🎛 Hardware Menu and OLED Interface Manual (Local)

In its idle state (`MENU_IDLE`), the SH1106 OLED displays the proxy's local IP, backend connection status, detected model name, and socket counter (`X/4`). Pressing the **OK** button suspends idle rendering and enters the advanced diagnostic menu, navigable with the **[+]**, **[-]**, and **[BACK]** buttons:

### 1. IP History

* **Function:** Displays a sequential list of all registered client IP addresses that have sent frames to the device.
* **Operation:** Selecting an IP with the **OK** button breaks down the IP address, the total accumulated queries, and the elapsed time in seconds since its last Modbus request.

### 2. Network Ping Test

* **Function:** Forces an instant Layer 3 audit against the EMMA, bypassing the general state machine.
* **Operation:** Launches 3 asynchronous ICMP pings. If the host responds, prints `PING OK!` along with the average response time in milliseconds (`Time: X ms`). On failure, prints `PING FAILED! Host Unreachable`.

### 3. Fixed Modbus

* **Function:** Performs a manual Layer 7 command injection test to the target device using the default toll parameters.
* **Operation:** Opens a manual socket, injects the 15-register frame to ID 0, register 30000, and inspects the response on screen. Displays clean ASCII text returned by the EMMA or intercepts the exception code in human-readable format.

### 4. Auto-HA Scanner

* **Function:** Automatic forensic search modes to map the RS485/Modbus bus unattended.
* **Operation:** Emulates home automation integration discovery. Sequentially scans suspected IDs (`0, 1, 2, 3, 6, 16, 100, 255`), strictly including **ID 00 (EMMA)** and **ID 06 (SUN2000 Inverter)**. Sends a 15-register read request to block 30000. For each ID, strictly opens and closes the socket with a deliberate delay to **bypass Huawei's firewall**. If a valid ID is found, the loop stops and the result is shown on the OLED (e.g. *"ID: 6 Read: SUN2000-10K-LC0"*).

### 5. Pause / Resume Comms

* **Function:** Quick software isolation switch (Maintenance Mode).
* **Operation:** Pressing **OK** on this option toggles the global `isPaused` flag. When paused, the proxy immediately closes the backend client, drops all incoming TCP connections on port 502, and freezes the Modbus service, allowing maintenance without Home Assistant attempting to reconnect.

### 6. Shut Down Proxy

* **Function:** Safe teardown and isolation procedure for the electronics.
* **Operation:** Confirming with **OK** executes the `ejecutarApagado()` routine. Orderly disconnects Huawei's socket and stops the `proxyServer` Modbus instance to release network descriptors for active clients. Clears the display and prints a static technical breakdown:
  * **ModbusTCP: DISABLED** (The EMMA is 100% free and protected from traffic).
  * **Dashboard: ENABLED** (The web interface continues serving historical telemetry).
  * **OTA: ENABLED** (Allows waking the device from this dormant state by sending new firmware over the network).

### 7. About

* **Function:** Author and version information screen.
* **Operation:** Renders the currently active semantic firmware version in a fully symmetrical, pixel-centered layout beneath the developer credits for Guybrush Threepwood. Pressing **BACK** or **OK** returns immediately to the main menu.

---

## 🛠️ Hardware Mapping and Pin Layout (WT32-ETH01)

To orient yourself physically on the board: look at the WT32-ETH01 module from the top with the **RJ45 network connector facing right**. **Pin 1 (EN)** is the first from the top left, going straight down to **Pin 10**. **Pin 11** starts at the bottom right and goes up to **Pin 20** (top right).

| Physical Pin # | Label (Silk) | Target Component | Connection Type / Required Resistor |
| --- | --- | --- | --- |
| **2** | `CFG (IO32)` | OLED Display — **SCL** pin | I2C synchronous clock line |
| **3** | `485_EN (IO33)` | OLED Display — **SDA** pin | I2C bidirectional data line |
| **6** | `GND` | Common Ground (OLED, Keypad, USB) | **Unified circuit ground point** |
| **7** | `3V3` | **Serial USB-TTL Adapter ONLY** | **⚠️ CAUTION!** Exclusive power injection for cable flashing only. |
| **9** | `5V` | **Main Power Supply Line** | DC current input (VCC) for operation inside enclosure. |
| **12** | `IO39` | Physical Button — **MINUS (-)** | **⚠️ REQUIRES EXTERNAL PULL-UP RESISTOR to 3V3** (Input Only pin). |
| **14** | `IO15` | Physical Button — **PLUS (+)** | Configured with software internal Pull-Up. |
| **15** | `IO14` | Physical Button — **BACK** | Configured with software internal Pull-Up. |
| **18** | `IO4` | Physical Button — **OK** | Configured with software internal Pull-Up. |
| **1** | `EN` | Physical Button — **Reset** | Connect to GND to reset. |

### 💾 Special Flashing Pins (Debug Serial Port)

For initial firmware upload or emergency recovery using an external cable programmer (e.g. CH341T), the crossed serial lines are mapped on the transceiver pads:

* **Pin `TX0` (Transmit):** Connect to the **RX** pin of the USB-TTL programmer.
* **Pin `RX0` (Receive):** Connect to the **TX** pin of the USB-TTL programmer.

```
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
```

> ⚠️ **GOLDEN RULE OF ELECTRICAL SAFETY:** Never connect the 5V working supply (Pin 9) and the 3.3V serial programmer supply (Pin 7) simultaneously. Breaking this rule will cause a back-current that can permanently damage your computer's USB port isolation or blow the ESP32's regulator.

---

## 🧙 Initial Configuration — Setup Mode

From version 5.0.0, the device includes an **initial configuration wizard** that avoids having to edit `secrets.h` and recompile for each new installation. From version 6.0-0, the setup portal is also available in all three supported languages.

### How it works

The firmware stores a `runSetup` variable in the ESP32's non-volatile storage (NVS):

| `runSetup` in NVS | What happens on boot |
|:-:|---|
| `true` (or missing) | Enters **Setup Mode** |
| `false` | Boots in normal **Proxy Mode** |

When NVS is empty (freshly flashed device or erased flash), `runSetup` does not exist and the device automatically enters Setup Mode.

---

### What Setup Mode does

When entering Setup Mode, the ESP32:

1. Creates an **open WiFi access point** named `modbusproxy-6.0-0` (no password).
2. Assigns IP `192.168.1.1` to its own interface.
3. Any device connecting to that WiFi and opening a browser is **automatically redirected** to the configuration page (captive portal — works just like hotel WiFi).
4. The wizard lets you configure:
   - Connection type: WiFi or Ethernet
   - Scan visible WiFi networks with signal strength indicator
   - DHCP or static IP (with IP, mask, gateway, and DNS)
   - IP and port of the Modbus server (EMMA / inverter)
5. Pressing **Save** writes the values to NVS, sets `runSetup` to `false`, and the device **restarts in normal proxy mode**.

> The OLED display shows the AP name and IP `192.168.1.1` while Setup Mode is active.

---

### Recovering Setup Mode (Factory Reset)

If the device is already configured but you need to return to the wizard — because you changed networks, forgot the IP, or want to reconfigure from scratch — you have three options:

---

#### Option A — Factory Reset from the web *(easiest)*

If you still have access to the proxy's web interface:

1. Open `http://<proxy_IP>/config` in your browser.
2. Scroll down to the **Danger Zone** section.
3. Press **Factory Reset (Enter Setup Mode)** and confirm.
4. The device restarts and brings up the `modbusproxy-6.0-0` AP.

---

#### Option B — `flash.py` flashing script *(with PlatformIO)*

The repository includes the `flash.py` script in the project root. It compiles, flashes, and optionally erases NVS to force Setup Mode, all in one step.

**Requirements:** Python 3 and PlatformIO installed.

```bash
python flash.py
```

The script prompts interactively:

```
╔══════════════════════════════════════════╗
║     FLASH — Proxy Modbus (USB)           ║
╚══════════════════════════════════════════╝
  Environment: esp32_usb

Force Setup mode after flash? (y/N):
```

- **N (default):** flashes the firmware keeping the NVS intact. The device boots with the saved configuration.
- **y:** flashes the firmware and erases the NVS partition. The device enters Setup Mode on boot.

> To change the build environment: `python flash.py -e PROD_ota`

---

#### Option C — Manual erase with Arduino IDE *(without PlatformIO)*

Arduino IDE does not include the `flash.py` script, but you can achieve the same result in two steps:

**Step 1 — Erase full flash from Arduino IDE:**

In Arduino IDE 2.x, with the ESP32 board selected and port connected:

```
Tools > Erase Flash > All Flash Contents
```

This erases both the firmware **and** the NVS. The device will be blank.

**Step 2 — Flash the firmware:**

Open the project and press **Upload** normally. Since NVS is empty, `runSetup` does not exist and the device enters Setup Mode.

---

**Alternative: erase only NVS with esptool** *(without erasing the firmware)*

If you want to erase only the NVS without touching the firmware, you can use `esptool.py` directly from the command line. This tool comes bundled with the ESP32 board package for Arduino IDE.

```bash
esptool.py --port <PORT> erase_region 0x9000 0x5000
```

Replace `<PORT>` with your ESP32's serial port:
- Windows: `COM3`, `COM4`, etc.
- Linux / macOS: `/dev/ttyUSB0`, `/dev/cu.usbserial-xxxx`, etc.

Location of `esptool.py` by operating system:

| OS | Common path |
|---|---|
| Windows | `%LOCALAPPDATA%\Arduino15\packages\esp32\tools\esptool_py\<ver>\esptool.exe` |
| macOS | `~/Library/Arduino15/packages/esp32/tools/esptool_py/<ver>/esptool.py` |
| Linux | `~/.arduino15/packages/esp32/tools/esptool_py/<ver>/esptool.py` |

> If you have Python installed you can also use: `python -m esptool --port <PORT> erase_region 0x9000 0x5000`

After running the command, the device restarts automatically and enters Setup Mode.

---

### `SETUP_NEEDED` variable for developers

In `src/proxy_operativo.hpp` there is the constant:

```cpp
// Set to true so the next boot enters Setup Mode.
// Only affects devices without configured NVS (virgin or erased).
const bool SETUP_NEEDED = false;
```

If set to `true` and the NVS is erased before flashing (or using `flash.py` with the force-Setup option), the device will enter Setup Mode. Once the user completes configuration, `runSetup` is written to `false` in NVS and the device boots normally on subsequent restarts — even if `SETUP_NEEDED` remains `true` in the code.

---

## 🌍 Multilingual Support (i18n)

From version 6.0-0, the proxy's web interface is fully multilingual. The language is selected from the device's Settings page (`/config`) and saved persistently to the ESP32's NVS.

### Available languages

| Code | Language | File |
|---|---|---|
| `es` | Español (default) | `src/i18n/es.hpp` |
| `en` | English | `src/i18n/en.hpp` |
| `de` | Deutsch | `src/i18n/de.hpp` |

### Technical architecture

All i18n logic lives in `src/i18n/`:

| File | Purpose |
|---|---|
| `strings.hpp` | Defines the `LangStrings` struct with every field that each language file must fill in |
| `es.hpp` / `en.hpp` / `de.hpp` | One `static const LangStrings LANG_XX` instance per language, stored in flash |
| `i18n.hpp` | Includes all three languages, defines the active pointer `L` and the `i18nInit(lang)` function |

At boot, `loadConfig()` reads the `"lang"` key from NVS and calls `i18nInit(cfg.lang)`, which sets the global pointer `L` to the correct struct. All web handlers use `L->field` to retrieve translated text.

### Adding a new language

1. Create `src/i18n/fr.hpp` (or the target ISO code) by copying the structure of `en.hpp`.
2. Rename the struct to `LANG_FR` and translate all fields.
3. In `src/i18n/i18n.hpp`:
   - Add `#include "fr.hpp"`.
   - Add `else if (strncmp(lang, "fr", 2) == 0) L = &LANG_FR;` inside `i18nInit()`.
4. In `src/proxy_operativo.hpp`, add the `fr` option to the language `<select>` in `handleWebConfig()` and validate it in `handleWebConfigSave()`.
5. Compile and test.

No web handler logic needs to be modified. The complete contract of required fields is defined in `src/i18n/strings.hpp`.

---

## ⚠️ Legal Notice and Liability

This project is free software developed and tested in a private domestic environment with specific Huawei EMMA and SUN2000 devices. It is published in the hope that it will be useful, but **without any warranty of any kind**.

- The author is not responsible for damage to your equipment, data loss, power supply interruptions, Huawei device bans, or any other harm resulting from the use of this firmware.
- **Use it at your own risk.** Make sure you understand what the code does before deploying it in a production environment or real photovoltaic installations.
- This software is not affiliated with or endorsed by Huawei Technologies Co., Ltd.
- The names Huawei, EMMA, SUN2000, and SmartGuard are registered trademarks of their respective owners.

---

## 📄 License

Copyright (C) 2024 mcamposv

This program is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License** as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but **WITHOUT ANY WARRANTY**; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.

---
