#!/usr/bin/env python3
"""Prueba byte-a-byte sobre SPI para Pico slave hardware."""

import argparse
import time

try:
    import spidev
except ImportError as exc:
    raise SystemExit(
        "python3-spidev no esta disponible en esta Raspberry Pi. "
        "Instalalo con: sudo apt install -y python3-spidev"
    ) from exc


CMD_RESET = 0xF0
CMD_NEXT = 0xF1
EXPECTED = bytes((0xA4, 0x29, 0x53, 0x50, 0x49, 0x4D, 0x49, 0x4E))


def xfer_byte(spi, value: int) -> int:
    return spi.xfer2([value & 0xFF])[0]


def main():
    parser = argparse.ArgumentParser(description="Prueba SPI byte-a-byte contra Pico slave hardware.")
    parser.add_argument("--bus", type=int, default=0)
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--hz", type=int, default=10000)
    parser.add_argument("--mode", type=int, default=0)
    parser.add_argument("--count", type=int, default=16)
    parser.add_argument("--sleep-ms", type=int, default=50)
    args = parser.parse_args()

    spi = spidev.SpiDev()
    spi.open(args.bus, args.device)
    spi.max_speed_hz = args.hz
    spi.mode = args.mode
    spi.bits_per_word = 8

    print("=== SPI BYTE PROTO TEST ===")
    print(f"spidev{args.bus}.{args.device} | hz={args.hz} | mode={args.mode} | count={args.count}")
    print("Secuencia: RESET(F0) y luego NEXT(F1) repetido")
    print("")

    try:
        rx0 = xfer_byte(spi, CMD_RESET)
        print(f"RESET -> RX={rx0:02X}")
        time.sleep(max(args.sleep_ms, 0) / 1000.0)

        collected = bytearray()
        for i in range(args.count):
            rx = xfer_byte(spi, CMD_NEXT)
            collected.append(rx)
            print(f"[{i+1:02d}] NEXT -> RX={rx:02X}")
            time.sleep(max(args.sleep_ms, 0) / 1000.0)

        print("")
        print(f"RX bytes: {bytes(collected).hex(' ')}")
        if bytes(collected[:8]) == EXPECTED:
            print("Veredicto: firma_SPI_MIN_OK")
        else:
            print("Veredicto: firma no coincide")
    finally:
        spi.close()


if __name__ == "__main__":
    main()
