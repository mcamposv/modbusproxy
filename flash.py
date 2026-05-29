#!/usr/bin/env python3
"""
flash.py — Atajo para compilar y flashear modbusproxy por USB.

Equivalente a ejecutar directamente:
    pio run -e esp32_usb -t upload

El script pre_upload.py (enganchado en PlatformIO) gestiona automaticamente
el resumen de configuracion, la confirmacion y la opcion de forzar modo Setup.

Uso:  python flash.py
      python flash.py -e PROD_ota
"""

import subprocess
import sys
import argparse

DEFAULT_ENV = "esp32_usb"

def main():
    parser = argparse.ArgumentParser(description="Flash modbusproxy via USB")
    parser.add_argument("-e", "--env", default=DEFAULT_ENV,
                        help=f"Entorno PlatformIO (default: {DEFAULT_ENV})")
    args = parser.parse_args()

    ret = subprocess.run(["pio", "run", "-e", args.env, "-t", "upload"]).returncode
    sys.exit(ret)

if __name__ == "__main__":
    main()
