"""通过本地HTTP服务验证单文件上位机上传、丢失确认、错误回复和取消。"""
import json
import struct
import tempfile
import threading
import unittest
import zlib
import sys
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from h7_wifi_tool import GatewayClient
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "shared"))
from package_firmware import package


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        """测试时关闭HTTP访问日志。"""

    def reply(self, data, status=200):
        """发送data字节及HTTP状态。"""
        self.send_response(status)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        """提供上传功能版本，供客户端识别设备能力。"""
        self.reply(json.dumps({"device":"h7-gateway", "api_version":1,
                               "ota_download_version":1, "ota_install_version":1}).encode())

    def do_POST(self):
        """模拟桥接与STM32确认，按测试要求注入丢包和校验错误。"""
        raw = self.rfile.read(int(self.headers["Content-Length"]))
        state = self.server.state
        if self.path == "/api/command":
            cmd = json.loads(raw)["command"]
            state["boot"] += 1
            return self.reply(json.dumps({"ok":True, "target":"stm32", "command":cmd,
                                          "reply":"APP_STARTING" if cmd=="App" else "BOOT_READY"}).encode())
        magic, cmd, sid, offset, length = struct.unpack_from("<5I", raw)
        assert magic == 0x544F3748 and len(raw) == 24 + length
        assert zlib.crc32(raw[:-4]) == struct.unpack_from("<I", raw, len(raw)-4)[0]
        payload = raw[20:-4]
        state["commands"].append(cmd)
        status = 0
        if cmd == 1:
            state["size"], state["crc"] = struct.unpack_from("<II", payload)
            state["data"] = bytearray()
        elif cmd == 2:
            if offset == len(state["data"]): state["data"].extend(payload)
            else: assert state["data"][offset:offset+length] == payload
            if state["drop"]:
                state["drop"] = False
                return self.reply(b"lost ACK", 504)
        elif cmd == 3:
            assert zlib.crc32(state["data"]) == state["crc"]
            if state["bad_image_crc"]: status = 5
        elif cmd == 4:
            state["aborted"] = True
        elif cmd == 6:
            size, checksum = struct.unpack("<II", payload)
            if size != len(state["data"]) or checksum != zlib.crc32(state["data"]):
                status = 6
            elif state.get("drop_install"):
                state["drop_install"] = False
                return self.reply(b"lost install ACK", 504)
        next_offset = 0 if cmd == 4 else len(state["data"])
        packed = struct.pack("<5I", sid, cmd, offset, next_offset, status)
        crc = zlib.crc32(packed)
        if state["bad_reply"]: crc ^= 1
        self.reply(f"OTA:{sid:08X}:{cmd}:{offset}:{next_offset}:{status}:{crc:08X}".encode())


class UploadTests(unittest.TestCase):
    def setUp(self):
        """构造包含二进制换行、零字节和非整包尾部的应用镜像。"""
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name) / "app.bin"
        self.data = struct.pack("<II", 0x24020000, 0x08020009) + bytes(range(256))*10 + b"\x00\n\xff"
        self.path.write_bytes(package(self.data))
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.server.state = dict(boot=0, commands=[], drop=False, bad_reply=False,
                                 bad_image_crc=False, data=bytearray(), aborted=False)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.client = GatewayClient(f"127.0.0.1:{self.server.server_port}")
        self.cancel = threading.Event()

    def tearDown(self):
        """关闭模拟服务并清理临时固件。"""
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.tmp.cleanup()

    def test_upload_and_lost_ack(self):
        self.server.state["drop"] = True
        stages = []
        result = self.client.upload_bin(self.path, lambda *args: stages.append(args), self.cancel)
        self.assertFalse(result["installed"])
        self.assertEqual(self.server.state["boot"], 1)
        self.assertEqual(self.server.state["data"], self.data)
        self.assertEqual(self.server.state["commands"].count(2), 4)

    def test_start_application_uses_command_endpoint(self):
        """启动应用必须走/api/command并被识别为stm32目标，不产生任何OTA帧。"""
        result = self.client.command("App")
        self.assertEqual(result["command"], "App")
        self.assertEqual(result["target"], "stm32")
        self.assertEqual(self.server.state["boot"], 1)
        self.assertEqual(self.server.state["commands"], [])

    def test_bad_image_rejected_before_boot(self):
        for data in (b"bad", struct.pack("<II", 0x24020000, 0x08000009)+bytes(100),
                     struct.pack("<II", 0x24020000, 0x08100009)+bytes(100), bytes(0x60001)):
            self.path.write_bytes(data)
            with self.assertRaises(ValueError):
                self.client.upload_bin(self.path, lambda *_: None, self.cancel)
        self.assertEqual(self.server.state["boot"], 0)

    def test_truncated_or_corrupt_package_rejected_before_boot(self):
        """拒绝尾部截断、正文缺字节、内容修改和未封装BIN。"""
        packed = package(self.data)
        variants = [packed[:-1], packed[:-1024], packed[:-33]+packed[-32:],
                    packed[:100]+bytes([packed[100]^1])+packed[101:], self.data]
        for data in variants:
            self.path.write_bytes(data)
            with self.assertRaises(ValueError):
                self.client.upload_bin(self.path, lambda *_: None, self.cancel)
        self.assertEqual(self.server.state["boot"], 0)

    def test_crc_failure_aborts(self):
        self.server.state["bad_image_crc"] = True
        with self.assertRaisesRegex(ValueError, "CRC"):
            self.client.upload_bin(self.path, lambda *_: None, self.cancel)
        self.assertTrue(self.server.state["aborted"])

    def test_corrupt_reply_not_accepted(self):
        self.server.state["bad_reply"] = True
        with self.assertRaisesRegex(ValueError, "3次"):
            self.client.ota_frame(1, 123, payload=struct.pack("<II", len(self.data), zlib.crc32(self.data))+self.data[:8])

    def test_cancel_between_blocks(self):
        def progress(stage, done, total):
            if done: self.cancel.set()
        with self.assertRaisesRegex(ValueError, "取消"):
            self.client.upload_bin(self.path, progress, self.cancel)
        self.assertTrue(self.server.state["aborted"])
        self.assertNotIn(3, self.server.state["commands"])

    def test_install_is_explicit_and_retries_lost_ack(self):
        """下载不自动安装，点击安装后丢失回复可以重入引导页并确认同一镜像。"""
        self.client.upload_bin(self.path, lambda *_: None, self.cancel)
        self.assertNotIn(6, self.server.state["commands"])
        self.server.state["drop_install"] = True
        result = self.client.install_bin(self.path, lambda *_: None)
        self.assertTrue(result["installed"])
        self.assertEqual(self.server.state["commands"].count(6), 2)
        self.assertEqual(self.server.state["commands"].count(1), 1)
        self.assertEqual(self.server.state["boot"], 3)

    def test_install_wrong_file_rejected(self):
        """安装时只比较所选文件，不重新上传或覆盖已有下载槽。"""
        self.client.upload_bin(self.path, lambda *_: None, self.cancel)
        self.path.write_bytes(package(self.data + b"different"))
        with self.assertRaisesRegex(ValueError, "不匹配"):
            self.client.install_bin(self.path, lambda *_: None)
        self.assertEqual(self.server.state["data"], self.data)
        self.assertFalse(self.server.state["aborted"])


if __name__ == "__main__":
    unittest.main()
