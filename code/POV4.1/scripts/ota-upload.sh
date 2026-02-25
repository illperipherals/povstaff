#!/usr/bin/env bash
set -euo pipefail

ENV_NAME="adafruit_qtpy_esp32s3_n4r2_ota"
HOST="POVStaff.local"
SERIAL_PORT=""
BAUD="115200"
SERIAL_TIMEOUT="15"
FROM_SERIAL="false"
NO_INSTALL="false"

usage() {
  cat <<'USAGE'
Usage: scripts/ota-upload.sh [--env <platformio_env>] [--host <ip-or-hostname>] [--from-serial]
                            [--serial-port <device>] [--baud <rate>] [--timeout <seconds>]
                            [--no-install]

Defaults:
  --env  adafruit_qtpy_esp32s3_n4r2_ota
  --host POVStaff.local
  --baud 115200
  --timeout 15
  --no-install
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -e|--env)
      ENV_NAME="$2"
      shift 2
      ;;
    -h|--host|--ip)
      HOST="$2"
      shift 2
      ;;
    --from-serial)
      FROM_SERIAL="true"
      shift 1
      ;;
    --serial-port)
      SERIAL_PORT="$2"
      shift 2
      ;;
    --baud)
      BAUD="$2"
      shift 2
      ;;
    --timeout)
      SERIAL_TIMEOUT="$2"
      shift 2
      ;;
    --no-install)
      NO_INSTALL="true"
      shift 1
      ;;
    -?|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1"
      usage
      exit 2
      ;;
  esac
done

if [[ "$FROM_SERIAL" == "true" ]]; then
  if [[ -z "$SERIAL_PORT" ]]; then
    shopt -s nullglob
    for port in /dev/cu.usbmodem* /dev/cu.usbserial*; do
      if [[ -e "$port" ]]; then
        SERIAL_PORT="$port"
        break
      fi
    done
    shopt -u nullglob
  fi

  if [[ -z "$SERIAL_PORT" ]]; then
    echo "No serial port found. Pass one with --serial-port /dev/cu.usbmodem101"
    exit 2
  fi

  HOST=$(python3 - "$SERIAL_PORT" "$BAUD" "$SERIAL_TIMEOUT" "$NO_INSTALL" <<'PY'
import re
import subprocess
import sys
import time
import os

def ensure_pyserial():
    no_install = sys.argv[4].lower() == "true"
    try:
        import serial  # noqa: F401
        return True
    except Exception:
        if no_install:
            print("pyserial not found. Install with: pip3 install pyserial", file=sys.stderr)
            return False
        print("pyserial not found. Installing...", file=sys.stderr)
        try:
            if "VIRTUAL_ENV" in os.environ:
                cmd = [sys.executable, "-m", "pip", "install", "pyserial"]
            else:
                cmd = ["pip3", "install", "--user", "pyserial"]
            subprocess.check_call(cmd, stdout=sys.stderr, stderr=sys.stderr)
        except Exception:
            print("Failed to install pyserial. Install with: pip3 install pyserial", file=sys.stderr)
            return False
        return True

if not ensure_pyserial():
    sys.exit(2)

import os
import serial

port = sys.argv[1]
baud = int(sys.argv[2])
timeout = float(sys.argv[3])
pattern = re.compile(r"OTA WiFi connected, IP:\s*([0-9.]+)")

ser = serial.Serial(port, baudrate=baud, timeout=0.5)
start = time.time()
ip = None
try:
    while time.time() - start < timeout:
        line = ser.readline().decode(errors="ignore").strip()
        if not line:
            continue
        match = pattern.search(line)
        if match:
            ip = match.group(1)
            break
finally:
    ser.close()

if not ip:
    print("Timed out waiting for OTA IP from serial.", file=sys.stderr)
    sys.exit(1)

print(ip)
PY
  )
  echo "Using IP from serial: $HOST"
fi

if ! ping -c 1 "$HOST" >/dev/null 2>&1; then
  echo "Unable to reach $HOST."
  echo "Provide the IP from serial with: scripts/ota-upload.sh --host <ip>"
  exit 1
fi

echo "Uploading to $HOST (env: $ENV_NAME)"
pio run -e "$ENV_NAME" -t upload --upload-port "$HOST"
