#!/usr/bin/env python3
r"""
STM32 remote bootloader upgrade tool over a TCP byte stream.

Typical stage-3 deployment:
- G780S works in NET transparent mode
- G780S maintains a TCP connection with the upgrade server
- This tool runs on the server side and talks to STM32 through that socket

The bootloader protocol is unchanged from stm32_local_upgrade.py.
Only the transport changes from serial to socket.
"""

from __future__ import annotations

import argparse
import socket
import struct
import sys
import time
import zlib
from dataclasses import dataclass
from pathlib import Path


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

MODBUS_FUNC_WRITE_SINGLE = 0x06

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


def build_boot_frame(cmd: int, seq: int, payload: bytes = b"") -> bytes:
    header = struct.pack("<BBH", cmd, seq & 0xFF, len(payload))
    crc = crc16_modbus(header + payload)
    return SOF + header + payload + struct.pack("<H", crc)


def build_modbus_write_single(slave_addr: int, reg_addr: int, reg_value: int) -> bytes:
    body = struct.pack(">BBHH", slave_addr & 0xFF, MODBUS_FUNC_WRITE_SINGLE, reg_addr & 0xFFFF, reg_value & 0xFFFF)
    crc = crc16_modbus(body)
    return body + struct.pack("<H", crc)


@dataclass
class BootFrame:
    cmd: int
    seq: int
    payload: bytes
    raw: bytes


