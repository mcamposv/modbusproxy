import socket
import time

# --- CONFIGURACIÓN ---
#IP_PROXY = "192.168.1.100" # EMMA Directa
IP_PROXY = "192.168.1.101" # Asegúrate de que es la IP de tu ESP32
PUERTO = 502

def leer_registro_modbus(slave_id, registro, cantidad):
    # Construcción dinámica de la trama Modbus TCP
    req = bytearray([
        0x00, 0x01,             # Transaction ID
        0x00, 0x00,             # Protocol ID
        0x00, 0x06,             # Longitud del resto de la trama (6 bytes)
        slave_id,               # Unit ID
        0x03,                   # Function Code (Leer Holding Registers)
        (registro >> 8) & 0xFF, # Registro - Parte Alta
        registro & 0xFF,        # Registro - Parte Baja
        (cantidad >> 8) & 0xFF, # Cantidad - Parte Alta
        cantidad & 0xFF         # Cantidad - Parte Baja
    ])

    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3)
        s.connect((IP_PROXY, PUERTO))
        s.sendall(req)

        # 1. Leer solo la cabecera (7 bytes)
        mbap = s.recv(7)
        if len(mbap) < 7:
            print(f"  [ID {slave_id:02d}] ⚠️ Error: Sin respuesta de la EMMA.")
            s.close()
            return

        # 2. Calcular cuántos bytes vienen detrás y leerlos
        longitud_restante = (mbap[4] << 8) | mbap[5]
        pdu = s.recv(longitud_restante)
        s.close()

        if not pdu:
            print(f"  [ID {slave_id:02d}] ⚠️ Error: Sin datos PDU.")
            return

        # 3. Analizar la respuesta
        funcion = pdu[0]
        if funcion == 0x03:
            datos_hex = pdu[2:].hex() # Saltamos la función y el contador de bytes
            print(f"  [ID {slave_id:02d}] ✅ ¡BINGO! Datos: {datos_hex}")
            
            # Si leímos muchos bytes, intentamos traducirlo a texto humano
            if cantidad > 1:
                try:
                    texto = pdu[2:].decode('utf-8', errors='ignore').replace('\x00', '').strip()
                    if texto:
                        print(f"             📝 Texto: {texto}")
                except:
                    pass
        elif funcion == 0x83:
            excepcion = pdu[1]
            print(f"  [ID {slave_id:02d}] ❌ Rechazado (Excepción 0x{excepcion:02X})")
        else:
            print(f"  [ID {slave_id:02d}] ❓ Respuesta rara: {pdu.hex()}")

    except Exception as e:
        print(f"  [ID {slave_id:02d}] 💥 Fallo de conexión: {e}")

# --- INICIO DEL TEST ---
print("==================================================")
print(f"🕵️ INICIANDO ESCÁNER FORENSE EN {IP_PROXY}:{PUERTO}")
print("==================================================\n")

# Añadimos el 6 que te chivó Home Assistant
ids_sospechosos = [0, 1, 2, 3, 6, 16, 100]

print("--- PRUEBA 1: Registro 30000 (Modelo, Longitud 15) ---")
for nid in ids_sospechosos:
    leer_registro_modbus(nid, 30000, 15)
    time.sleep(0.3) # Cortesía para no asfixiar el proxy

print("\n--- PRUEBA 2: Registro 30070 (Familia, Longitud 1) ---")
for nid in ids_sospechosos:
    leer_registro_modbus(nid, 30070, 1)
    time.sleep(0.3)

print("\n==================================================")
print("🏁 ESCÁNER FINALIZADO")
print("==================================================")

