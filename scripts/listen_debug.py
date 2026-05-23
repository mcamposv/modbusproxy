#!/usr/bin/env python3
import socket
import sys

def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    try:
        s.bind(('0.0.0.0', 502))
        s.listen(5)
    except PermissionError:
        print("❌ ERROR: Necesitas permisos de administrador para usar el puerto 502.")
        print("👉 Ejecuta el script con sudo: sudo python3 listen_debug.py")
        sys.exit(1)
    except OSError as e:
        print(f"❌ ERROR: No se pudo abrir el puerto 502 ({e}).")
        sys.exit(1)

    print("🚀 =====================================================")
    print("🛰️  SERVIDOR ESPÍA MODBUS (CON REENSAMBLADO TCP)")
    print("   Escuchando en el puerto: 502")
    print("   Pulse Ctrl+C para detener el monitor.")
    print("===================================================== 🚀\n")

    try:
        while True:
            print("⏳ Esperando ráfaga del ESP32...")
            c, a = s.accept()
            print(f"   ➡ ¡CONECTADO! Cliente detectado: {a[0]}:{a[1]}")
            
            # 1. Leemos primero los 7 bytes de la cabecera MBAP
            data = c.recv(7)
            
            if len(data) == 7:
                # 2. Parseamos la longitud Modbus (bytes 4 y 5)
                msg_len = (data[4] << 8) | data[5]
                
                # Como el byte 6 (Unit ID) ya está dentro de los 7 bytes leídos,
                # los bytes reales que faltan por llegar de la PDU son: msg_len - 1
                bytes_faltantes = msg_len - 1
                
                if 0 < bytes_faltantes < 260:
                    # 3. Forzamos a Python a esperar el resto del chorbo de bytes
                    resto = c.recv(bytes_faltantes)
                    data += resto # Los fusionamos en un solo bloque

            # 4. Imprimimos el paquete unificado definitivo
            if data:
                hex_string = data.hex(" ").upper()
                print(f"   📦 Datos HEX: {hex_string}")
                
                if "75 76" in hex_string:
                    print("   💡 [Info] Confirmado: Es la petición del registro 30070.")
            else:
                print("   📦 Conexión vacía o cerrada sin datos.")
                
            c.close()
            print("   🔚 TRANSMISIÓN FINALIZADA (Bloque completo recibido)\n" + "-"*55)
            
    except KeyboardInterrupt:
        print("\n\n🛑 Servidor detenido por el usuario. Liberando puerto 502...")
    finally:
        s.close()

if __name__ == "__main__":
    main()