#!/usr/bin/env python3
r"""
STM32 local bootloader upgrade tool.

Default parameters match the current project bring-up:
- Port: COM6
- Baudrate: 115200
- Image: STM32\MDK-ARM\Objects\App.bin

Requires:
    pip install pyserial
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
import zlib
from dataclasses import dataclass
from pathlib import Path

try:
    import serial
except ModuleNotFoundError:
    print("Missing dependency: pyserial. Install with: pip install pyserial", file=sys.stderr)
    raise


SOF = b"\x55\xAA"
BOOT_MAX_PAYLOAD = 256
DATA_PAYLOAD_OVERHEAD = 6
MAX_DATA_CHUNK_SIZE = BOOT_MAX_PAYLOAD - DATA_PAYLOAD_OVERHEAD

CMD_GET_INFO = 0x01
CMD_START = 0x02
CMD_DATA = 0x03
CMD_END = 0x04
CMD_ABORT = 0x05
CMD_QUERY_STATUS = 0x06

RSP_ACK = 0x80
RSP_NACK = 0x81
RSP_INFO = 0x82
RSP_STATUS = 0x83

BOOT_ERR_NAMES = {
    0x0000: "NONE",
    0x0001: "BAD_FRAME",
    0x0002: "BAD_CMD",
    0x0003: "BAD_LENGTH",
    0x0004: "BAD_STATE",
    0x0005: "BAD_SIZE",
    0x0006: "FLASH_ERASE",
    0x0007: "FLASH_PROGRAM",
    0x0008: "BAD_OFFSET",
    0x0009: "VERIFY",
    0x000A: "NOT_COMPLETE",
    0x000B: "BAD_CRC",
    0x000C: "TIMEOUT_RECOVERY",
}

STATE_NAMES = {
    0x0000: "IDLE",
    0x0001: "REQUESTED",
    0x0002: "ERASING",
    0x0003: "PROGRAMMING",
    0x0004: "VERIFYING",
    0x0005: "DONE",
    0x0006: "FAILED",
}


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def format_hex(data: bytes) -> str:
    return data.hex(" ").upper()


def build_frame(cmd: int, seq: int, payload: bytes = b"") -> bytes:
    header = struct.pack("<BBH", cmd, seq & 0xFF, len(payload))
    crc = crc16_modbus(header + payload)
    return SOF + header + payload + struct.pack("<H", crc)


@dataclass
class Frame:
    cmd: int
    seq: int
    payload: bytes
    raw: bytes


class BootloaderTool:
    def __init__(self, port: str, baudrate: int, timeout: float, verbose: bool) -> None:
        self.ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            bytesize=8,
            parity="N",
            stopbits=1,
            timeout=0.02,
            write_timeout=1.0,
            inter_byte_timeout=0.02,
        )
        self.verbose = verbose
        time.sleep(0.5)
        self.reset_input()

    def close(self) -> None:
        if self.ser.is_open:
            self.ser.close()

    def reset_input(self) -> None:
        self.ser.reset_input_buffer()

    def send(self, frame: bytes) -> None:
        if self.verbose:
            print(f"TX: {format_hex(frame)}")
        self.ser.write(frame)
        self.ser.flush()

    def read_frame(self, timeout_s: float) -> Frame:
        deadline = time.time() + timeout_s
        buffer = bytearray()
        while time.time() < deadline:
            time.sleep(0.05)
            chunk = self.ser.read_all()
            if chunk:
                buffer.extend(chunk)
                frame = self._extract_frame(buffer)
                if frame is not None:
                    if self.verbose:
                        print(f"RX: {format_hex(frame.raw)}")
                    return frame
            else:
                time.sleep(0.02)
        raise TimeoutError("Timed out waiting for bootloader response")

    def _extract_frame(self, buffer: bytearray) -> Frame | None:
        while True:
            sof_idx = buffer.find(SOF)
            if sof_idx < 0:
                buffer.clear()
                return None
            if sof_idx > 0:
                del buffer[:sof_idx]
            if len(buffer) < 6:
                return None
            cmd = buffer[2]
            seq = buffer[3]
            payload_len = struct.unpack_from("<H", buffer, 4)[0]
            frame_len = 2 + 1 + 1 + 2 + payload_len + 2
            if len(buffer) < frame_len:
                return None
            raw = bytes(buffer[:frame_len])
            del buffer[:frame_len]

            frame_crc = struct.unpack_from("<H", raw, frame_len - 2)[0]
            calc_crc = crc16_modbus(raw[2:-2])
            if frame_crc != calc_crc:
                print(
                    f"Ignore bad CRC frame: got=0x{frame_crc:04X}, expect=0x{calc_crc:04X}, raw={format_hex(raw)}",
                    file=sys.stderr,
                )
                continue
            payload = raw[6:-2]
            return Frame(cmd=cmd, seq=seq, payload=payload, raw=raw)

    def transact(self, cmd: int, seq: int, payload: bytes = b"", timeout_s: float = 2.0) -> Frame:
        frame = build_frame(cmd, seq, payload)
        self.reset_input()
        self.send(frame)
        response = self.read_frame(timeout_s)

        if response.cmd == RSP_NACK:
            err = struct.unpack("<H", response.payload[:2])[0] if len(response.payload) >= 2 else 0xFFFF
            err_name = BOOT_ERR_NAMES.get(err, "UNKNOWN")
            raise RuntimeError(f"Bootloader NACK: 0x{err:04X} ({err_name})")

        if response.seq != (seq & 0xFF):
            raise RuntimeError(f"Unexpected sequence in response: expect 0x{seq:02X}, got 0x{response.seq:02X}")

        return response


def parse_info_payload(payload: bytes) -> str:
    if len(payload) != 24:
        return f"unexpected info payload length={len(payload)}"
    boot_ver, proto_ver = struct.unpack_from("<HH", payload, 0)
    app_base, app_max_size = struct.unpack_from("<II", payload, 4)
    page_size, state = struct.unpack_from("<HH", payload, 12)
    written_bytes, last_error = struct.unpack_from("<IH", payload, 16)
    return (
        f"boot_ver=0x{boot_ver:04X}, proto_ver=0x{proto_ver:04X}, "
        f"app_base=0x{app_base:08X}, app_max_size=0x{app_max_size:08X}, "
        f"page_size={page_size}, state={STATE_NAMES.get(state, hex(state))}, "
        f"written_bytes={written_bytes}, last_error={BOOT_ERR_NAMES.get(last_error, hex(last_error))}"
    )


def parse_status_payload(payload: bytes) -> str:
    if len(payload) != 20:
        return f"unexpected status payload length={len(payload)}"
    state, last_error = struct.unpack_from("<HH", payload, 0)
    image_size, written_bytes, last_ok_offset, image_crc32 = struct.unpack_from("<IIII", payload, 4)
    return (
        f"state={STATE_NAMES.get(state, hex(state))}, "
        f"last_error={BOOT_ERR_NAMES.get(last_error, hex(last_error))}, "
        f"image_size={image_size}, written_bytes={written_bytes}, "
        f"last_ok_offset={last_ok_offset}, image_crc32=0x{image_crc32:08X}"
    )


def load_image(image_path: Path) -> tuple[bytes, int]:
    image = image_path.read_bytes()
    crc32 = zlib.crc32(image) & 0xFFFFFFFF
    return image, crc32


def validate_chunk_size(chunk_size: int) -> int:
    if chunk_size <= 0:
        raise ValueError("chunk-size must be greater than 0")
    if chunk_size > MAX_DATA_CHUNK_SIZE:
        raise ValueError(
            f"chunk-size {chunk_size} is too large: bootloader payload limit is {BOOT_MAX_PAYLOAD} bytes, "
            f"and DATA uses {DATA_PAYLOAD_OVERHEAD} bytes for offset/length, so chunk-size must be <= {MAX_DATA_CHUNK_SIZE}"
        )
    return chunk_size


def print_image_summary(image_path: Path, image: bytes, crc32: int) -> None:
    print(f"Image: {image_path}")
    print(f"Image size: {len(image)} bytes (0x{len(image):08X})")
    print(f"Image CRC32: 0x{crc32:08X}")


def upgrade(args: argparse.Namespace) -> int:
    try:
        chunk_size = validate_chunk_size(args.chunk_size)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    image_path = Path(args.image).resolve()
    if not image_path.exists():
        print(f"Image not found: {image_path}", file=sys.stderr)
        return 1

    image, crc32 = load_image(image_path)
    print_image_summary(image_path, image, crc32)

    seq = 1
    tool = BootloaderTool(args.port, args.baudrate, args.timeout, args.verbose)
    try:
        if not args.skip_info:
            last_exc = None
            info = None
            for _ in range(args.info_retries):
                try:
                    info = tool.transact(CMD_GET_INFO, seq, b"", timeout_s=args.timeout)
                    break
                except TimeoutError as exc:
                    last_exc = exc
                    time.sleep(0.2)
            if info is None:
                raise last_exc if last_exc is not None else TimeoutError("Timed out waiting for GET_INFO")
            print("GET_INFO:", parse_info_payload(info.payload))
            seq += 1

        if not args.skip_status:
            status = tool.transact(CMD_QUERY_STATUS, seq, b"", timeout_s=args.timeout)
            print("QUERY_STATUS(before):", parse_status_payload(status.payload))
            seq += 1

        start_payload = struct.pack("<III", len(image), crc32, args.target_fw_version)
        start_rsp = tool.transact(CMD_START, seq, start_payload, timeout_s=args.timeout)
        print(f"START: ACK payload={format_hex(start_rsp.payload)}")
        seq += 1

        sent = 0
        total = len(image)
        while sent < total:
            chunk = image[sent: sent + chunk_size]
            payload = struct.pack("<IH", sent, len(chunk)) + chunk
            data_rsp = tool.transact(CMD_DATA, seq, payload, timeout_s=args.timeout)
            if args.verbose:
                print(f"DATA ACK payload={format_hex(data_rsp.payload)}")
            sent += len(chunk)
            seq = (seq + 1) & 0xFF
            if seq == 0:
                seq = 1
            print(f"DATA: {sent}/{total} bytes ({sent * 100 // total}%)")

        end_rsp = tool.transact(CMD_END, seq, b"", timeout_s=max(args.timeout, 5.0))
        print(f"END: ACK payload={format_hex(end_rsp.payload)}")
        seq += 1

        if not args.skip_status:
            status = tool.transact(CMD_QUERY_STATUS, seq, b"", timeout_s=args.timeout)
            print("QUERY_STATUS(after):", parse_status_payload(status.payload))

        print("Upgrade transfer completed. Device should verify image and jump back to App.")
        return 0
    finally:
        tool.close()


def dump_start_frame(args: argparse.Namespace) -> int:
    image_path = Path(args.image).resolve()
    if not image_path.exists():
        print(f"Image not found: {image_path}", file=sys.stderr)
        return 1
    image, crc32 = load_image(image_path)
    payload = struct.pack("<III", len(image), crc32, args.target_fw_version)
    frame = build_frame(CMD_START, args.seq, payload)
    print_image_summary(image_path, image, crc32)
    print(f"START frame (seq=0x{args.seq:02X}): {format_hex(frame)}")
    return 0


def dump_first_data_frame(args: argparse.Namespace) -> int:
    try:
        chunk_size = validate_chunk_size(args.chunk_size)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    image_path = Path(args.image).resolve()
    if not image_path.exists():
        print(f"Image not found: {image_path}", file=sys.stderr)
        return 1
    image = image_path.read_bytes()
    chunk = image[:chunk_size]
    payload = struct.pack("<IH", 0, len(chunk)) + chunk
    frame = build_frame(CMD_DATA, args.seq, payload)
    print(f"First DATA payload bytes: {len(chunk)}")
    print(f"First DATA frame (seq=0x{args.seq:02X}): {format_hex(frame)}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    default_image = Path("STM32/MDK-ARM/Objects/App.bin")
    parser = argparse.ArgumentParser(description="STM32 local bootloader upgrade tool")
    subparsers = parser.add_subparsers(dest="command")

    run_parser = subparsers.add_parser("run", help="Send GET_INFO/START/DATA/END to the bootloader")
    run_parser.add_argument("--port", default="COM6", help="Serial port, default COM6")
    run_parser.add_argument("--baudrate", type=int, default=115200, help="Baudrate, default 115200")
    run_parser.add_argument("--timeout", type=float, default=2.0, help="Read timeout in seconds")
    run_parser.add_argument(
        "--chunk-size",
        type=int,
        default=MAX_DATA_CHUNK_SIZE,
        help=f"DATA chunk size in bytes, default {MAX_DATA_CHUNK_SIZE} (max {MAX_DATA_CHUNK_SIZE})",
    )
    run_parser.add_argument("--image", default=str(default_image), help="Path to App.bin")
    run_parser.add_argument("--target-fw-version", type=lambda x: int(x, 0), default=0, help="u32 target fw version")
    run_parser.add_argument("--info-retries", type=int, default=3, help="Retry count for initial GET_INFO")
    run_parser.add_argument("--skip-info", action="store_true", help="Skip GET_INFO before upgrade")
    run_parser.add_argument("--skip-status", action="store_true", help="Skip QUERY_STATUS before/after upgrade")
    run_parser.add_argument("--verbose", action="store_true", help="Print full TX/RX frames")
    run_parser.set_defaults(func=upgrade)

    start_parser = subparsers.add_parser("dump-start", help="Print the START frame for the current App.bin")
    start_parser.add_argument("--image", default=str(default_image), help="Path to App.bin")
    start_parser.add_argument("--target-fw-version", type=lambda x: int(x, 0), default=0, help="u32 target fw version")
    start_parser.add_argument("--seq", type=lambda x: int(x, 0), default=0x02, help="Sequence number")
    start_parser.set_defaults(func=dump_start_frame)

    data_parser = subparsers.add_parser("dump-first-data", help="Print the first DATA frame for manual testing")
    data_parser.add_argument("--image", default=str(default_image), help="Path to App.bin")
    data_parser.add_argument(
        "--chunk-size",
        type=int,
        default=128,
        help=f"First DATA chunk size, max {MAX_DATA_CHUNK_SIZE}",
    )
    data_parser.add_argument("--seq", type=lambda x: int(x, 0), default=0x03, help="Sequence number")
    data_parser.set_defaults(func=dump_first_data_frame)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if not getattr(args, "command", None):
        args = parser.parse_args(["run"])
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
