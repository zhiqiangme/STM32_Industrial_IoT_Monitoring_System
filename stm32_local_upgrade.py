#!/usr/bin/env python3
r"""
STM32 local bootloader upgrade tool.

Current local upgrade path:
- App stage: use Modbus RTU to request entry into Bootloader
- Bootloader stage: use YMODEM over USART3/RS485

Default parameters:
- Port: COM6
- Baudrate: 115200
- Image: STM32\MDK-ARM\Objects\App.bin

Requires:
    pip install pyserial
"""

from __future__ import annotations

import argparse
import sys
import time
import zlib
from pathlib import Path

try:
    import serial
except ModuleNotFoundError:
    print("Missing dependency: pyserial. Install with: pip install pyserial", file=sys.stderr)
    raise


SOH = 0x01
STX = 0x02
EOT = 0x04
ACK = 0x06
NAK = 0x15
CA = 0x18
CRC16 = 0x43

ABORT1 = 0x41
ABORT2 = 0x61

PACKET_SIZE = 128
PACKET_1K_SIZE = 1024
PACKET_OVERHEAD = 3 + 2
MAX_RETRIES = 10
CPM_EOF = 0x1A


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def format_hex(data: bytes) -> str:
    return data.hex(" ").upper()


def build_packet(packet_no: int, payload: bytes, packet_type: int, pad_byte: int | None = None) -> bytes:
    if packet_type == STX:
        packet_size = PACKET_1K_SIZE
    elif packet_type == SOH:
        packet_size = PACKET_SIZE
    else:
        raise ValueError(f"unsupported packet type: 0x{packet_type:02X}")

    if len(payload) > packet_size:
        raise ValueError(f"payload too large for packet: {len(payload)} > {packet_size}")

    if pad_byte is None:
        pad_byte = CPM_EOF
    padded = payload.ljust(packet_size, bytes([pad_byte]))

    header = bytes([packet_type, packet_no & 0xFF, (~packet_no) & 0xFF])
    crc = crc16_xmodem(padded)
    return header + padded + bytes([(crc >> 8) & 0xFF, crc & 0xFF])


def build_initial_packet(file_name: str, file_size: int, crc32: int, target_fw_version: int) -> bytes:
    safe_name = Path(file_name).name
    metadata = f"{file_size} 0x{crc32:08X} {target_fw_version}"
    payload = safe_name.encode("ascii", errors="ignore") + b"\x00" + metadata.encode("ascii") + b"\x00"
    return build_packet(0, payload, SOH, pad_byte=0x00)


def build_final_packet() -> bytes:
    return build_packet(0, b"", SOH, pad_byte=0x00)


def choose_data_block(remaining: int) -> int:
    return PACKET_1K_SIZE if remaining >= PACKET_1K_SIZE else PACKET_SIZE


