#!/bin/bash
echo "=================================================="
echo "🚀 LANZANDO DIAGNÓSTICO MODBUS EN PARALELO"
echo "=================================================="

for octeto in 211 212 213; do
    case $octeto in
        211) desc="PROXI OK (Producción Real)" ;;
        212) desc="EMMA EMULADA (Tu laboratorio)" ;;
        213) desc="PROXY BAJO PRUEBAS (El que se cuelga)" ;;
    esac

    echo "--------------------------------------------------"
    echo "📡 PROBANDO IP: 192.168.254.$octeto -> $desc"
    echo "--------------------------------------------------"
    
    mbpoll -m tcp -t 3 -a 0 -r 30000 -c 15 -1 192.168.254.$octeto
    
    echo "" # Salto de línea para separar lecturas
done

echo "=================================================="
echo "🏁 PRUEBAS FINALIZADAS"
echo "=================================================="
