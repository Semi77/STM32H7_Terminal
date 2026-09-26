"""从Keil生成的Bootloader AXF导出BIN和第二Bank的HEX镜像。"""

import argparse
from pathlib import Path
import subprocess

from build_ab_factory import BANK2_BASE, BOOT_SIZE, check_boot, write_hex_segments


def build(fromelf, axf, raw, bank2_hex):
    """校验引导程序后复制到第二Bank，axf为链接输出，raw和bank2_hex为输出路径。"""
    raw = Path(raw)
    bank2_hex = Path(bank2_hex)
    raw_temporary = raw.with_suffix(".build.bin")
    hex_temporary = bank2_hex.with_suffix(".build.hex")
    bank2_hex.unlink(missing_ok=True)
    try:
        subprocess.run([fromelf, "--bin", "--output=" + str(raw_temporary), str(axf)], check=True)
        data = raw_temporary.read_bytes()
        check_boot(data)
        write_hex_segments(((BANK2_BASE, data.ljust(BOOT_SIZE, b"\xFF")),), hex_temporary)
        raw_temporary.replace(raw)
        hex_temporary.replace(bank2_hex)
        print(f"Bank2 Bootloader: {bank2_hex} ({len(data)} bytes)")
    finally:
        raw_temporary.unlink(missing_ok=True)
        hex_temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fromelf", required=True)
    parser.add_argument("--axf", required=True)
    parser.add_argument("--bin", required=True)
    parser.add_argument("--bank2-hex", required=True)
    args = parser.parse_args()
    build(args.fromelf, args.axf, args.bin, args.bank2_hex)
