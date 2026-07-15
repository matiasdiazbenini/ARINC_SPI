#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Uso:
  tools/raspi_deploy.sh [opciones]

Actualiza en la Raspberry Pi 3B+ las carpetas HOST_3b+ y DASHBOARD_WEB,
instala los unit files systemd versionados y reinicia los servicios ARINC.

Opciones:
  --host IP             IP o hostname de la Raspberry (default: 192.168.50.2)
  --user USUARIO        Usuario SSH (default: mdiaz501)
  --target RUTA         Ruta base remota (default: /home/mdiaz501/PAMPA)
  --install-deps        Crear/actualizar venv e instalar dependencias Python
  --no-systemd          No copiar unit files a /etc/systemd/system
  --no-restart          No reiniciar servicios al finalizar
  --dry-run             Mostrar que copiaria rsync sin modificar la Raspberry
  -h, --help            Mostrar esta ayuda

Variables equivalentes:
  RASPI_HOST, RASPI_USER, RASPI_ROOT

Ejemplo:
  tools/raspi_deploy.sh --install-deps
EOF
}

RASPI_HOST="${RASPI_HOST:-192.168.50.2}"
RASPI_USER="${RASPI_USER:-mdiaz501}"
RASPI_ROOT="${RASPI_ROOT:-/home/mdiaz501/PAMPA}"
INSTALL_DEPS=0
INSTALL_SYSTEMD=1
RESTART_SERVICES=1
DRY_RUN=0

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
    --install-deps)
      INSTALL_DEPS=1
      shift
      ;;
    --no-systemd)
      INSTALL_SYSTEMD=0
      shift
      ;;
    --no-restart)
      RESTART_SERVICES=0
      shift
      ;;
    --dry-run)
      DRY_RUN=1
      shift
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

if [[ ! -d "HOST_3b+" || ! -d "DASHBOARD_WEB" ]]; then
  echo "Ejecutar desde la raiz del repo ARINC_SPI." >&2
  exit 1
fi

for cmd in ssh rsync; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Falta '$cmd'. En WSL: sudo apt install openssh-client rsync" >&2
    exit 1
  fi
done

if [[ "$RASPI_USER" != "mdiaz501" || "$RASPI_ROOT" != "/home/mdiaz501/PAMPA" ]]; then
  echo "Advertencia: los unit files systemd versionados usan /home/mdiaz501/PAMPA y User=mdiaz501." >&2
  echo "Si cambia usuario/ruta, ajustar HOST_3b+/systemd/*.service antes de instalar systemd." >&2
fi

echo "Destino: ${REMOTE}:${RASPI_ROOT}"

ssh "$REMOTE" "mkdir -p \"${RASPI_ROOT}/HOST_3b+\" \"${RASPI_ROOT}/DASHBOARD_WEB\""

rsync_opts=(
  -az
  --delete
  --human-readable
  --exclude ".venv/"
  --exclude "__pycache__/"
  --exclude "*.pyc"
  --exclude "*.log"
  --exclude "arinc_supervisor_status.json"
  --exclude "arinc_supervisor_events.jsonl"
)

if [[ "$DRY_RUN" -eq 1 ]]; then
  rsync_opts+=(--dry-run)
fi

echo "Copiando HOST_3b+..."
rsync "${rsync_opts[@]}" "HOST_3b+/" "${REMOTE}:${RASPI_ROOT}/HOST_3b+/"

echo "Copiando DASHBOARD_WEB..."
rsync "${rsync_opts[@]}" "DASHBOARD_WEB/" "${REMOTE}:${RASPI_ROOT}/DASHBOARD_WEB/"

if [[ "$DRY_RUN" -eq 1 ]]; then
  echo "Dry-run finalizado. No se instalan servicios ni se reinicia nada."
  exit 0
fi

ssh "$REMOTE" \
  "RASPI_ROOT='${RASPI_ROOT}' INSTALL_DEPS='${INSTALL_DEPS}' INSTALL_SYSTEMD='${INSTALL_SYSTEMD}' RESTART_SERVICES='${RESTART_SERVICES}' bash -s" <<'REMOTE_SCRIPT'
set -euo pipefail

services=(arinc-sniffer-bridge arinc-dashboard-flask arinc-supervisor)

if [[ "$INSTALL_DEPS" == "1" ]]; then
  echo "Creando/actualizando venv HOST_3b+..."
  python3 -m venv --system-site-packages "$RASPI_ROOT/HOST_3b+/.venv"
  "$RASPI_ROOT/HOST_3b+/.venv/bin/python" -m pip install --upgrade pip
  "$RASPI_ROOT/HOST_3b+/.venv/bin/python" -m pip install -r "$RASPI_ROOT/HOST_3b+/requirements.txt"

  echo "Creando/actualizando venv DASHBOARD_WEB..."
  python3 -m venv "$RASPI_ROOT/DASHBOARD_WEB/arinc_dashboard/.venv"
  "$RASPI_ROOT/DASHBOARD_WEB/arinc_dashboard/.venv/bin/python" -m pip install --upgrade pip
  if [[ -f "$RASPI_ROOT/DASHBOARD_WEB/arinc_dashboard/requirements.txt" ]]; then
    "$RASPI_ROOT/DASHBOARD_WEB/arinc_dashboard/.venv/bin/python" -m pip install -r "$RASPI_ROOT/DASHBOARD_WEB/arinc_dashboard/requirements.txt"
  else
    "$RASPI_ROOT/DASHBOARD_WEB/arinc_dashboard/.venv/bin/python" -m pip install -r "$RASPI_ROOT/DASHBOARD_WEB/arinc_dashboard/requeriments.txt"
  fi
fi

if [[ "$INSTALL_SYSTEMD" == "1" ]]; then
  echo "Instalando unit files systemd..."
  sudo cp "$RASPI_ROOT/HOST_3b+/systemd/"*.service /etc/systemd/system/
  sudo systemctl daemon-reload
  sudo systemctl enable "${services[@]}"
fi

if [[ "$RESTART_SERVICES" == "1" ]]; then
  echo "Reiniciando servicios ARINC..."
  sudo systemctl restart arinc-sniffer-bridge
  sudo systemctl restart arinc-dashboard-flask
  sudo systemctl restart arinc-supervisor
fi
REMOTE_SCRIPT

echo "Deploy terminado. Verificacion rapida:"
"$(dirname "$0")/raspi_healthcheck.sh" --host "$RASPI_HOST" --user "$RASPI_USER" --target "$RASPI_ROOT"
