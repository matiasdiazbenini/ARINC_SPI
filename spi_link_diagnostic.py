import argparse
import os
import shutil
import subprocess
import time

try:
    import spidev
except ImportError as exc:  # pragma: no cover - depende de Raspberry Pi OS
    raise SystemExit(
        "python3-spidev no esta disponible en esta Raspberry Pi. "
        "Instalalo con: sudo apt install -y python3-spidev"
    ) from exc

from sniffer_spi_protocol import (
    CMD_GET_STATS,
    CMD_PING,
    RESP_PONG,
    RESP_STATS,
    SNIFFER_SPI_PACKET_SIZE,
    SNIFFER_SPI_TRANSPORT_IDLE,
    SNIFFER_SPI_TRANSPORT_RESET,
    STATUS_OK,
    ProtocolError,
    build_packet,
    parse_packet,
    unpack_stats,
)


def env_flag(name: str, default: bool) -> bool:
    value = os.getenv(name)
    if value is None:
        return default
    return value.strip().lower() not in ("0", "false", "no", "off")


class ManualCsController:
    def __init__(self, gpio: int):
        self.gpio = gpio
        self.backend = self._detect_backend()
        self._run_set("op", "dh")

    @staticmethod
    def _detect_backend() -> str:
        for candidate in ("pinctrl", "raspi-gpio"):
            if shutil.which(candidate):
                return candidate
        raise RuntimeError("No se encontro pinctrl ni raspi-gpio para manejar CS manual en la Raspberry Pi.")

    def _run_set(self, mode: str, level: str) -> None:
        subprocess.run([self.backend, "set", str(self.gpio), mode, level], check=True)

    def low(self) -> None:
        self._run_set("op", "dl")

    def high(self) -> None:
        self._run_set("op", "dh")


def parse_csv_ints(text: str) -> list[int]:
    values = []
    for token in text.split(","):
        token = token.strip()
        if not token:
            continue
        values.append(int(token))
    if not values:
        raise ValueError("la lista no puede quedar vacia")
    return values


def classify_raw_response(raw: bytes) -> str:
    if not raw:
        return "sin_bytes"

    if all(byte == 0x00 for byte in raw):
        return "todo_cero"

    if all(byte == 0xFF for byte in raw):
        return "todo_ff"

    magic = bytes((0xA4, 0x29))
    pos = raw.find(magic)
    if pos == 0:
        return "magic_ok_pero_crc_o_version_fallaron"
    if pos > 0:
        return f"magic_desplazado_byte_{pos}"

    return f"sin_magic cabecera={raw[:8].hex()}"


def xfer_byte(spi,
              value: int,
              cs: ManualCsController | None,
              cs_setup_us: int,
              cs_hold_us: int,
              byte_delay_us: int) -> int:
    if cs is None:
        response = spi.xfer2([value & 0xFF])[0]
    else:
        cs.low()
        if cs_setup_us > 0:
            time.sleep(cs_setup_us / 1_000_000.0)
        try:
            response = spi.xfer2([value & 0xFF])[0]
        finally:
            if cs_hold_us > 0:
                time.sleep(cs_hold_us / 1_000_000.0)
            cs.high()

    if byte_delay_us > 0:
        time.sleep(byte_delay_us / 1_000_000.0)
    return response


def transfer_request(spi,
                     command: int,
                     payload: bytes,
                     sequence_seed: int,
                     retries: int,
                     delay_ms: int,
                     cs: ManualCsController | None,
                     cs_setup_us: int,
                     cs_hold_us: int,
                     byte_delay_us: int):
    request_packet = build_packet(command, payload, sequence=sequence_seed & 0xFFFF)

    last_error = None
    raw_history = []
    for _ in range(max(retries, 1)):
        xfer_byte(spi, SNIFFER_SPI_TRANSPORT_RESET, cs, cs_setup_us, cs_hold_us, byte_delay_us)
        for byte in request_packet:
            xfer_byte(spi, byte, cs, cs_setup_us, cs_hold_us, byte_delay_us)

        if delay_ms > 0:
            time.sleep(delay_ms / 1000.0)

        raw = bytes(
            xfer_byte(
                spi,
                SNIFFER_SPI_TRANSPORT_IDLE,
                cs,
                cs_setup_us,
                cs_hold_us,
                byte_delay_us,
            )
            for _ in range(SNIFFER_SPI_PACKET_SIZE)
        )
        raw_history.append(raw)
        try:
            return parse_packet(raw), raw_history
        except ProtocolError as exc:
            last_error = exc

    detail = classify_raw_response(raw_history[-1]) if raw_history else "sin_historial"
    if last_error is not None:
        raise ProtocolError(f"{last_error} | {detail}")
    raise ProtocolError(f"sin respuesta SPI valida | {detail}")


