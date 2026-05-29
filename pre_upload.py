Import("env")
import os
import re
import sys
import glob
import subprocess

# ── Particion NVS en el partition table por defecto del ESP32 ──────────────
NVS_ADDR = "0x9000"
NVS_SIZE = "0x5000"

def find_esptool():
    home = os.path.expanduser("~")
    hits = glob.glob(os.path.join(home, ".platformio", "packages", "tool-esptoolpy*", "esptool.py"))
    return hits[0] if hits else None

def detect_serial_port():
    """Devuelve el primer puerto USB serie detectado, ignorando puertos de sistema."""
    try:
        r = subprocess.run(["pio", "device", "list", "--serial"],
                           capture_output=True, text=True)
        for line in r.stdout.splitlines():
            line = line.strip()
            port = line.split()[0] if line else ""
            # Solo puertos USB — excluir /dev/ttyS* (puertos serie de sistema)
            if (port.startswith("/dev/ttyUSB")   # Linux USB-serial (CH340, CP2102...)
                    or port.startswith("/dev/ttyACM")  # Linux USB-CDC (Arduino Nano, etc.)
                    or port.startswith("/dev/cu.")     # macOS
                    or (len(port) >= 4 and port[:3].upper() == "COM")):  # Windows
                return port
    except Exception:
        pass
    return None

def erase_nvs(port):
    esptool_path = find_esptool()
    esptool_cmd  = [sys.executable, esptool_path] if esptool_path else [sys.executable, "-m", "esptool"]
    cmd = esptool_cmd + ["--port", port, "--baud", "921600", "erase_region", NVS_ADDR, NVS_SIZE]
    print("\n>>> " + " ".join(cmd))
    return subprocess.run(cmd).returncode

# ---------------------------------------------------------------------------
# Helpers de extraccion
# ---------------------------------------------------------------------------

def read_file(filepath):
    """Devuelve el contenido completo de un fichero o cadena vacia si no existe."""
    if not os.path.exists(filepath):
        return ""
    with open(filepath, "r", encoding="utf-8") as f:
        return f.read()

def extract_first(content, pattern, default="???"):
    """Devuelve el primer grupo de captura del patron, o 'default' si no aparece."""
    m = re.search(pattern, content)
    return m.group(1).strip() if m else default

def extract_define(content, name, default="???"):
    """
    Extrae el valor de un #define NAME activo (no comentado) de C/C++.
    Soporta:
      #define NAME "valor con espacios"   -> valor con espacios
      #define NAME valor                  -> valor
    Ignora lineas que empiecen con // (comentadas).
    """
    for line in content.splitlines():
        stripped = line.strip()
        # Ignorar lineas comentadas con //
        if stripped.startswith("//"):
            continue
        # Buscar la directiva activa
        m = re.match(
            r'#\s*define\s+' + re.escape(name) + r'\s+"([^"]+)"',
            stripped
        )
        if m:
            return m.group(1)
        m = re.match(
            r'#\s*define\s+' + re.escape(name) + r'\s+(\S+)',
            stripped
        )
        if m:
            val = m.group(1)
            # Quitar comentario inline si quedara pegado
            val = val.split("//")[0].strip().strip('"')
            return val
    return default

def extract_struct_field_str(content, field, default="???"):
    """
    Extrae el valor por defecto de un campo char[] del struct AppConfig.
    Busca:  char   wifiSSID[64]  = DEFAULT_WIFI_SSID;  -> resuelve la macro
    o bien: char   localIP[16]   = "192.168.1.1";      -> devuelve el literal
    """
    # Intenta encontrar campo = "literal"
    pat_literal = r'char\s+' + re.escape(field) + r'\s*\[\d+\]\s*=\s*"([^"]+)"'
    m = re.search(pat_literal, content)
    if m:
        return m.group(1)
    # Intenta encontrar campo = MACRO_NAME
    pat_macro = r'char\s+' + re.escape(field) + r'\s*\[\d+\]\s*=\s*([A-Z0-9_]+)\s*;'
    m = re.search(pat_macro, content)
    if m:
        return m.group(1)   # Devuelve el nombre de la macro para resolverla despues
    return default

def extract_struct_field_bool(content, field, default="???"):
    """Extrae el valor bool de un campo del struct AppConfig."""
    pat = r'bool\s+' + re.escape(field) + r'\s*=\s*(true|false)'
    return extract_first(content, pat, default)

def extract_struct_field_uint(content, field, default="???"):
    """Extrae el valor entero de un campo uint16_t del struct AppConfig."""
    pat = r'uint16_t\s+' + re.escape(field) + r'\s*=\s*([A-Z0-9_]+)'
    raw = extract_first(content, pat, default)
    # Si es un nombre de macro, intentamos resolverlo despues
    return raw

# ---------------------------------------------------------------------------
# Logica principal del pre-upload
# ---------------------------------------------------------------------------

