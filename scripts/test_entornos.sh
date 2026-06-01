#!/bin/bash
echo "=================================================="
echo "🚀 LANZANDO DIAGNÓSTICO MODBUS EN PARALELO"
echo "=================================================="

# Ajusta estas IPs a las de tu entorno
IPS=(
    "192.168.1.101:PROXI OK (Producción Real)"
    "192.168.1.102:EMMA EMULADA (Tu laboratorio)"
    "192.168.1.103:PROXY BAJO PRUEBAS (El que se cuelga)"
)

for entry in "${IPS[@]}"; do
    ip="${entry%%:*}"
    desc="${entry#*:}"

    echo "--------------------------------------------------"
    echo "📡 PROBANDO IP: $ip -> $desc"
    echo "--------------------------------------------------"

    mbpoll -m tcp -t 3 -a 0 -r 30000 -c 15 -1 "$ip"
    
    echo "" # Salto de línea para separar lecturas
done

echo "=================================================="
echo "🏁 PRUEBAS FINALIZADAS"
echo "=================================================="