class SocketTransport:
    def __init__(
        self,
        mode: str,
        host: str,
        port: int,
        connect_timeout: float,
        recv_timeout: float,
        verbose: bool,
    ) -> None:
        self.mode = mode
        self.host = host
        self.port = port
        self.connect_timeout = connect_timeout
        self.recv_timeout = recv_timeout
        self.verbose = verbose
        self.server_sock: socket.socket | None = None
        self.sock: socket.socket | None = None
        self.peer_desc = ""
        self.buffer = bytearray()

    def open(self) -> None:
        if self.mode == "listen":
            self._open_listen()
        elif self.mode == "connect":
            self._open_connect()
        else:
            raise ValueError(f"Unsupported mode: {self.mode}")

    def _open_listen(self) -> None:
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((self.host, self.port))
        server.listen(1)
        self.server_sock = server

        print(f"Listening for G780S connection on {self.host}:{self.port} ...")
        server.settimeout(self.connect_timeout if self.connect_timeout > 0 else None)
        conn, addr = server.accept()
        conn.settimeout(self.recv_timeout)
        self.sock = conn
        self.peer_desc = f"{addr[0]}:{addr[1]}"
        print(f"Accepted connection from {self.peer_desc}")

    def _open_connect(self) -> None:
        if not self.host:
            raise ValueError("connect mode requires --host")

        sock = socket.create_connection((self.host, self.port), timeout=self.connect_timeout)
        sock.settimeout(self.recv_timeout)
        self.sock = sock
        self.peer_desc = f"{self.host}:{self.port}"
        print(f"Connected to {self.peer_desc}")

    def close(self) -> None:
        if self.sock is not None:
            self.sock.close()
            self.sock = None
        if self.server_sock is not None:
            self.server_sock.close()
            self.server_sock = None

    def reset_buffer(self) -> None:
        self.buffer.clear()

    def send(self, data: bytes) -> None:
        if self.sock is None:
            raise RuntimeError("Socket is not open")
        if self.verbose:
            print(f"TX: {format_hex(data)}")
        self.sock.sendall(data)

    def _recv_some(self) -> bytes:
        if self.sock is None:
            raise RuntimeError("Socket is not open")
        try:
            chunk = self.sock.recv(4096)
        except socket.timeout:
            return b""
        if chunk == b"":
            raise ConnectionError("Socket closed by peer")
        return chunk

    def _extract_boot_frame(self) -> BootFrame | None:
        while True:
            sof_idx = self.buffer.find(SOF)
            if sof_idx < 0:
                self.buffer.clear()
                return None
            if sof_idx > 0:
                del self.buffer[:sof_idx]
            if len(self.buffer) < 6:
                return None

            cmd = self.buffer[2]
            seq = self.buffer[3]
            payload_len = struct.unpack_from("<H", self.buffer, 4)[0]
            frame_len = 2 + 1 + 1 + 2 + payload_len + 2
            if len(self.buffer) < frame_len:
                return None

            raw = bytes(self.buffer[:frame_len])
            del self.buffer[:frame_len]

            frame_crc = struct.unpack_from("<H", raw, frame_len - 2)[0]
            calc_crc = crc16_modbus(raw[2:-2])
            if frame_crc != calc_crc:
                print(
                    f"Ignore bad boot CRC frame: got=0x{frame_crc:04X}, expect=0x{calc_crc:04X}, raw={format_hex(raw)}",
                    file=sys.stderr,
                )
                continue

            payload = raw[6:-2]
            return BootFrame(cmd=cmd, seq=seq, payload=payload, raw=raw)

    def read_boot_frame(self, timeout_s: float) -> BootFrame:
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            frame = self._extract_boot_frame()
            if frame is not None:
                if self.verbose:
                    print(f"RX: {format_hex(frame.raw)}")
                return frame

            chunk = self._recv_some()
            if chunk:
                self.buffer.extend(chunk)
            else:
                time.sleep(0.02)

        raise TimeoutError("Timed out waiting for bootloader response")

    def transact_boot(self, cmd: int, seq: int, payload: bytes = b"", timeout_s: float = 2.0) -> BootFrame:
        frame = build_boot_frame(cmd, seq, payload)
        self.reset_buffer()
        self.send(frame)
        response = self.read_boot_frame(timeout_s)

        if response.cmd == RSP_NACK:
            err = struct.unpack("<H", response.payload[:2])[0] if len(response.payload) >= 2 else 0xFFFF
            err_name = BOOT_ERR_NAMES.get(err, "UNKNOWN")
            raise RuntimeError(f"Bootloader NACK: 0x{err:04X} ({err_name})")

        if response.seq != (seq & 0xFF):
            raise RuntimeError(f"Unexpected sequence in response: expect 0x{seq:02X}, got 0x{response.seq:02X}")

        return response

    def _extract_modbus_write_response(self, slave_addr: int, function_code: int) -> bytes | None:
        scan_idx = 0

        while scan_idx < len(self.buffer):
            if self.buffer[scan_idx] != (slave_addr & 0xFF):
                scan_idx += 1
                continue

            remaining = len(self.buffer) - scan_idx
            if remaining < 5:
                break

            func = self.buffer[scan_idx + 1]
            if func == (function_code | 0x80):
                frame_len = 5
            elif func == function_code:
                frame_len = 8
            else:
                scan_idx += 1
                continue

            if remaining < frame_len:
                break

            raw = bytes(self.buffer[scan_idx:scan_idx + frame_len])
            del self.buffer[:scan_idx + frame_len]

            frame_crc = struct.unpack_from("<H", raw, frame_len - 2)[0]
            calc_crc = crc16_modbus(raw[:-2])
            if frame_crc != calc_crc:
                print(
                    f"Ignore bad Modbus CRC frame: got=0x{frame_crc:04X}, expect=0x{calc_crc:04X}, raw={format_hex(raw)}",
                    file=sys.stderr,
                )
                scan_idx = 0
                continue

            return raw

        if scan_idx > 0:
            del self.buffer[:scan_idx]

        return None

    def transact_modbus_write_single(
        self,
        slave_addr: int,
        reg_addr: int,
        reg_value: int,
        timeout_s: float,
    ) -> bytes:
        request = build_modbus_write_single(slave_addr, reg_addr, reg_value)
        self.reset_buffer()
        self.send(request)

        deadline = time.time() + timeout_s
        while time.time() < deadline:
            response = self._extract_modbus_write_response(slave_addr, MODBUS_FUNC_WRITE_SINGLE)
            if response is not None:
                if self.verbose:
                    print(f"RX: {format_hex(response)}")
                func = response[1]
                if func == (MODBUS_FUNC_WRITE_SINGLE | 0x80):
                    exc = response[2]
                    raise RuntimeError(
                        f"Modbus exception: slave=0x{slave_addr:02X}, func=0x{func:02X}, exception=0x{exc:02X}"
                    )
                return response

            chunk = self._recv_some()
            if chunk:
                self.buffer.extend(chunk)
            else:
                time.sleep(0.02)

        raise TimeoutError(
            f"Timed out waiting for Modbus write response: reg=0x{reg_addr:04X}, value=0x{reg_value:04X}"
        )


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


def enter_bootloader_via_app_stage(tool: SocketTransport, args: argparse.Namespace) -> None:
    print("App stage: unlock device ...")
    rsp = tool.transact_modbus_write_single(args.slave_addr, 0x0030, args.unlock_key, timeout_s=args.timeout)
    print(f"UNLOCK: {format_hex(rsp)}")

    print("App stage: request Bootloader ...")
    rsp = tool.transact_modbus_write_single(args.slave_addr, 0x0031, args.boot_command, timeout_s=args.timeout)
    print(f"ENTER_BOOT_UPGRADE: {format_hex(rsp)}")

    print(f"Waiting {args.boot_wait:.1f}s for App reset -> Bootloader ...")
    time.sleep(args.boot_wait)


