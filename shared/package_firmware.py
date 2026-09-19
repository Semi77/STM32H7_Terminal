"""由成功链接的AXF生成原始BIN及带构建期完整性尾部的OTA文件。"""
import argparse
from pathlib import Path
import struct
import subprocess
import zlib

MAGIC = 0x57463748  # 小端H7FW。
BASE = 0x08020000
MAX_SIZE = 0x60000


def package(data):
    """为原始应用data追加32字节构建期描述，CRC覆盖原始文件及描述头。"""
    if not 8 <= len(data) <= MAX_SIZE:
        raise ValueError("应用长度必须为8字节至384 KiB")
    sp, pc = struct.unpack_from("<II", data)
    if (sp & 7 or not (0x20000000 < sp <= 0x20020000 or 0x24000000 < sp <= 0x24080000)
            or not pc & 1 or not BASE <= (pc & ~1) < BASE + len(data)):
        raise ValueError("AXF未链接到H7应用分区")
    header = struct.pack("<7I", MAGIC, 1, BASE, len(data), zlib.crc32(data), 0, 0)
    return data + header + struct.pack("<I", zlib.crc32(header))


def build(fromelf, axf, raw):
    """只对成功解析的axf打包，raw为原始BIN路径；失败时移除旧OTA产物防止误用。"""
    raw = Path(raw)
    ota = raw.with_suffix(".ota.bin")
    temporary = raw.with_suffix(".build.bin")
    ota_temporary = ota.with_suffix(".tmp")
    ota.unlink(missing_ok=True)
    try:
        subprocess.run([fromelf, "--bin", "--output=" + str(temporary), str(axf)], check=True)
        data = temporary.read_bytes()
        ota_temporary.write_bytes(package(data))
        temporary.replace(raw)
        ota_temporary.replace(ota)
        print(f"OTA: {ota} ({len(data)} bytes, CRC32={zlib.crc32(data):08X})")
    finally:
        temporary.unlink(missing_ok=True)
        ota_temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fromelf", required=True)
    parser.add_argument("--axf", required=True)
    parser.add_argument("--bin", required=True)
    args = parser.parse_args()
    build(args.fromelf, args.axf, args.bin)
