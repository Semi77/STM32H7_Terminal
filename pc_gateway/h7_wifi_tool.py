"""H7局域网上位机：状态查询、引导页控制和应用固件下载；仅使用Python标准库。"""
import json
import struct
import zlib
import secrets
from pathlib import Path
from urllib.error import URLError
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
        old_timeout = self.timeout
        try:
            # App与Bootloader都要等H7复位并重新初始化，需要放宽超时。
            if command in ("Bootloader", "App"): self.timeout = max(self.timeout, 12)
            result = self.request("/api/command", {"command": command, "message": message})
        finally:
            self.timeout = old_timeout
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

    def ota_frame(self, command, session, offset=0, payload=b""):
        """发送带CRC的二进制帧并校验H7回复；参数为命令、会话、文件偏移和数据。"""
        frame = struct.pack("<5I", 0x544F3748, command, session, offset, len(payload)) + payload
        frame += struct.pack("<I", zlib.crc32(frame))
        last_error = None
        for attempt in range(3):
            try:
                req = Request(self.base + "/api/ota", data=frame,
                              headers={"Content-Type": "application/octet-stream"})
                with self.opener.open(req, timeout=130 if command == 6 else 20) as response:
                    raw = response.read(101)
                fields = raw.decode("ascii").strip().split(":")
                if len(fields) != 7 or fields[0] != "OTA":
                    raise ValueError("OTA回复格式错误")
                sid = int(fields[1], 16)
                cmd, request_offset, next_offset, status = map(int, fields[2:6])
                checksum = int(fields[6], 16)
                packed = struct.pack("<5I", sid, cmd, request_offset, next_offset, status)
                if checksum != zlib.crc32(packed) or (sid, cmd, request_offset) != (session, command, offset):
                    raise ValueError("OTA回复CRC或会话不匹配")
            except (OSError, URLError, ValueError, struct.error) as error:
                if isinstance(error, HTTPError) and error.code not in (408, 502, 503, 504):
                    raise ValueError(f"OTA接口HTTP {error.code}") from error
                last_error = error
                if attempt < 2:
                    time.sleep(0.3)
                    if command == 6:
                        # 安装可能已成功并复位，重新进入引导页查询同一镜像的完成记录。
                        try: self.command("Bootloader")
                        except (OSError, ValueError): pass
                continue
            if status:
                errors = {1:"帧CRC或长度错误", 2:"会话失效或没有有效下载记录", 3:"文件偏移错误", 4:"Flash读写失败",
                          5:"固件CRC不一致", 6:"固件入口、大小或所选文件与下载记录不匹配",
                          7:"另一下载尚未结束，请在停止传输60秒后重试", 8:"安装尚未恢复，请重新上传完整固件并安装"}
                raise ValueError(errors.get(status, f"STM32错误 {status}"))
            return next_offset
        raise ValueError(f"OTA请求连续3次未获得有效确认：{last_error}")

    @staticmethod
    def read_bin(filename):
        """校验构建时追加的长度和CRC，再返回去掉封装的原始应用字节。"""
        path = Path(filename)
        if path.suffix.lower() != ".bin": raise ValueError("请选择应用程序.bin文件")
        with path.open("rb") as source:
            packed = source.read(0x60000 + 33)
        if not 40 <= len(packed) <= 0x60000 + 32:
            raise ValueError("请选择构建生成的.ota.bin，文件可能被截断或超限")
        header = packed[-32:]
        magic, version, base, size, crc, reserved0, reserved1, header_crc = struct.unpack("<8I", header)
        if ((magic, version, base, reserved0, reserved1) != (0x57463748, 1, 0x08020000, 0, 0)
                or zlib.crc32(header[:28]) != header_crc):
            raise ValueError("缺少有效固件描述，请选择构建生成的.ota.bin，不要选择原始.bin")
        data = packed[:-32]
        if size != len(data) or zlib.crc32(data) != crc:
            raise ValueError("固件文件不完整或CRC错误，请重新构建")
        total = len(data)
        if not 8 <= total <= 0x60000: raise ValueError("固件必须为8字节至384 KiB")
        sp, pc = struct.unpack_from("<II", data)
        if (sp & 7 or not (0x20000000 < sp <= 0x20020000 or 0x24000000 < sp <= 0x24080000)
                or not pc & 1 or not 0x08020000 <= (pc & ~1) < 0x08020000 + total):
            raise ValueError("不是链接到0x08020000的A/B App固件；请勿选择旧布局、ESP32或Bootloader镜像")
        return data

    def install_bin(self, filename, progress):
        """安装与filename一致的已下载镜像，progress报告阶段、已完成字节和总字节。"""
        data = self.read_bin(filename)
        total, checksum = len(data), zlib.crc32(data)
        progress("检查安装支持", 0, total)
        if self.status().get("ota_install_version") != 1:
            raise ValueError("ESP32尚未烧录支持固件安装的新程序")
        self.command("Bootloader")
        progress("正在安装并读回校验，请等待", 0, total)
        session = secrets.randbelow(0xFFFFFFFF) + 1
        next_offset = self.ota_frame(6, session, payload=struct.pack("<II", total, checksum))
        if next_offset != total: raise ValueError("安装完成长度不匹配")
        progress("安装校验成功，设备即将启动新固件", total, total)
        return {"ok": True, "size": total, "crc32": f"{checksum:08X}",
                "installed": True, "startup": "requested"}

    def upload_bin(self, filename, progress, cancel):
        """上传filename到外部Flash；progress接收阶段/已确认字节/总字节，cancel为取消事件。"""
        data = self.read_bin(filename)
        total = len(data)
        checksum = zlib.crc32(data)
        session = secrets.randbelow(0xFFFFFFFF) + 1
        progress("检查设备", 0, total)
        if self.status().get("ota_download_version") != 1:
            raise ValueError("ESP32尚未烧录支持固件上传的新程序")
        if cancel.is_set(): raise ValueError("已取消")
        progress("进入Bootloader", 0, total)
        self.command("Bootloader")
        begun = False
        try:
            if cancel.is_set(): raise ValueError("已取消")
            progress("准备下载槽", 0, total)
            # 即使BEGIN回复丢失，也尝试ABORT同一会话；不会影响其他会话。
            begun = True
            next_offset = self.ota_frame(1, session, payload=struct.pack("<II", total, checksum) + data[:8])
            if next_offset != 0: raise ValueError("新会话起始偏移错误")
            for offset in range(0, total, 1024):
                if cancel.is_set(): raise ValueError("已取消")
                block = data[offset:offset+1024]
                next_offset = self.ota_frame(2, session, offset, block)
                if next_offset != offset + len(block): raise ValueError("STM32确认偏移不匹配")
                progress("接收并写入W25Q64", next_offset, total)
            if cancel.is_set(): raise ValueError("已取消")
            progress("整包读回校验", total, total)
            if self.ota_frame(3, session, total) != total: raise ValueError("校验完成偏移错误")
            begun = False
            progress("下载校验完成，未安装", total, total)
            return {"ok": True, "size": total, "crc32": f"{checksum:08X}",
                    "slot": "0x080000", "installed": False}
        except Exception:
            if begun:
                try: self.ota_frame(4, session)
                except Exception: pass
            raise


