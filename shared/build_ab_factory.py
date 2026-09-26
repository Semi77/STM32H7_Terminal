"""生成覆盖两份相同Bootloader和工厂应用的1 MiB Intel HEX烧录文件。"""
import argparse
from pathlib import Path
import struct

from package_firmware import BASE, MAX_SIZE, package

BANK_SIZE = 0x80000
BOOT_SIZE = 0x20000
FLASH_BASE = 0x08000000
BANK2_BASE = 0x08100000


def check_boot(data):
    """校验Bootloader向量和大小，防止把应用错烧进引导扇区。"""
    if not 8 <= len(data) <= BOOT_SIZE:
        raise ValueError("Bootloader长度必须为8字节至128 KiB")
    sp, pc = struct.unpack_from("<II", data)
    if (sp & 7 or not (0x20000000 < sp <= 0x20020000
                     or 0x24000000 < sp <= 0x24080000)
            or not pc & 1 or not FLASH_BASE <= (pc & ~1) < FLASH_BASE + len(data)):
        raise ValueError("Bootloader必须链接到0x08000000")


def hex_record(address, kind, payload):
    """把地址、记录类型和载荷编码成一条带校验和的Intel HEX记录。"""
    body = bytes((len(payload), address >> 8, address & 255, kind)) + payload
    return ":" + (body + bytes((-sum(body) & 255,))).hex().upper() + "\n"


def write_hex_segments(segments, output):
    """按实际非连续Bank地址写出带完整校验和的Intel HEX文件。"""
    output = Path(output)
    with output.open("w", encoding="ascii", newline="\n") as out:
        upper = -1
        for base, data in segments:
            for offset in range(0, len(data), 16):
                address = base + offset
                if address >> 16 != upper:
                    upper = address >> 16
                    out.write(hex_record(0, 4, upper.to_bytes(2, "big")))
                out.write(hex_record(address & 0xFFFF, 0, data[offset:offset + 16]))
        out.write(hex_record(0, 1, b""))


def build(boot, app, output, boot_bank2_output=None):
    """将Boot复制到两侧Bank，并把App放在低Bank的128 KiB偏移。"""
    boot = Path(boot).read_bytes()
    app = Path(app).read_bytes()
    check_boot(boot)
    package(app)
    boot_sector = boot.ljust(BOOT_SIZE, b"\xFF")
    bank1 = boot_sector + app.ljust(BANK_SIZE - BOOT_SIZE, b"\xFF")
    bank2 = boot_sector + b"\xFF" * (BANK_SIZE - BOOT_SIZE)
    write_hex_segments(((FLASH_BASE, bank1), (BANK2_BASE, bank2)), output)
    if boot_bank2_output is not None:
        write_hex_segments(((BANK2_BASE, boot_sector),), boot_bank2_output)
    print(f"工厂镜像: {output}；Boot两份各{len(boot)}字节；App {len(app)}字节/{MAX_SIZE}字节")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boot-bin", required=True)
    parser.add_argument("--app-bin", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--boot-bank2-output")
    args = parser.parse_args()
    build(args.boot_bin, args.app_bin, args.output, args.boot_bank2_output)