class YmodemSender:
    def __init__(self, port: str, baudrate: int, timeout: float, verbose: bool) -> None:
        self.ser = serial.Serial(
            port=port,
            baudrate=baudrate,
            bytesize=8,
            parity="N",
            stopbits=1,
            timeout=0.1,
            write_timeout=1.0,
            inter_byte_timeout=0.02,
        )
        self.timeout = timeout
        self.verbose = verbose
        time.sleep(0.5)
        self.reset_input()

    def close(self) -> None:
        if self.ser.is_open:
            self.ser.close()

    def reset_input(self) -> None:
        self.ser.reset_input_buffer()

    def send(self, data: bytes) -> None:
        if self.verbose:
            print(f"TX: {format_hex(data)}")
        self.ser.write(data)
        self.ser.flush()

    def _read_byte(self, timeout_s: float) -> int:
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            chunk = self.ser.read(1)
            if chunk:
                value = chunk[0]
                if self.verbose:
                    print(f"RX: {value:02X}")
                return value
        raise TimeoutError("Timed out waiting for bootloader control byte")

    def _read_control(self, timeout_s: float, accepted: set[int]) -> int:
        while True:
            value = self._read_byte(timeout_s)
            if value in accepted:
                return value

    def wait_receiver_ready(self) -> None:
        print("Waiting for bootloader YMODEM handshake ('C')...")
        while True:
            try:
                value = self._read_byte(self.timeout)
            except TimeoutError:
                continue
            if value == CRC16:
                return
            if value == CA:
                other = self._read_byte(0.5)
                if other == CA:
                    raise RuntimeError("Bootloader aborted the YMODEM session")

    def _wait_ack(self, *, expect_crc_request: bool = False) -> None:
        for _ in range(MAX_RETRIES):
            value = self._read_control(self.timeout, {ACK, NAK, CA})
            if value == ACK:
                if expect_crc_request:
                    try:
                        follow = self._read_control(0.5, {CRC16, ACK, NAK, CA})
                        if follow == CRC16:
                            return
                        if follow == ACK:
                            return
                        if follow == NAK:
                            continue
                        if follow == CA:
                            other = self._read_byte(0.5)
                            if other == CA:
                                raise RuntimeError("Bootloader aborted the YMODEM session")
                    except TimeoutError:
                        return
                return

            if value == NAK:
                raise RuntimeError("Bootloader requested retransmission")

            other = self._read_byte(0.5)
            if other == CA:
                raise RuntimeError("Bootloader aborted the YMODEM session")

        raise RuntimeError("Too many retries waiting for ACK")

    def _send_packet_with_retry(self, packet: bytes, *, expect_crc_request: bool = False) -> None:
        last_error: Exception | None = None
        for _ in range(MAX_RETRIES):
            try:
                self.send(packet)
                self._wait_ack(expect_crc_request=expect_crc_request)
                return
            except (RuntimeError, TimeoutError) as exc:
                last_error = exc
                if "aborted" in str(exc).lower():
                    raise
        raise RuntimeError(str(last_error) if last_error is not None else "YMODEM packet transfer failed")

    def send_file(self, image_path: Path, image: bytes, crc32: int, target_fw_version: int) -> None:
        self.wait_receiver_ready()

        header_packet = build_initial_packet(image_path.name, len(image), crc32, target_fw_version)
        self._send_packet_with_retry(header_packet, expect_crc_request=True)
        print("YMODEM header accepted.")

        packet_no = 1
        sent = 0
        total = len(image)
        while sent < total:
            packet_size = choose_data_block(total - sent)
            chunk = image[sent : sent + packet_size]
            packet_type = STX if packet_size == PACKET_1K_SIZE else SOH
            packet = build_packet(packet_no, chunk, packet_type)
            self._send_packet_with_retry(packet)

            sent += len(chunk)
            packet_no = (packet_no + 1) & 0xFF
            print(f"DATA: {sent}/{total} bytes ({sent * 100 // total}%)")

        eot_sent = False
        for _ in range(MAX_RETRIES):
            self.send(bytes([EOT]))
            try:
                value = self._read_control(self.timeout, {ACK, NAK, CA})
            except TimeoutError:
                continue
            if value == ACK:
                eot_sent = True
                break
            if value == NAK:
                continue
            other = self._read_byte(0.5)
            if other == CA:
                raise RuntimeError("Bootloader aborted the YMODEM session")
        if not eot_sent:
            raise RuntimeError("Bootloader did not ACK EOT")
        print("EOT accepted.")

        final_packet = build_final_packet()
        self._send_packet_with_retry(final_packet)
        print("Final empty packet accepted.")


def load_image(image_path: Path) -> tuple[bytes, int]:
    image = image_path.read_bytes()
    crc32 = zlib.crc32(image) & 0xFFFFFFFF
    return image, crc32


def print_image_summary(image_path: Path, image: bytes, crc32: int) -> None:
    print(f"Image: {image_path}")
    print(f"Image size: {len(image)} bytes (0x{len(image):08X})")
    print(f"Image CRC32: 0x{crc32:08X}")


def upgrade(args: argparse.Namespace) -> int:
    image_path = Path(args.image).resolve()
    if not image_path.exists():
        print(f"Image not found: {image_path}", file=sys.stderr)
        return 1

    image, crc32 = load_image(image_path)
    print_image_summary(image_path, image, crc32)
    if args.chunk_size is not None:
        print("Note: --chunk-size is ignored in YMODEM mode; packet size is chosen automatically (128/1024 bytes).")

    sender = YmodemSender(args.port, args.baudrate, args.timeout, args.verbose)
    try:
        sender.send_file(image_path, image, crc32, args.target_fw_version)
    finally:
        sender.close()

    print("Upgrade transfer completed. Device should verify image and reset back to App.")
    return 0


def build_parser() -> argparse.ArgumentParser:
    default_image = Path("STM32/MDK-ARM/Objects/App.bin")
    parser = argparse.ArgumentParser(description="STM32 local bootloader YMODEM upgrade tool")
    subparsers = parser.add_subparsers(dest="command")

    run_parser = subparsers.add_parser("run", help="Send App.bin to the bootloader with YMODEM")
    run_parser.add_argument("--port", default="COM6", help="Serial port, default COM6")
    run_parser.add_argument("--baudrate", type=int, default=115200, help="Baudrate, default 115200")
    run_parser.add_argument("--timeout", type=float, default=5.0, help="Per-step timeout in seconds")
    run_parser.add_argument("--image", default=str(default_image), help="Path to App.bin")
    run_parser.add_argument("--target-fw-version", type=lambda x: int(x, 0), default=0, help="u32 target fw version")
    run_parser.add_argument(
        "--chunk-size",
        type=int,
        default=None,
        help="Deprecated in YMODEM mode; kept for CLI compatibility and ignored",
    )
    run_parser.add_argument("--verbose", action="store_true", help="Print YMODEM TX/RX bytes")
    run_parser.set_defaults(func=upgrade)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if not getattr(args, "command", None):
        args = parser.parse_args(["run"])
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