def run_upgrade(args: argparse.Namespace) -> int:
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

    if args.mode == "connect" and not args.host:
        print("--host is required when --mode=connect", file=sys.stderr)
        return 1

    seq = 1
    tool = SocketTransport(
        mode=args.mode,
        host=args.host,
        port=args.port,
        connect_timeout=args.connect_timeout,
        recv_timeout=args.recv_timeout,
        verbose=args.verbose,
    )

    try:
        tool.open()

        if args.enter_bootloader:
            enter_bootloader_via_app_stage(tool, args)

        if not args.skip_info:
            last_exc: Exception | None = None
            info = None
            for _ in range(args.info_retries):
                try:
                    info = tool.transact_boot(CMD_GET_INFO, seq, b"", timeout_s=args.timeout)
                    break
                except (TimeoutError, ConnectionError) as exc:
                    last_exc = exc
                    time.sleep(0.2)
            if info is None:
                raise last_exc if last_exc is not None else TimeoutError("Timed out waiting for GET_INFO")
            print("GET_INFO:", parse_info_payload(info.payload))
            seq += 1

        if not args.skip_status:
            status = tool.transact_boot(CMD_QUERY_STATUS, seq, b"", timeout_s=args.timeout)
            print("QUERY_STATUS(before):", parse_status_payload(status.payload))
            seq += 1

        start_payload = struct.pack("<III", len(image), crc32, args.target_fw_version)
        start_rsp = tool.transact_boot(CMD_START, seq, start_payload, timeout_s=max(args.timeout, 5.0))
        print(f"START: ACK payload={format_hex(start_rsp.payload)}")
        seq += 1

        sent = 0
        total = len(image)
        while sent < total:
            chunk = image[sent: sent + chunk_size]
            payload = struct.pack("<IH", sent, len(chunk)) + chunk
            data_rsp = tool.transact_boot(CMD_DATA, seq, payload, timeout_s=args.timeout)
            if args.verbose:
                print(f"DATA ACK payload={format_hex(data_rsp.payload)}")
            sent += len(chunk)
            seq = (seq + 1) & 0xFF
            if seq == 0:
                seq = 1
            print(f"DATA: {sent}/{total} bytes ({sent * 100 // total}%)")

        end_rsp = tool.transact_boot(CMD_END, seq, b"", timeout_s=max(args.timeout, 5.0))
        print(f"END: ACK payload={format_hex(end_rsp.payload)}")
        seq += 1

        if not args.skip_status:
            try:
                status = tool.transact_boot(CMD_QUERY_STATUS, seq, b"", timeout_s=args.timeout)
                print("QUERY_STATUS(after):", parse_status_payload(status.payload))
            except (TimeoutError, ConnectionError):
                print("QUERY_STATUS(after): no response, device may already be resetting back to App")

        print("Remote transfer completed.")
        return 0
    except TimeoutError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    except (ConnectionError, OSError, RuntimeError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    finally:
        tool.close()


def build_parser() -> argparse.ArgumentParser:
    default_image = Path("STM32/MDK-ARM/Objects/App.bin")

    parser = argparse.ArgumentParser(description="STM32 remote bootloader upgrade tool over TCP")
    parser.add_argument("--mode", choices=("listen", "connect"), default="listen", help="TCP transport mode")
    parser.add_argument("--host", default="0.0.0.0", help="Bind host in listen mode or peer host in connect mode")
    parser.add_argument("--port", type=int, default=8899, help="Bind/peer TCP port")
    parser.add_argument("--connect-timeout", type=float, default=60.0, help="Connect/accept timeout in seconds")
    parser.add_argument("--recv-timeout", type=float, default=0.2, help="Socket recv timeout in seconds")
    parser.add_argument("--timeout", type=float, default=2.0, help="Protocol response timeout in seconds")
    parser.add_argument(
        "--chunk-size",
        type=int,
        default=128,
        help=f"DATA chunk size in bytes, max {MAX_DATA_CHUNK_SIZE}",
    )
    parser.add_argument("--image", default=str(default_image), help="Path to App.bin")
    parser.add_argument("--target-fw-version", type=lambda x: int(x, 0), default=0, help="u32 target fw version")
    parser.add_argument("--info-retries", type=int, default=5, help="Retry count for initial GET_INFO")
    parser.add_argument("--skip-info", action="store_true", help="Skip GET_INFO before upgrade")
    parser.add_argument("--skip-status", action="store_true", help="Skip QUERY_STATUS before/after upgrade")
    parser.add_argument("--enter-bootloader", action="store_true", help="Send App-stage Modbus commands before upgrade")
    parser.add_argument("--boot-wait", type=float, default=1.5, help="Wait time after App reset request")
    parser.add_argument("--slave-addr", type=lambda x: int(x, 0), default=0x0A, help="App-stage Modbus slave address")
    parser.add_argument("--unlock-key", type=lambda x: int(x, 0), default=0xA55A, help="App-stage unlock key")
    parser.add_argument("--boot-command", type=lambda x: int(x, 0), default=0x0005, help="Enter Bootloader command")
    parser.add_argument("--verbose", action="store_true", help="Print full TX/RX frames")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return run_upgrade(args)


if __name__ == "__main__":
    sys.exit(main())
