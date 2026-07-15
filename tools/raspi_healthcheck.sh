#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Uso:
  tools/raspi_healthcheck.sh [opciones]

Verifica por SSH que bridge, dashboard Flask y supervisor esten activos
en la Raspberry Pi 3B+ y consulta los endpoints locales del bridge.

Opciones:
  --host IP             IP o hostname de la Raspberry (default: 192.168.50.2)
  --user USUARIO        Usuario SSH (default: mdiaz501)
  --target RUTA         Ruta base remota (default: /home/mdiaz501/PAMPA)
  --timeout SEG         Timeout SSH/curl (default: 5)
  -h, --help            Mostrar esta ayuda
EOF
}

RASPI_HOST="${RASPI_HOST:-192.168.50.2}"
RASPI_USER="${RASPI_USER:-mdiaz501}"
RASPI_ROOT="${RASPI_ROOT:-/home/mdiaz501/PAMPA}"
TIMEOUT=5

while [[ $# -gt 0 ]]; do
  case "$1" in
    --host)
      RASPI_HOST="$2"
      shift 2
      ;;
    --user)
      RASPI_USER="$2"
      shift 2
      ;;
    --target)
      RASPI_ROOT="$2"
      shift 2
      ;;
    --timeout)
      TIMEOUT="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Opcion no reconocida: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

REMOTE="${RASPI_USER}@${RASPI_HOST}"

ssh -o ConnectTimeout="$TIMEOUT" "$REMOTE" \
  "RASPI_ROOT='${RASPI_ROOT}' CURL_TIMEOUT='${TIMEOUT}' bash -s" <<'REMOTE_SCRIPT'
set -euo pipefail

services=(arinc-sniffer-bridge arinc-dashboard-flask arinc-supervisor)

echo "== systemd =="
for svc in "${services[@]}"; do
  state="$(systemctl is-active "$svc" || true)"
  enabled="$(systemctl is-enabled "$svc" 2>/dev/null || true)"
  printf "%-24s active=%-10s enabled=%s\n" "$svc" "$state" "$enabled"
  if [[ "$state" != "active" ]]; then
    echo "Servicio no activo: $svc" >&2
    exit 1
  fi
done

echo
echo "== bridge /health =="
curl -fsS --max-time "$CURL_TIMEOUT" http://127.0.0.1:5100/health
echo

echo
echo "== bridge /stats resumen =="
curl -fsS --max-time "$CURL_TIMEOUT" http://127.0.0.1:5100/stats | python3 -c '
import json
import sys

d = json.load(sys.stdin)
keys = [
    "running",
    "connected",
    "source_mode",
    "spi_transfer_mode",
    "spi_request_mode",
    "spi_effective_request_mode",
    "spi_requested_hz",
    "detected_bit_rate_txt",
    "accepted_words",
    "received_words",
    "parity_error",
    "spi_errors",
    "spi_startup_errors",
    "supervisor_health",
    "supervisor_state",
]
for key in keys:
    if key in d:
        print(f"{key}: {d[key]}")
'

echo
echo "== dashboard Flask =="
curl -fsS --max-time "$CURL_TIMEOUT" http://127.0.0.1:5000/stats >/dev/null
echo "Flask /stats: OK"

echo
echo "Healthcheck OK"
REMOTE_SCRIPT
