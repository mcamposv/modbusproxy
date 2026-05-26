import socket
import struct
import argparse
import time

# ==========================================
# 🗺️ MAPA DE SERVIDORES
# ==========================================
SERVIDORES = {
    "emulador":   "192.168.254.212", 
    "proxy_test": "192.168.254.213", 
    "produccion": "192.168.254.211", 
    "emmareal":   "192.168.254.210"  
}

IDS_A_ESCANEAR = [0, 1, 2, 3, 6, 16, 100]

def decodificar_texto(payload_bytes):
    """Convierte los bytes extraídos a texto ASCII"""
    texto = ""
    for b in payload_bytes:
        texto += chr(b) if 32 <= b <= 126 else ' '
    return texto

def pedir_modbus_crudo(ip, puerto, unit_id, registro_inicio, cantidad):
    """Genera la trama hexadecimal pura y la envía por socket TCP"""
    # Cabecera MBAP (Transaction ID=1, Protocol=0, Length=6, UnitID) + PDU (FC=3, Reg, Qty)
    # Formato: >HHHBBHH (Big Endian)
    trama_peticion = struct.pack('>HHHBBHH', 1, 0, 6, unit_id, 3, registro_inicio, cantidad)
    
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(1.0)
    
    try:
        s.connect((ip, puerto))
        s.sendall(trama_peticion)
        respuesta = s.recv(1024)
        s.close()
        
        # Validar si es una respuesta válida de Modbus (FC 0x03)
        if len(respuesta) >= 9 and respuesta[7] == 3:
            byte_count = respuesta[8]
            payload = respuesta[9:9+byte_count]
            return True, payload
        else:
            return False, b''
    except Exception:
        s.close()
        return False, b''

def escanear_servidor(ip_destino):
    print(f"\n{'='*50}")
    print(f"🕵️ INICIANDO ESCÁNER FORENSE EN {ip_destino}:502")
    print(f"{'='*50}")

    # --- PRUEBA 1: Registro 30000 (Modelo) ---
    print("\n--- PRUEBA 1: Registro 30000 (Modelo, Longitud 15) ---")
    for unit_id in IDS_A_ESCANEAR:
        exito, payload = pedir_modbus_crudo(ip_destino, 502, unit_id, 30000, 15)
        
        if exito:
            hex_data = payload.hex()
            texto = decodificar_texto(payload)
            print(f"  [ID {unit_id:02d}] ✅ ¡BINGO! Datos: {hex_data}")
            print(f"             📝 Texto: {texto}")
        else:
            print(f"  [ID {unit_id:02d}] 💥 Fallo de conexión: timed out")
        time.sleep(0.3)

    # --- PRUEBA 2: Registro 30070 (Familia) ---
    print("\n--- PRUEBA 2: Registro 30070 (Familia, Longitud 1) ---")
    for unit_id in IDS_A_ESCANEAR:
        exito, payload = pedir_modbus_crudo(ip_destino, 502, unit_id, 30070, 1)
        
        if exito:
            hex_data = payload.hex()
            print(f"  [ID {unit_id:02d}] ✅ ¡BINGO! Datos: {hex_data}")
        else:
            print(f"  [ID {unit_id:02d}] 💥 Fallo de conexión: timed out")
        time.sleep(0.3)

    print(f"\n{'='*50}")
    print("🏁 ESCÁNER FINALIZADO")
    print(f"{'='*50}\n")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Escáner Modbus Crudo (Zero Dependencias)")
    parser.add_argument("-t", "--target", choices=SERVIDORES.keys(), default="emulador")
    args = parser.parse_args()
    
    ip_seleccionada = SERVIDORES[args.target]
    print(f"🎯 Target seleccionado: {args.target.upper()} ({ip_seleccionada})")
    escanear_servidor(ip_seleccionada)