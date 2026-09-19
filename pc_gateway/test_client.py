"""使用真实本地HTTP连接验证客户端协议、UTF-8回显和失败处理。"""
import json
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from client import GatewayClient, normalize_address


class Handler(BaseHTTPRequestHandler):
    mode = "normal"
    # 为None时按命令给出正常回复，否则强制用作启动应用命令的reply以测试白名单。
    app_reply = None

    def log_message(self, *_):
        """关闭测试HTTP访问日志。"""

    def respond(self, body, status=200):
        """将body编码为JSON并发送指定状态码。"""
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def do_GET(self):
        """模拟正常状态、错误服务和超时响应。"""
        if self.mode == "slow":
            time.sleep(0.2)
        if self.mode == "error":
            return self.respond({"error": "failed"}, 503)
        self.respond({"device": "wrong" if self.mode == "wrong" else "h7-gateway",
                      "api_version": 1, "has_sample": False})

    def do_POST(self):
        """模拟ESP32命令回复，验证请求体UTF-8传输并区分启动应用命令。"""
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        reply = self.app_reply or ("APP_STARTING" if body["command"] == "App" else
                                   "PONG" if body["command"] == "PING" else body["message"])
        self.respond({"ok": True,
                      "target": "stm32" if body["command"] in ("App", "Bootloader") else "esp32",
                      "command": body["command"], "reply": reply})


class ClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        """启动仅监听本机随机端口的协议模拟服务。"""
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.address = f"127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        """关闭测试服务并等待服务线程退出。"""
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def setUp(self):
        """为每个用例恢复正常服务模式。"""
        Handler.mode = "normal"

    def test_status_and_commands(self):
        client = GatewayClient(self.address)
        self.assertEqual(client.status()["device"], "h7-gateway")
        self.assertEqual(client.command("PING")["reply"], "PONG")
        self.assertEqual(client.command("ECHO", "你好，网关")["reply"], "你好，网关")

    def test_start_application_command(self):
        """启动应用只接受APP_STARTING，必须拒绝周期BOOT_READY。"""
        client = GatewayClient(self.address)
        self.assertEqual(client.command("App")["reply"], "APP_STARTING")
        Handler.app_reply = "BOOT_READY"
        try:
            with self.assertRaises(ValueError):
                client.command("App")
        finally:
            Handler.app_reply = None
        """回显文本不得被当作启动应用的合法回复。"""
        Handler.app_reply = "你好"
        try:
            with self.assertRaises(ValueError):
                client.command("App")
        finally:
            Handler.app_reply = None

    def test_bad_inputs(self):
        for address in ("", "https://host", "http://u:p@host", "host/path", "host:99999"):
            with self.subTest(address=address), self.assertRaises(ValueError):
                normalize_address(address)
        client = GatewayClient(self.address)
        with self.assertRaises(ValueError):
            client.command("ENTER_UPDATE")
        with self.assertRaises(ValueError):
            client.command("ECHO", "中" * 43)

    def test_wrong_service_and_http_error(self):
        for mode in ("wrong", "error"):
            Handler.mode = mode
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                GatewayClient(self.address).status()

    def test_timeout(self):
        Handler.mode = "slow"
        with self.assertRaises(TimeoutError):
            GatewayClient(self.address, timeout=0.03).status()


if __name__ == "__main__":
    unittest.main()
