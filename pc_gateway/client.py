"""局域网接口客户端；使用标准库，不依赖第三方包。"""
import json
from urllib.error import HTTPError
from urllib.parse import urlsplit
from urllib.request import Request, build_opener, ProxyHandler


def normalize_address(address):
    """将用户输入的主机地址规范为HTTP地址，拒绝路径和账号信息。"""
    value = address.strip()
    parsed = urlsplit(value if "://" in value else "http://" + value)
    if (parsed.scheme != "http" or not parsed.hostname or parsed.username
            or parsed.password or parsed.path not in ("", "/")
            or parsed.query or parsed.fragment):
        raise ValueError("请输入设备IP或主机名，例如 192.168.1.100")
    if parsed.port is not None and not 1 <= parsed.port <= 65535:
        raise ValueError("端口必须在1到65535之间")
    return "http://" + parsed.netloc


class GatewayClient:
    def __init__(self, address, timeout=4):
        """绑定设备地址和请求超时秒数，局域网请求不经过系统代理。"""
        self.base = normalize_address(address)
        self.timeout = timeout
        self.opener = build_opener(ProxyHandler({}))

    def request(self, path, payload=None):
        """请求指定接口，payload为可选JSON对象，返回解析后的响应对象。"""
        data = None if payload is None else json.dumps(payload, ensure_ascii=False).encode("utf-8")
        request = Request(self.base + path, data=data,
                          headers={"Content-Type": "application/json", "Accept": "application/json"})
        try:
            with self.opener.open(request, timeout=self.timeout) as response:
                raw = response.read(65537)
        except HTTPError as error:
            detail = error.read(512).decode("utf-8", errors="replace")
            raise ValueError(f"设备返回HTTP {error.code}：{detail}") from error
        if len(raw) > 65536:
            raise ValueError("响应超过允许长度")
        result = json.loads(raw)
        if not isinstance(result, dict):
            raise ValueError("设备响应必须是JSON对象")
        return result

    def status(self):
        """读取并检查网关状态响应，防止误连其他HTTP服务。"""
        result = self.request("/api/status")
        if result.get("device") != "h7-gateway" or result.get("api_version") != 1:
            raise ValueError("目标不是受支持的H7网关接口")
        if result.get("has_sample"):
            sample = result.get("sample")
            if (not isinstance(sample, dict)
                    or not isinstance(result.get("sample_age_ms"), (int, float))
                    or any(not isinstance(sample.get(key), (int, float)) for key in
                           ("sequence", "temperature", "humidity", "brightness"))):
                raise ValueError("设备数据字段不完整")
        return result

    def command(self, command, message=""):
        """发送PING/ECHO/Bootloader/App命令；message为最长128字节的UTF-8文本。"""
        if command not in ("PING", "ECHO", "Bootloader", "App"):
            raise ValueError("仅支持PING、ECHO、Bootloader和App")
        if len(message.encode("utf-8")) > 128:
            raise ValueError("回显文本不能超过128个UTF-8字节")
        previous_timeout = self.timeout
        try:
            # App与Bootloader都要等H7复位并重新初始化，需要放宽超时。
            if command in ("Bootloader", "App"): self.timeout = max(self.timeout, 12)
            result = self.request("/api/command", {"command": command, "message": message})
        finally:
            self.timeout = previous_timeout
        if command == "App":
            # 周期BOOT_READY只表示引导页在线，不能证明App命令已被接受。
            expected = ("APP_STARTING",)
            target = "stm32"
        else:
            expected = ("BOOT_READY",) if command == "Bootloader" else ("PONG",) if command == "PING" else (message,)
            target = "stm32" if command == "Bootloader" else "esp32"
        if (result.get("ok") is not True or result.get("target") != target
                or result.get("command") != command
                or result.get("reply") not in expected):
            raise ValueError("命令回复不匹配")
        return result
