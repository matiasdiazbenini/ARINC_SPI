#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Uso:
  tools/raspi_services.sh ACCION [opciones]

Controla por SSH los servicios ARINC de la Raspberry Pi 3B+.

Acciones:
  status     Mostrar estado systemd
  start      Arrancar bridge, Flask y supervisor
  stop       Frenar supervisor, Flask y bridge
  restart    Reiniciar bridge, Flask y supervisor
  enable     Habilitar autoarranque
  disable    Deshabilitar autoarranque
  logs       Mostrar ultimas lineas de journalctl

Opciones:
  --host IP       IP o hostname de la Raspberry (default: 192.168.50.2)
  --user USUARIO  Usuario SSH (default: mdiaz501)
  -h, --help      Mostrar esta ayuda

Ejemplos:
  tools/raspi_services.sh status
  tools/raspi_services.sh stop
  tools/raspi_services.sh restart
EOF
}

if [[ $# -eq 0 ]]; then
  usage >&2
  exit 2
fi

ACTION="$1"
shift

RASPI_HOST="${RASPI_HOST:-192.168.50.2}"
RASPI_USER="${RASPI_USER:-mdiaz501}"

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

case "$ACTION" in
  status)
    ssh "$REMOTE" "systemctl status arinc-sniffer-bridge arinc-dashboard-flask arinc-supervisor --no-pager"
    ;;
  start)
    ssh "$REMOTE" "sudo systemctl start arinc-sniffer-bridge arinc-dashboard-flask arinc-supervisor"
    ;;
  stop)
    ssh "$REMOTE" "sudo systemctl stop arinc-supervisor arinc-dashboard-flask arinc-sniffer-bridge"
    ;;
  restart)
    ssh "$REMOTE" "sudo systemctl restart arinc-sniffer-bridge arinc-dashboard-flask arinc-supervisor"
    ;;
  enable)
    ssh "$REMOTE" "sudo systemctl enable arinc-sniffer-bridge arinc-dashboard-flask arinc-supervisor"
    ;;
  disable)
    ssh "$REMOTE" "sudo systemctl disable arinc-supervisor arinc-dashboard-flask arinc-sniffer-bridge"
    ;;
  logs)
    ssh "$REMOTE" "journalctl -u arinc-sniffer-bridge -u arinc-dashboard-flask -u arinc-supervisor -n 120 --no-pager"
    ;;
  -h|--help)
    usage
    ;;
  *)
    echo "Accion no reconocida: $ACTION" >&2
    usage >&2
    exit 2
    ;;
esac