import json
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk, filedialog
from tkinter.scrolledtext import ScrolledText



from selftest_view import format_selftest


class GatewayWindow:
    def __init__(self, root):
        """创建桌面窗口，root为Tk主窗口。"""
        self.root = root
        root.title("H7 网关 · Wi-Fi 上位机")
        root.geometry("860x780")
        root.minsize(780, 700)
        self.events = queue.Queue()
        self.busy = False
        self.last_selftest = None
        self.cancel_upload = threading.Event()
        self.filename = tk.StringVar()
        self.upload_state = tk.StringVar(value="选择编译生成的 .ota.bin 升级文件，应用最大384 KiB")
        self.auto = tk.BooleanVar(value=False)
        self.address = tk.StringVar()
        self.message = tk.StringVar(value="你好，ESP32-C3")
        self.connection = tk.StringVar(value="尚未连接")
        self.details = tk.StringVar(value="输入ESP32串口日志中的IP，然后点击连接 / 刷新。")
        outer = ttk.Frame(root, padding=20)
        outer.pack(fill="both", expand=True)
        ttk.Label(outer, text="H7 网关", font=("Microsoft YaHei UI", 22, "bold")).pack(anchor="w")
        ttk.Label(outer, text="局域网状态监测与命令测试", padding=(0, 4, 0, 15)).pack(anchor="w")
        row = ttk.Frame(outer)
        row.pack(fill="x")
        ttk.Label(row, text="设备地址").pack(side="left")
        ttk.Entry(row, textvariable=self.address, width=30).pack(side="left", padx=10, fill="x", expand=True)
        self.refresh = ttk.Button(row, text="连接 / 刷新", command=lambda: self.submit("status"))
        self.refresh.pack(side="left")
        ttk.Checkbutton(outer, text="每2秒刷新状态", variable=self.auto).pack(anchor="w", pady=8)
        ttk.Label(outer, textvariable=self.connection, font=("Microsoft YaHei UI", 12, "bold")).pack(anchor="w")
        ttk.Label(outer, textvariable=self.details, justify="left", padding=(0, 10), wraplength=750).pack(anchor="w")
        commands = ttk.LabelFrame(outer, text="ESP32 命令往返测试", padding=10)
        commands.pack(fill="x", pady=8)
        self.ping = ttk.Button(commands, text="PING", command=lambda: self.submit("PING"))
        self.ping.pack(side="left")
        ttk.Entry(commands, textvariable=self.message).pack(side="left", fill="x", expand=True, padx=10)
        self.echo = ttk.Button(commands, text="发送回显", command=lambda: self.submit("ECHO"))
        self.echo.pack(side="left")
        self.boot = ttk.Button(commands, text="Bootloader", command=lambda: self.submit("Bootloader"))
        self.boot.pack(side="left", padx=5)
        # 与Bootloader互逆：向引导页发送App令其复位回Bank2。
        # 引导程序恢复失败时会永久停留在引导页，此按钮是唯一的软件出口。
        self.app = ttk.Button(commands, text="启动应用", command=lambda: self.submit("App"))
        self.app.pack(side="left", padx=5)
        upload = ttk.LabelFrame(outer, text="STM32 固件下载与安装", padding=10)
        upload.pack(fill="x", pady=8)
        ttk.Entry(upload, textvariable=self.filename).pack(fill="x")
        controls = ttk.Frame(upload)
        controls.pack(fill="x", pady=5)
        self.choose = ttk.Button(controls, text="选择升级文件", command=self.choose_bin)
        self.choose.pack(side="left")
        self.send_bin = ttk.Button(controls, text="发送固件", command=lambda: self.submit("upload"))
        self.send_bin.pack(side="left", padx=8)
        self.install = ttk.Button(controls, text="安装固件", command=lambda: self.submit("install"))
        self.install.pack(side="left", padx=8)
        self.cancel = ttk.Button(controls, text="取消上传", command=self.cancel_upload.set, state="disabled")
        self.cancel.pack(side="left")
        self.progress = ttk.Progressbar(upload, maximum=100)
        self.progress.pack(fill="x")
        ttk.Label(upload, textvariable=self.upload_state).pack(anchor="w")
        ttk.Label(outer, text="先发送固件，再点击安装；所选BIN须与下载内容一致。安装成功后自动启动，断电后恢复安装。",
                  wraplength=750).pack(anchor="w", pady=5)
        ttk.Label(outer, text="启动应用：向引导页发送App令其复位回Bank2。引导程序恢复失败时会停在引导页且无法重新下载，此按钮是唯一的人工出口。",
                  wraplength=750).pack(anchor="w")
        self.log = ScrolledText(outer, height=13, state="disabled", font=("Consolas", 10))
        self.log.pack(fill="both", expand=True, pady=(8, 0))
        root.after(100, self.drain)
        root.after(2000, self.poll)

    def choose_bin(self):
        """通过文件对话框选择待上传的STM32应用镜像。"""
        name = filedialog.askopenfilename(filetypes=[("STM32升级固件", "*.ota.bin")])
        if name: self.filename.set(name)

    def append_log(self, text):
        """将text追加到有行数上限的日志窗口。"""
        self.log.configure(state="normal")
        self.log.insert("end", time.strftime("%H:%M:%S ") + text + "\n")
        if int(self.log.index("end-1c").split(".")[0]) > 400:
            self.log.delete("1.0", "100.0")
        self.log.see("end")
        self.log.configure(state="disabled")

    def submit(self, action):
        """将action对应请求交给工作线程，避免网络超时阻塞窗口。"""
        if self.busy:
            return
        address, message = self.address.get(), self.filename.get() if action in ("upload", "install") else self.message.get()
        self.busy = True
        self.cancel_upload.clear()
        self.cancel.configure(state="normal" if action == "upload" else "disabled")
        for button in (self.refresh, self.ping, self.echo, self.boot, self.app, self.choose, self.send_bin, self.install):
            button.configure(state="disabled")
        threading.Thread(target=self.work, args=(address, action, message), daemon=True).start()

    def work(self, address, action, message):
        """执行网络请求并投递结果，参数为地址、操作和回显文本。"""
        start = time.monotonic()
        try:
            client = GatewayClient(address)
            if action in ("upload", "install"):
                def progress(stage, done, total):
                    self.events.put((address, "progress", (stage, done, total), 0, None))
                result = (client.upload_bin(message, progress, self.cancel_upload) if action == "upload"
                          else client.install_bin(message, progress))
            else:
                result = client.status() if action == "status" else client.command(action, message)
            self.events.put((address, action, result, (time.monotonic() - start) * 1000, None))
        except Exception as error:
            self.events.put((address, action, None, 0, str(error)))

    def drain(self):
        """在界面线程消费请求结果，并显示成功或错误状态。"""
        try:
            address, action, result, elapsed, error = self.events.get_nowait()
        except queue.Empty:
            pass
        else:
            if action == "progress":
                stage, done, total = result
                self.upload_state.set(f"{stage}：{done}/{total} 字节")
                self.progress["value"] = done * 100 / total if total else 0
                self.root.after(10, self.drain)
                return
            self.busy = False
            self.cancel.configure(state="disabled")
            for button in (self.refresh, self.ping, self.echo, self.boot, self.app, self.choose, self.send_bin, self.install):
                button.configure(state="normal")
            if error:
                if action == "upload": self.upload_state.set("上传未完成：" + error)
                if action == "install": self.upload_state.set("未获得安装成功确认：" + error)
                self.connection.set(f"请求失败 · {address}")
                self.details.set("当前状态未知；请检查设备IP、供电以及电脑与ESP32是否处于可互通网络。")
                self.append_log(error)
            else:
                self.connection.set(f"ESP32 请求成功 · {address} · {elapsed:.0f} ms")
                if action == "status":
                    sample = result.get("sample")
                    data = "尚未收到有效H7串口数据（不等于H7离线）"
                    if sample:
                        data = (f"最近串口记录 #{sample['sequence']} · 接收于 {result['sample_age_ms']/1000:.1f} 秒前\n"
                                f"温度 {sample['temperature']} °C    湿度 {sample['humidity']} %    光照 {sample['brightness']} lx")
                    self.details.set(f"MQTT上传就绪：{'是' if result.get('mqtt_ready') else '否'}    "
                                     f"串口：{result.get('uart_baud')}    Wi-Fi信号：{result.get('rssi', '--')} dBm\n"
                                     f"ESP32运行：{result.get('uptime_ms', 0)/1000:.0f} 秒\n{data}")
                if action == "status" and "selftest" in result:
                    report=result["selftest"]
                    key=(address,json.dumps(report,sort_keys=True))
                    if key != self.last_selftest:
                        self.append_log(format_selftest(report))
                        self.last_selftest=key
                    result={k:v for k,v in result.items() if k!="selftest"}
                self.append_log(action + " " + json.dumps(result, ensure_ascii=False))
        self.root.after(100, self.drain)

    def poll(self):
        """按用户选择定时刷新，前一请求未结束时不叠加请求。"""
        if self.auto.get() and self.address.get().strip() and not self.busy:
            self.submit("status")
        self.root.after(2000, self.poll)


if __name__ == "__main__":
    window = tk.Tk()
    GatewayWindow(window)
    window.mainloop()
