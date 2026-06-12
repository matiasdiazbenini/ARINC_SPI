import struct

SNIFFER_SPI_MAGIC = bytes((0xA4, 0x29))
SNIFFER_SPI_PROTOCOL_VERSION = 0x01
SNIFFER_SPI_PACKET_SIZE = 32
SNIFFER_SPI_FILTER_CAPACITY = 10
SNIFFER_SPI_TRANSPORT_RESET = 0xF0
SNIFFER_SPI_TRANSPORT_IDLE = 0xEE

CMD_NOP = 0x00
CMD_PING = 0x01
CMD_POP_EVENT = 0x02
CMD_GET_STATS = 0x03
CMD_GET_FILTER = 0x04
CMD_SET_FILTER = 0x05
CMD_RESET_STATS = 0x06
CMD_GET_LATEST_META = 0x07
CMD_GET_LATEST_SLOT = 0x08

STATUS_OK = 0x00
STATUS_EMPTY = 0x01
STATUS_BAD_CRC = 0x02
STATUS_BAD_MAGIC = 0x03
STATUS_BAD_COMMAND = 0x04
STATUS_BAD_PAYLOAD = 0x05

RESP_NONE = 0x00
RESP_PONG = 0x10
RESP_EVENT = 0x11
RESP_STATS = 0x12
RESP_FILTER = 0x13
RESP_ACK = 0x14
RESP_LATEST_META = 0x15
RESP_LATEST_SLOT = 0x16

EVENT_DATA = 0x01
EVENT_ACK = 0x02

FILTER_MODE_WHITELIST = 0x00
FILTER_MODE_PASS_ALL = 0x01

PACKET_STRUCT = struct.Struct("<2sBBBBH22sH")
EVENT_STRUCT = struct.Struct("<6BIiIHH")
STATS_STRUCT = struct.Struct("<IIIIHHH")
FILTER_STRUCT = struct.Struct("<BB20s")
LATEST_META_STRUCT = struct.Struct("<BBBBIIIHHH")
LATEST_SLOT_STRUCT = struct.Struct("<BBBBBIiIIB")


class ProtocolError(RuntimeError):
    pass


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_packet(command: int, payload: bytes = b"", *, status: int = 0, flags: int = 0, sequence: int = 0) -> bytes:
    if len(payload) > 22:
        raise ValueError("payload demasiado grande para el frame SPI")

    payload_block = payload.ljust(22, b"\x00")
    head = PACKET_STRUCT.pack(
        SNIFFER_SPI_MAGIC,
        SNIFFER_SPI_PROTOCOL_VERSION,
        command & 0xFF,
        status & 0xFF,
        flags & 0xFF,
        sequence & 0xFFFF,
        payload_block,
        0,
    )
    crc = crc16_ccitt(head[:-2])
    return PACKET_STRUCT.pack(
        SNIFFER_SPI_MAGIC,
        SNIFFER_SPI_PROTOCOL_VERSION,
        command & 0xFF,
        status & 0xFF,
        flags & 0xFF,
        sequence & 0xFFFF,
        payload_block,
        crc,
    )


def parse_packet(raw: bytes) -> dict:
    if len(raw) != SNIFFER_SPI_PACKET_SIZE:
        raise ProtocolError(f"largo SPI invalido: {len(raw)}")

    magic, version, command, status, flags, sequence, payload, crc = PACKET_STRUCT.unpack(raw)
    if magic != SNIFFER_SPI_MAGIC:
        raise ProtocolError("magic SPI invalido")
    if version != SNIFFER_SPI_PROTOCOL_VERSION:
        raise ProtocolError("version de protocolo SPI invalida")
    if crc16_ccitt(raw[:-2]) != crc:
        raise ProtocolError("CRC SPI invalido")

    return {
        "command": command,
        "status": status,
        "flags": flags,
        "sequence": sequence,
        "payload": payload,
    }


def unpack_event(payload: bytes) -> dict:
    event_type, channel, label, sdi, ssm, parity_ok, raw_value, scaled_tenths, event_counter, drop_counter, queue_depth = EVENT_STRUCT.unpack(payload[:EVENT_STRUCT.size])
    return {
        "event_type": event_type,
        "channel": channel,
        "label": label,
        "sdi": sdi,
        "ssm": ssm,
        "parity_ok": bool(parity_ok),
        "raw_value": raw_value,
        "scaled_tenths": scaled_tenths,
        "event_counter": event_counter,
        "drop_counter": drop_counter,
        "queue_depth": queue_depth,
    }


def unpack_stats(payload: bytes) -> dict:
    received_words, accepted_words, filtered_words, parity_errors, overflow_events, fwd_resync_events, rev_resync_events = STATS_STRUCT.unpack(payload[:STATS_STRUCT.size])
    return {
        "received_words": received_words,
        "accepted_words": accepted_words,
        "filtered_words": filtered_words,
        "parity_errors": parity_errors,
        "overflow_events": overflow_events,
        "fwd_resync_events": fwd_resync_events,
        "rev_resync_events": rev_resync_events,
    }


def pack_filter(mode: int, entries: list[tuple[int, int]]) -> bytes:
    if len(entries) > SNIFFER_SPI_FILTER_CAPACITY:
        raise ValueError("demasiadas entradas de filtro")

    raw_entries = bytearray()
    for label, sdi in entries:
        raw_entries.append(label & 0xFF)
        raw_entries.append(sdi & 0xFF)
    raw_entries.extend(b"\x00" * (20 - len(raw_entries)))
    return FILTER_STRUCT.pack(mode & 0xFF, len(entries) & 0xFF, bytes(raw_entries))


def unpack_filter(payload: bytes) -> dict:
    mode, count, raw_entries = FILTER_STRUCT.unpack(payload[:FILTER_STRUCT.size])
    entries = []
    for index in range(min(count, SNIFFER_SPI_FILTER_CAPACITY)):
        label = raw_entries[index * 2]
        sdi = raw_entries[index * 2 + 1]
        entries.append({"label": label, "sdi": sdi})
    return {
        "mode": mode,
        "count": count,
        "entries": entries,
    }


def unpack_latest_meta(payload: bytes) -> dict:
    slot_count, slot_capacity, flags, reserved0, snapshot_revision, slot_evictions, last_update_counter, fwd_startup_resync_events, fwd_operational_resync_events, reserved1 = LATEST_META_STRUCT.unpack(
        payload[:LATEST_META_STRUCT.size]
    )
    return {
        "slot_count": slot_count,
        "slot_capacity": slot_capacity,
        "flags": flags,
        "reserved0": reserved0,
        "snapshot_revision": snapshot_revision,
        "slot_evictions": slot_evictions,
        "last_update_counter": last_update_counter,
        "fwd_startup_resync_events": fwd_startup_resync_events,
        "fwd_operational_resync_events": fwd_operational_resync_events,
        "reserved1": reserved1,
    }


def unpack_latest_slot(payload: bytes) -> dict:
    channel, label, sdi, ssm, flags, raw_value, scaled_tenths, update_counter, hit_count, reserved = LATEST_SLOT_STRUCT.unpack(
        payload[:LATEST_SLOT_STRUCT.size]
    )
    return {
        "channel": channel,
        "label": label,
        "sdi": sdi,
        "ssm": ssm,
        "valid": bool(flags & 0x01),
        "parity_ok": bool(flags & 0x02),
        "flags": flags,
        "raw_value": raw_value,
        "scaled_tenths": scaled_tenths,
        "update_counter": update_counter,
        "hit_count": hit_count,
        "reserved": reserved,
    }
