"""核对工厂HEX的完整地址、校验和、两份Boot和应用内容。"""
import argparse
from pathlib import Path

from build_ab_factory import BANK_SIZE, BOOT_SIZE, FLASH_BASE, BANK2_BASE


def verify(hex_path, boot_path, app_path):
    """重建1 MiB镜像并检查两个Bank的预期内容。"""
    image = bytearray(BANK_SIZE * 2)
    visited = bytearray(len(image))
    upper = 0
    ended = False
    for line in Path(hex_path).read_text(encoding="ascii").splitlines():
        assert line.startswith(":")
        record = bytes.fromhex(line[1:])
        assert len(record) == record[0] + 5 and sum(record) & 255 == 0
        address = int.from_bytes(record[1:3], "big")
        kind = record[3]
        data = record[4:-1]
        if kind == 4:
            assert len(data) == 2
            upper = int.from_bytes(data, "big")
        elif kind == 0:
            absolute = (upper << 16) + address
            if FLASH_BASE <= absolute < FLASH_BASE + BANK_SIZE:
                offset = absolute - FLASH_BASE
                assert offset + len(data) <= BANK_SIZE
            elif BANK2_BASE <= absolute < BANK2_BASE + BANK_SIZE:
                offset = BANK_SIZE + absolute - BANK2_BASE
                assert offset + len(data) <= len(image)
            else:
                raise AssertionError(f"HEX地址落在无效区域: 0x{absolute:08X}")
            assert not any(visited[offset:offset + len(data)])
            image[offset:offset + len(data)] = data
            visited[offset:offset + len(data)] = b"\x01" * len(data)
        elif kind == 1:
            assert not data
            ended = True
        else:
            raise AssertionError(f"意外HEX记录类型: {kind}")
    assert ended and all(visited)
    boot = Path(boot_path).read_bytes()
    app = Path(app_path).read_bytes()
    expected_boot = boot.ljust(BOOT_SIZE, b"\xFF")
    assert image[:BOOT_SIZE] == expected_boot
    assert image[BANK_SIZE:BANK_SIZE + BOOT_SIZE] == expected_boot
    assert image[BOOT_SIZE:BOOT_SIZE + len(app)] == app
    assert all(byte == 255 for byte in image[BOOT_SIZE + len(app):BANK_SIZE])
    assert all(byte == 255 for byte in image[BANK_SIZE + BOOT_SIZE:])
    print("工厂HEX校验通过：完整1 MiB、两份引导一致、应用地址和空白区正确")


def verify_boot_bank2(hex_path, boot_path):
    """确认独立Bank2引导HEX只覆盖0x08100000起的完整首扇区。"""
    data = bytearray(BOOT_SIZE)
    visited = bytearray(BOOT_SIZE)
    upper = 0
    ended = False
    for line in Path(hex_path).read_text(encoding="ascii").splitlines():
        assert line.startswith(":")
        record = bytes.fromhex(line[1:])
        assert len(record) == record[0] + 5 and sum(record) & 255 == 0
        kind = record[3]
        payload = record[4:-1]
        if kind == 4:
            assert len(payload) == 2
            upper = int.from_bytes(payload, "big")
        elif kind == 0:
            address = (upper << 16) + int.from_bytes(record[1:3], "big")
            offset = address - BANK2_BASE
            assert 0 <= offset <= BOOT_SIZE - len(payload)
            assert not any(visited[offset:offset + len(payload)])
            data[offset:offset + len(payload)] = payload
            visited[offset:offset + len(payload)] = b"\x01" * len(payload)
        elif kind == 1:
            assert not payload
            ended = True
        elif kind == 5:
            assert len(payload) == 4
        else:
            raise AssertionError(f"意外HEX记录类型: {kind}")
    assert ended and all(visited)
    assert data == Path(boot_path).read_bytes().ljust(BOOT_SIZE, b"\xFF")
    print("独立Bank2引导HEX校验通过：地址0x08100000，内容与Bank1相同")


def verify_source_hex(hex_path, base, binary_path):
    """检查Keil生成的源HEX只覆盖指定地址并与原始BIN完全一致。"""
    binary = Path(binary_path).read_bytes()
    image = bytearray(len(binary))
    visited = bytearray(len(binary))
    upper = 0
    ended = False
    for line in Path(hex_path).read_text(encoding="ascii").splitlines():
        assert line.startswith(":")
        record = bytes.fromhex(line[1:])
        assert len(record) == record[0] + 5 and sum(record) & 255 == 0
        kind = record[3]
        payload = record[4:-1]
        if kind == 4:
            assert len(payload) == 2
            upper = int.from_bytes(payload, "big")
        elif kind == 0:
            address = (upper << 16) + int.from_bytes(record[1:3], "big")
            offset = address - base
            assert 0 <= offset <= len(binary) - len(payload)
            assert not any(visited[offset:offset + len(payload)])
            image[offset:offset + len(payload)] = payload
            visited[offset:offset + len(payload)] = b"\x01" * len(payload)
        elif kind == 1:
            assert not payload
            ended = True
        elif kind == 5:
            assert len(payload) == 4
        else:
            raise AssertionError(f"意外HEX记录类型: {kind}")
    assert ended and image == binary
    assert all(visited[i] or binary[i] == 255 for i in range(len(binary)))
    print(f"Keil源HEX校验通过：0x{base:08X}，{len(binary)}字节")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hex", required=True)
    parser.add_argument("--boot-bin", required=True)
    parser.add_argument("--app-bin", required=True)
    parser.add_argument("--boot-bank2-hex")
    parser.add_argument("--boot-bank1-hex")
    parser.add_argument("--app-hex")
    args = parser.parse_args()
    verify(args.hex, args.boot_bin, args.app_bin)
    if args.boot_bank2_hex:
        verify_boot_bank2(args.boot_bank2_hex, args.boot_bin)
    if args.boot_bank1_hex:
        verify_source_hex(args.boot_bank1_hex, FLASH_BASE, args.boot_bin)
    if args.app_hex:
        verify_source_hex(args.app_hex, FLASH_BASE + BOOT_SIZE, args.app_bin)
