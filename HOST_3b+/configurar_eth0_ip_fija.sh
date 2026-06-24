#!/usr/bin/env bash
set -euo pipefail

# Configura la Raspberry Pi 3B+ para operar por Ethernet directo contra la
# notebook del laboratorio.
#
# Valores por defecto:
# - Raspberry Pi eth0: 192.168.50.2/24
# - Sin gateway por defecto en eth0, para no romper Wi-Fi/Internet si existe.
#
# Uso:
#   sudo bash ~/PAMPA/HOST_3b+/configurar_eth0_ip_fija.sh

IFACE="${ARINC_ETH_IFACE:-eth0}"
PI_IP="${ARINC_PI_IP:-192.168.50.2/24}"
CONNECTION_NAME="${ARINC_ETH_CONNECTION:-arinc-eth0-static}"

if [[ "${EUID}" -ne 0 ]]; then
    echo "Ejecutar con sudo."
    exit 1
fi

echo "Configurando ${IFACE} con IP fija ${PI_IP}"

if command -v nmcli >/dev/null 2>&1 && systemctl is-active --quiet NetworkManager; then
    if ! nmcli -t -f NAME connection show | grep -Fxq "${CONNECTION_NAME}"; then
        nmcli connection add \
            type ethernet \
            ifname "${IFACE}" \
            con-name "${CONNECTION_NAME}" \
            autoconnect yes
    fi

    nmcli connection modify "${CONNECTION_NAME}" \
        connection.interface-name "${IFACE}" \
        connection.autoconnect yes \
        ipv4.method manual \
        ipv4.addresses "${PI_IP}" \
        ipv4.never-default yes \
        ipv6.method disabled

    nmcli connection up "${CONNECTION_NAME}"
    echo "Listo con NetworkManager."
    ip -4 addr show "${IFACE}"
    exit 0
fi

if [[ -f /etc/dhcpcd.conf ]]; then
    backup="/etc/dhcpcd.conf.arinc.bak.$(date +%Y%m%d_%H%M%S)"
    cp /etc/dhcpcd.conf "${backup}"

    sed -i '/# ARINC_SPI_STATIC_ETH0_BEGIN/,/# ARINC_SPI_STATIC_ETH0_END/d' /etc/dhcpcd.conf
    cat >> /etc/dhcpcd.conf <<EOF

# ARINC_SPI_STATIC_ETH0_BEGIN
interface ${IFACE}
static ip_address=${PI_IP}
# ARINC_SPI_STATIC_ETH0_END
EOF

    if systemctl list-unit-files | grep -q '^dhcpcd\.service'; then
        systemctl restart dhcpcd
    fi

    echo "Listo con dhcpcd. Backup: ${backup}"
    ip -4 addr show "${IFACE}"
    exit 0
fi

echo "No se encontro NetworkManager activo ni /etc/dhcpcd.conf."
echo "Configurar manualmente ${IFACE} con ${PI_IP}."
exit 2