def run_ping_series(bus: int,
                    device: int,
                    hz: int,
                    delay_ms: int,
                    attempts: int,
                    retries: int,
                    manual_cs_gpio: int | None,
                    cs_setup_us: int,
                    cs_hold_us: int,
                    byte_delay_us: int):
    spi = spidev.SpiDev()
    spi.open(bus, device)
    spi.max_speed_hz = hz
    spi.mode = 0
    spi.bits_per_word = 8
    if manual_cs_gpio is not None:
        spi.no_cs = True

    cs = ManualCsController(manual_cs_gpio) if manual_cs_gpio is not None else None

    success = 0
    failures = {}
    first_failure_samples = {}
    first_success_packet = None
    sequence_seed = 1

    try:
        for _ in range(attempts):
            try:
                packet, _raw_history = transfer_request(
                    spi,
                    CMD_PING,
                    b"",
                    sequence_seed=sequence_seed,
                    retries=retries,
                    delay_ms=delay_ms,
                    cs=cs,
                    cs_setup_us=cs_setup_us,
                    cs_hold_us=cs_hold_us,
                    byte_delay_us=byte_delay_us,
                )
                sequence_seed += 2

                if packet["command"] == RESP_PONG and packet["status"] == STATUS_OK:
                    success += 1
                    if first_success_packet is None:
                        first_success_packet = packet
                else:
                    key = f"respuesta_inesperada cmd=0x{packet['command']:02X} status=0x{packet['status']:02X}"
                    failures[key] = failures.get(key, 0) + 1
            except Exception as exc:
                sequence_seed += 2
                key = str(exc)
                failures[key] = failures.get(key, 0) + 1
                if key not in first_failure_samples:
                    sample = ""
                    if hasattr(exc, "args") and exc.args:
                        sample = str(exc.args[0])
                    first_failure_samples[key] = sample
    finally:
        spi.close()

    return {
        "success": success,
        "attempts": attempts,
        "failures": failures,
        "first_failure_samples": first_failure_samples,
        "first_success_packet": first_success_packet,
    }


def fetch_stats_once(bus: int,
                     device: int,
                     hz: int,
                     delay_ms: int,
                     retries: int,
                     manual_cs_gpio: int | None,
                     cs_setup_us: int,
                     cs_hold_us: int,
                     byte_delay_us: int):
    spi = spidev.SpiDev()
    spi.open(bus, device)
    spi.max_speed_hz = hz
    spi.mode = 0
    spi.bits_per_word = 8
    if manual_cs_gpio is not None:
        spi.no_cs = True

    cs = ManualCsController(manual_cs_gpio) if manual_cs_gpio is not None else None

    try:
        packet, raw_history = transfer_request(
            spi,
            CMD_GET_STATS,
            b"",
            sequence_seed=0x4000,
            retries=retries,
            delay_ms=delay_ms,
            cs=cs,
            cs_setup_us=cs_setup_us,
            cs_hold_us=cs_hold_us,
            byte_delay_us=byte_delay_us,
        )
        if packet["command"] != RESP_STATS or packet["status"] != STATUS_OK:
            raise ProtocolError(
                f"GET_STATS devolvio cmd=0x{packet['command']:02X} status=0x{packet['status']:02X}"
            )
        return unpack_stats(packet["payload"]), raw_history[-1]
    finally:
        spi.close()