def ask_for_confirmation(source, target, env):

    src_dir = env.get("PROJECT_SRC_DIR")
    inc_dir = env.get("PROJECT_INCLUDE_DIR")
    root_dir = env.get("PROJECT_DIR")

    # --- Localizar ficheros fuente ---
    def find_file(name, dirs):
        for d in dirs:
            p = os.path.join(d, name)
            if os.path.exists(p):
                return p
        return None

    proxy_path   = find_file("proxy_operativo.hpp", [src_dir, inc_dir])
    secrets_path = find_file("secrets.h",           [src_dir, inc_dir])
    ini_path     = find_file("secrets.ini",          [root_dir])

    proxy_content   = read_file(proxy_path)   if proxy_path   else ""
    secrets_content = read_file(secrets_path) if secrets_path else ""
    ini_content     = read_file(ini_path)     if ini_path     else ""

    # --- Advertencias de ficheros ausentes ---
    warnings = []
    if not proxy_path:
        warnings.append("  AVISO: proxy_operativo.hpp no encontrado")
    if not secrets_path:
        warnings.append("  AVISO: secrets.h no encontrado — se usaran los defaults del struct")
    if not ini_path:
        warnings.append("  AVISO: secrets.ini no encontrado — la contrasena OTA de PlatformIO no estara disponible")

    # -----------------------------------------------------------------------
    # Leer version del firmware desde proxy_operativo.hpp
    # -----------------------------------------------------------------------
    fw_version = extract_first(
        proxy_content,
        r'const\s+String\s+FIRMWARE_VERSION\s*=\s*"([^"]+)"'
    )


    # -----------------------------------------------------------------------
    # Leer configuracion de red desde el struct AppConfig de proxy_operativo.hpp
    # Los campos sensibles apuntan a macros; los resolvemos contra secrets.h
    # -----------------------------------------------------------------------

    # Interfaz y modo IP (bools del struct)
    use_eth_raw  = extract_struct_field_bool(proxy_content, "useEthernet")
    use_dhcp_raw = extract_struct_field_bool(proxy_content, "useDHCP")
    use_eth  = use_eth_raw  == "true"
    use_dhcp = use_dhcp_raw == "true"

    # IP estatica del proxy (literal en el struct)
    local_ip = extract_struct_field_str(proxy_content, "localIP")
    gw_ip    = extract_struct_field_str(proxy_content, "gateway")
    sn_ip    = extract_struct_field_str(proxy_content, "subnet")

    # Puerto Modbus: puede ser la macro DEFAULT_MODBUS_PORT o un numero
    modbus_port_raw = extract_struct_field_uint(proxy_content, "modbusPort")
    if modbus_port_raw.isdigit():
        modbus_port = modbus_port_raw
    else:
        # Es un nombre de macro -> resolverlo en secrets.h
        modbus_port = extract_define(secrets_content, modbus_port_raw, modbus_port_raw)

    # -----------------------------------------------------------------------
    # Leer datos sensibles desde secrets.h (macros DEFAULT_*)
    # -----------------------------------------------------------------------

    # SSID: el struct apunta a DEFAULT_WIFI_SSID
    wifi_ssid_macro = extract_struct_field_str(proxy_content, "wifiSSID")
    if wifi_ssid_macro.startswith("DEFAULT_"):
        wifi_ssid = extract_define(secrets_content, wifi_ssid_macro)
    else:
        wifi_ssid = wifi_ssid_macro   # Era un literal directo (inusual)

    # IP Modbus: el struct apunta a DEFAULT_MODBUS_IP
    modbus_ip_macro = extract_struct_field_str(proxy_content, "modbusIP")
    if modbus_ip_macro.startswith("DEFAULT_"):
        modbus_ip = extract_define(secrets_content, modbus_ip_macro)
    else:
        modbus_ip = modbus_ip_macro

    # Password OTA del struct (DEFAULT_OTA_PASSWORD)
    ota_macro = extract_struct_field_str(proxy_content, "otaPassword")
    if ota_macro.startswith("DEFAULT_"):
        ota_pass_fw = extract_define(secrets_content, ota_macro)
    else:
        ota_pass_fw = ota_macro

    # Password OTA de PlatformIO / secrets.ini  (clave ota_password)
    # IMPORTANTE: re.MULTILINE es obligatorio para que ^ y $ casen por linea
    m_ini = re.search(r'^\s*ota_password\s*=\s*(.+)$', ini_content, re.MULTILINE)
    ota_pass_ini = m_ini.group(1).strip() if m_ini else "???"

    # -----------------------------------------------------------------------
    # Entorno PlatformIO que se esta usando
    # -----------------------------------------------------------------------
    env_name    = env.get("PIOENV", "desconocido")
    upload_port = env.get("UPLOAD_PORT", "(USB / auto)")
    upload_proto = env.get("UPLOAD_PROTOCOL", "serial")

    # -----------------------------------------------------------------------
    # Imprimir resumen
    # -----------------------------------------------------------------------
    SEP  = "=" * 62
    SEP2 = "-" * 62

    print("\n" + SEP)
    print("   REVISION PREVIA AL FLASH  -  Proxy Modbus TCP v" + fw_version)
    print(SEP)

    if warnings:
        for w in warnings:
            print(w)
        print(SEP2)

    # Entorno
    print(f"  Entorno PlatformIO : {env_name}")
    print(f"  Protocolo subida   : {upload_proto}")
    print(f"  Puerto / IP subida : {upload_port}")
    print(SEP2)

    # Red
    tipo_red = "ETHERNET (cable RJ45)" if use_eth  else "WiFi (inalambrico)"
    tipo_ip  = "DHCP (automatica)"    if use_dhcp else "IP Estatica"
    print(f"  Interfaz de red    : {tipo_red}")
    print(f"  Modo de IP         : {tipo_ip}")
    if not use_dhcp:
        print(f"  IP del proxy       : {local_ip}")
        print(f"  Gateway            : {gw_ip}")
        print(f"  Subred             : {sn_ip}")
    if not use_eth:
        print(f"  WiFi SSID          : {wifi_ssid}")
        print(f"  WiFi Pass          : (oculta — ver secrets.h)")
    print(SEP2)

    # Modbus
    print(f"  Destino Modbus TCP : {modbus_ip}:{modbus_port}")
    print(SEP2)

    # OTA — compara las dos fuentes y avisa si discrepan
    ota_match = ota_pass_fw == ota_pass_ini
    ota_icon  = "[OK]" if ota_match else "[DISCREPANCIA]"
    print(f"  OTA pass (fw)      : {ota_pass_fw}")
    print(f"  OTA pass (ini)     : {ota_pass_ini}  {ota_icon}")
    if not ota_match:
        print("  *** ATENCION: la contrasena OTA del firmware y la de")
        print("  *** secrets.ini no coinciden. La subida OTA fallara.")
    print(SEP2)

    # IP de subida OTA vs IP estatica del proxy
    # Solo aplica cuando el protocolo es OTA (espota)
    if upload_proto == "espota":
        # upload_port puede ser "IP:puerto" o solo "IP"
        upload_ip = upload_port.split(":")[0].strip()
        ip_match  = upload_ip == local_ip
        ip_icon   = "[OK]" if ip_match else "[WARNING]"
        print(f"  IP subida OTA      : {upload_ip}  {ip_icon}")
        print(f"  IP proxy (firmware): {local_ip}")
        if not ip_match:
            print(f"  *** ADVERTENCIA: la IP de subida ({upload_ip}) no coincide")
            print(f"  *** con la IP estatica del proxy ({local_ip}).")
            print("  *** La subida OTA fallara si el proxy no esta en esa IP.")
    print(SEP)

    sys.stdout.flush()

    # -----------------------------------------------------------------------
    # Pregunta 1: ¿Flashear?
    # -----------------------------------------------------------------------
    print("  Flashear ahora? [S/n]: ")
    sys.stdout.flush()
    try:
        resp_flash = sys.stdin.readline().strip().lower()
    except KeyboardInterrupt:
        resp_flash = "n"

    if resp_flash in ["n", "no"]:
        sys.stderr.write("\n  Subida CANCELADA por el usuario.\n\n")
        env.Exit(1)
        return

    # -----------------------------------------------------------------------
    # Pregunta 2: ¿Forzar Setup? (solo para subidas USB, no OTA)
    # -----------------------------------------------------------------------
    if upload_proto != "espota":
        print("  Forzar modo Setup en el proximo arranque? [s/N]: ")
        sys.stdout.flush()
        try:
            resp_setup = sys.stdin.readline().strip().lower()
        except KeyboardInterrupt:
            resp_setup = "n"

        if resp_setup in ["s", "si", "y", "yes"]:
            port = upload_port if upload_port != "(USB / auto)" else detect_serial_port()
            if not port:
                sys.stderr.write("\n  [!] No se pudo detectar el puerto. Introduce el puerto manualmente: ")
                sys.stdout.flush()
                port = sys.stdin.readline().strip()

            if port:
                sys.stderr.write(f"\n  [i] Borrando NVS en {port}...\n")
                ret = erase_nvs(port)
                if ret == 0:
                    sys.stderr.write("  [OK] NVS borrada. El dispositivo entrara en modo Setup.\n\n")
                else:
                    sys.stderr.write("  [ERROR] No se pudo borrar la NVS. Abortando.\n\n")
                    env.Exit(1)
                    return
            else:
                sys.stderr.write("  [ERROR] Puerto no especificado. Abortando.\n\n")
                env.Exit(1)
                return

    sys.stderr.write("\n  Confirmado. Compilando y subiendo...\n\n")


# Enganche con PlatformIO
env.AddPreAction("upload", ask_for_confirmation)