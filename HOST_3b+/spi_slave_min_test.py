#!/usr/bin/env python3
"""Prueba minima del SPI hardware entre Raspberry Pi 3B+ y Pico."""

import argparse
import time

try:
    import spidev
except ImportError as exc:
    raise SystemExit(
        "python3-spidev no esta disponible en esta Raspberry Pi. "
        "Instalalo con: sudo apt install -y python3-spidev"
    ) from exc


FRAME_SIZE = 32


def build_tx_frame() -> bytes:
    payload = bytearray(FRAME_SIZE)
    payload[0] = 0xA4
    payload[1] = 0x29
    payload[2] = 0x50  # P
    payload[3] = 0x49  # I
    payload[4] = 0x33  # 3
    payload[5] = 0x42  # B
    payload[6] = 0x2B  # +
    payload[7] = 0x2B  # +
    for i in range(8, FRAME_SIZE):
        payload[i] = (0x80 + i) & 0xFF
    return bytes(payload)


def classify_response(raw: bytes) -> str:
    if all(byte == 0x00 for byte in raw):
        return "todo_cero"
    if raw[:8] == bytes((0xA4, 0x29, 0x53, 0x50, 0x49, 0x4D, 0x49, 0x4E)):
        return "firma_SPI_MIN_OK"
    if raw[:2] == bytes((0xA4, 0x29)):
        return "magic_ok_firma_distinta"
    return f"cabecera={raw[:8].hex()}"


def main():
    parser = argparse.ArgumentParser(description="Prueba minima SPI master para Pico slave hardware.")
    parser.add_argument("--bus", type=int, default=0)
    parser.add_argument("--device", type=int, default=0)
    parser.add_argument("--hz", type=int, default=10000)
    parser.add_argument("--mode", type=int, default=0)
    parser.add_argument("--attempts", type=int, default=10)
    parser.add_argument("--sleep-ms", type=int, default=300)
    args = parser.parse_args()

    spi = spidev.SpiDev()
    spi.open(args.bus, args.device)
    spi.max_speed_hz = args.hz
    spi.mode = args.mode
    spi.bits_per_word = 8

    tx_frame = build_tx_frame()

    print("=== SPI SLAVE MIN TEST ===")
    print(f"spidev{args.bus}.{args.device} | hz={args.hz} | mode={args.mode} | attempts={args.attempts}")
    print(f"TX first8: {tx_frame[:8].hex(' ')}")
    print("")

    try:
        for attempt in range(1, args.attempts + 1):
            raw = bytes(spi.xfer2(list(tx_frame)))
            verdict = classify_response(raw)
            print(f"[{attempt:02d}] RX first8={raw[:8].hex(' ')} | {verdict}")
            time.sleep(max(args.sleep_ms, 0) / 1000.0)
    finally:
        spi.close()


if __name__ == "__main__":
    main()
