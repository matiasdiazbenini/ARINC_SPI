# Operacion Raspberry Pi 3B+ con scripts

Esta guia deja preparado un camino reproducible para reemplazar la copia manual
por WinSCP cuando se quiera actualizar la Raspberry Pi 3B+ desde WSL.

No automatiza la carga de firmware `.uf2` en las Pico. Ese paso sigue siendo
manual porque depende de conectar cada placa en modo BOOTSEL.

## Requisitos en WSL

Desde la notebook:

```bash
sudo apt update
sudo apt install openssh-client rsync
```

Repositorio esperado:

```bash
cd ~/pampa/ARINC_SPI
```

Red recomendada:

- notebook Ethernet: `192.168.50.1/24`
- Raspberry Pi 3B+ `eth0`: `192.168.50.2/24`
- usuario Raspberry: `mdiaz501`
- ruta remota: `/home/mdiaz501/PAMPA`

## Conexion SSH

Prueba basica:

```bash
ssh mdiaz501@192.168.50.2
```

Para no escribir contrasena en cada copia:

```bash
ssh-copy-id mdiaz501@192.168.50.2
```

## Verificar estado

```bash
cd ~/pampa/ARINC_SPI
chmod +x tools/raspi_*.sh
./tools/raspi_healthcheck.sh
```

El healthcheck entra por SSH y consulta dentro de la Raspberry:

- `systemctl` de `arinc-sniffer-bridge`
- `systemctl` de `arinc-dashboard-flask`
- `systemctl` de `arinc-supervisor`
- `http://127.0.0.1:5100/health`
- `http://127.0.0.1:5100/stats`
- `http://127.0.0.1:5000/stats`

El bridge se consulta por loopback porque el servicio esta configurado para
escuchar en `127.0.0.1` dentro de la Raspberry.

## Actualizacion normal

Para copiar codigo actualizado e instalar los unit files systemd:

```bash
cd ~/pampa/ARINC_SPI
./tools/raspi_deploy.sh
```

El script copia por `rsync`:

- `HOST_3b+` -> `/home/mdiaz501/PAMPA/HOST_3b+`
- `DASHBOARD_WEB` -> `/home/mdiaz501/PAMPA/DASHBOARD_WEB`

Luego instala los `.service` versionados y reinicia:

- `arinc-sniffer-bridge`
- `arinc-dashboard-flask`
- `arinc-supervisor`

## Primera instalacion o venv roto

Si los entornos virtuales no existen, o se quiere reinstalar dependencias:

```bash
./tools/raspi_deploy.sh --install-deps
```

Esto crea/actualiza:

- `/home/mdiaz501/PAMPA/HOST_3b+/.venv`
- `/home/mdiaz501/PAMPA/DASHBOARD_WEB/arinc_dashboard/.venv`

E instala los requirements de cada componente.

## Simular sin modificar la Raspberry

```bash
./tools/raspi_deploy.sh --dry-run
```

Este modo muestra que copiaria `rsync`, pero no instala servicios ni reinicia
procesos.

## Control de servicios

Estado:

```bash
./tools/raspi_services.sh status
```

Reinicio limpio:

```bash
./tools/raspi_services.sh restart
```

Frenar todo para entregar las placas o trabajar en hardware:

```bash
./tools/raspi_services.sh stop
```

Volver a arrancar:

```bash
./tools/raspi_services.sh start
```

Deshabilitar autoarranque:

```bash
./tools/raspi_services.sh disable
```

Habilitar autoarranque:

```bash
./tools/raspi_services.sh enable
```

Ver logs recientes:

```bash
./tools/raspi_services.sh logs
```

## Cambiar IP, usuario o ruta

Por opcion:

```bash
./tools/raspi_deploy.sh --host 192.168.50.2 --user mdiaz501 --target /home/mdiaz501/PAMPA
```

Por variables de entorno:

```bash
RASPI_HOST=192.168.50.2 RASPI_USER=mdiaz501 ./tools/raspi_healthcheck.sh
```

Nota: los unit files versionados usan `User=mdiaz501` y
`/home/mdiaz501/PAMPA`. Si se cambia usuario o ruta, primero hay que ajustar
`HOST_3b+/systemd/*.service`.

## Relacion con WinSCP

WinSCP copia archivos manualmente. `raspi_deploy.sh` hace la misma tarea con
`rsync`, pero con tres ventajas:

- copia siempre las mismas carpetas;
- excluye `.venv`, logs y caches;
- reinicia y verifica los servicios al finalizar.

Se puede seguir usando WinSCP. Estos scripts son el camino automatizado para
cuando se quiera repetir una actualizacion sin depender de pasos manuales.
