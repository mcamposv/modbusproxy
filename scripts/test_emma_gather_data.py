import socket
import threading
import time
import argparse

try:
    from local_config import DEFAULT_HOST as _default_host
except ImportError:
    _default_host = None

parser = argparse.ArgumentParser(description="Test de estrés Modbus TCP para EMMA/inversor Huawei")
parser.add_argument("--host", default=_default_host, required=_default_host is None,
                    help="IP del proxy o EMMA (ej: 192.168.1.100)")
parser.add_argument("--port", type=int, default=502, help="Puerto Modbus (default: 502)")
args = parser.parse_args()

IP_EMMA = args.host
PUERTO = args.port

# Trama Modbus TCP real en Hexadecimal (Pidiendo leer 1 registro)
PAYLOAD_MODBUS = b'\x00\x01\x00\x00\x00\x06\x01\x03\x7d\x00\x00\x01'

def test_cliente_modbus(id_cliente):
    try:
        # 1. Abrimos conexión TCP
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3)
        s.connect((IP_EMMA, PUERTO))
        print(f"🟢 Cliente {id_cliente}: Conectado por TCP.")
        
        # 2. Enviamos la petición de datos Modbus
        s.sendall(PAYLOAD_MODBUS)
        
        # 3. Esperamos la respuesta
        datos = s.recv(1024)
        
        # 4. Evaluamos la respuesta
        if datos:
            print(f"✅ Cliente {id_cliente}: ¡Recibió respuesta Modbus! ({datos.hex()})")
        else:
            print(f"⚠️ Cliente {id_cliente}: Conectó, pero la EMMA no devolvió datos.")
            
        s.close()
    except Exception as e:
        print(f"❌ Cliente {id_cliente}: Falló la prueba. Error: {e}")

print(f"--- Iniciando Test de Estrés Modbus en {IP_EMMA}:{PUERTO} ---")

# Creamos dos "hilos" para que ataquen a la EMMA al milisegundo exacto
hilo1 = threading.Thread(target=test_cliente_modbus, args=(1,))
hilo2 = threading.Thread(target=test_cliente_modbus, args=(2,))

# ¡Disparo simultáneo!
hilo1.start()
hilo2.start()

# Esperamos a que terminen
hilo1.join()
hilo2.join()

print("--- Test Finalizado ---")

