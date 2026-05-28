Import("env")
import os
import re
import sys

def extract_value(filepath, pattern, default="Desconocido"):
    """Lee un archivo y extrae un valor usando una expresión regular."""
    if not os.path.exists(filepath):
        return default
    with open(filepath, "r", encoding="utf-8") as f:
        content = f.read()
        match = re.search(pattern, content)
        if match:
            return match.group(1)
    return default

def ask_for_confirmation(source, target, env):
    print("\n" + "="*60)
    print(" 🛑 REVISION DE CONFIGURACION ANTES DE FLASHEAR 🛑")
    print("="*60)

    # Directorios donde suelen estar los archivos
    src_dir = env.get("PROJECT_SRC_DIR")
    inc_dir = env.get("PROJECT_INCLUDE_DIR")

    proxy_file = os.path.join(src_dir, "proxy_operativo.hpp")
    if not os.path.exists(proxy_file):
        proxy_file = os.path.join(inc_dir, "proxy_operativo.hpp")
        
    secrets_file = os.path.join(src_dir, "secrets.h")
    if not os.path.exists(secrets_file):
        secrets_file = os.path.join(inc_dir, "secrets.h")

    eth_pattern    = r'const\s+bool\s+USE_ETHERNET\s*=\s*(true|false)\s*;'
    dhcp_pattern   = r'const\s+bool\s+USE_DHCP\s*=\s*(true|false)\s*;'
    ip_pattern     = r'IPAddress\s+local_IP\s*\(\s*(\d{1,3}\s*,\s*\d{1,3}\s*,\s*\d{1,3}\s*,\s*\d{1,3})\s*\)\s*;'
    modbus_pattern = r'const\s+char\*\s*MODBUS_SERVER_IP\s*=\s*"([^"]+)"\s*;'

    use_ethernet = extract_value(proxy_file, eth_pattern)
    use_dhcp     = extract_value(proxy_file, dhcp_pattern)
    modbus_ip    = extract_value(secrets_file, modbus_pattern)
    
    local_ip_raw = extract_value(proxy_file, ip_pattern)
    if local_ip_raw != "Desconocido":
        local_ip = local_ip_raw.replace(" ", "").replace(",", ".")
    else:
        local_ip = "Desconocida / No encontrada"

    print(" Vas a flashear el ESP32 con la siguiente configuracion:\n")
    
    tipo_red = "ETHERNET (Cable RJ45)" if use_ethernet == "true" else "WIFI (Inalámbrico)"
    tipo_ip  = "DHCP (Automática)" if use_dhcp == "true" else "ESTÁTICA"

    print(f"  🔌 Red a utilizar    : {tipo_red}")
    print(f"  ⚙️  Modo de IP       : {tipo_ip}")
    if use_dhcp == "false":
        print(f"  📍 IP Local (ESP32) : {local_ip}")
    print(f"  🎯 Target MODBUS    : {modbus_ip}")
    print("-" * 60)
    
    # Forzamos que se imprima todo el bloque de arriba
    sys.stdout.flush() 

    # --- LA MAGIA ---
    # Usamos sys.stderr para enviar la pregunta saltándonos el bloqueo de PlatformIO
    print(" ❓ ¿Son estos datos correctos para subir el firmware? [s/N]: ")
    sys.stdout.flush()
    
    # Leemos directamente del canal de entrada del sistema
    try:
        respuesta = sys.stdin.readline().strip()
    except KeyboardInterrupt:
        respuesta = "n"
    
    if respuesta.lower() not in ['s', 'si', 'y', 'yes']:
        sys.stderr.write("\n ❌ Subida CANCELADA por el usuario para evitar desastres.\n\n")
        env.Exit(1)
    else:
        sys.stderr.write("\n ✅ Confirmado. Compilando y subiendo...\n\n")

# Enganche con PlatformIO
env.AddPreAction("upload", ask_for_confirmation)