def main():
    manual_cs_default = env_flag("ARINC_SPI_MANUAL_CS", False)
    parser = argparse.ArgumentParser(
        description="Diagnostico de conectividad SPI entre Raspberry Pi 3B+ y Pico ARINC_SNIFFER."
    )
    parser.add_argument("--bus", type=int, default=0, help="SPI bus de Linux, por defecto 0")
    parser.add_argument("--device",
                        type=int,
                        default=int(os.getenv("ARINC_SPI_DEVICE", "0")),
                        help="SPI device de Linux, por defecto 0")
    parser.add_argument("--hz-list", default="200000,100000,50000", help="Lista CSV de velocidades SPI")
    parser.add_argument("--delay-ms-list", default="0,2,5", help="Lista CSV de esperas entre request y respuesta")
    parser.add_argument("--attempts", type=int, default=12, help="Cantidad de PING por combinacion")
    parser.add_argument("--retries", type=int, default=4, help="Cantidad de NOP de reintento por intento")
    parser.add_argument("--manual-cs", action="store_true", default=manual_cs_default,
                        help="Usa CS manual por GPIO en vez de CE hardware.")
    parser.add_argument("--cs-gpio", type=int,
                        default=int(os.getenv("ARINC_SPI_CS_GPIO", "8")),
                        help="GPIO BCM para CS manual, default 8 (pin 24 / CE0).")
    parser.add_argument("--cs-setup-us", type=int,
                        default=int(os.getenv("ARINC_SPI_CS_SETUP_US", "150")),
                        help="Espera en us entre bajar CS y clockear SPI.")
    parser.add_argument("--cs-hold-us", type=int,
                        default=int(os.getenv("ARINC_SPI_CS_HOLD_US", "150")),
                        help="Espera en us entre terminar SPI y subir CS.")
    parser.add_argument("--byte-delay-us", type=int,
                        default=int(os.getenv("ARINC_SPI_BYTE_DELAY_US", "1000")),
                        help="Espera en us entre transacciones byte-a-byte.")
    args = parser.parse_args()

    hz_values = parse_csv_ints(args.hz_list)
    delay_values = parse_csv_ints(args.delay_ms_list)

    print("=== SPI LINK DIAGNOSTIC ===")
    print(f"Bus/device : spidev{args.bus}.{args.device}")
    print(f"Velocidades: {hz_values}")
    print(f"Delay (ms) : {delay_values}")
    print(f"Intentos   : {args.attempts}")
    print(f"Reintentos : {args.retries}")
    if args.manual_cs:
        print(f"CS manual  : GPIO{args.cs_gpio} | setup={args.cs_setup_us}us hold={args.cs_hold_us}us")
    else:
        print("CS manual  : desactivado (CE hardware)")
    print(f"Pacing byte: {args.byte_delay_us}us")
    print(f"Transporte : RESET=0x{SNIFFER_SPI_TRANSPORT_RESET:02X} + request[{SNIFFER_SPI_PACKET_SIZE}] + response[{SNIFFER_SPI_PACKET_SIZE}]")
    print()

    best_case = None

    for hz in hz_values:
        for delay_ms in delay_values:
            result = run_ping_series(
                bus=args.bus,
                device=args.device,
                hz=hz,
                delay_ms=delay_ms,
                attempts=args.attempts,
                retries=args.retries,
                manual_cs_gpio=args.cs_gpio if args.manual_cs else None,
                cs_setup_us=args.cs_setup_us,
                cs_hold_us=args.cs_hold_us,
                byte_delay_us=args.byte_delay_us,
            )
            ratio = f"{result['success']}/{result['attempts']}"
            print(f"[PING] hz={hz:>7} delay={delay_ms:>2}ms -> validos={ratio}")

            if result["failures"]:
                for key, count in result["failures"].items():
                    print(f"       fallo x{count}: {key}")

            if best_case is None or result["success"] > best_case["success"]:
                best_case = {
                    "hz": hz,
                    "delay_ms": delay_ms,
                    "success": result["success"],
                }

    print()
    if best_case is None:
        print("No se pudo completar ningun caso de prueba.")
        raise SystemExit(1)

    print(
        f"Mejor caso: hz={best_case['hz']} delay={best_case['delay_ms']}ms "
        f"con {best_case['success']}/{args.attempts} respuestas validas"
    )

    if best_case["success"] > 0:
        try:
            stats, last_raw = fetch_stats_once(
                bus=args.bus,
                device=args.device,
                hz=best_case["hz"],
                delay_ms=best_case["delay_ms"],
                retries=args.retries,
                manual_cs_gpio=args.cs_gpio if args.manual_cs else None,
                cs_setup_us=args.cs_setup_us,
                cs_hold_us=args.cs_hold_us,
                byte_delay_us=args.byte_delay_us,
            )
            print("GET_STATS OK con la mejor combinacion:")
            print(f"  raw frame : {last_raw.hex()}")
            print(f"  stats     : {stats}")
        except Exception as exc:
            print(f"GET_STATS fallo aun con la mejor combinacion: {exc}")
    else:
        print(
            "No hubo ningun PING valido. En este punto conviene revisar primero si la Pico tiene "
            "la build correcta del sniffer con transporte byte-a-byte, y recien despues volver a "
            "mirar cableado o temporizacion."
        )


if __name__ == "__main__":
    main()
