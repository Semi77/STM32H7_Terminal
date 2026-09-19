"""将ESP32返回的SD自检记录转换为中文日志。"""

LABELS = {
    0: "SD卡初始化通过", 1: "文件系统挂载通过", 2: "音频文件完整读取通过",
    3: "SD卡未就绪或通信失败", 4: "文件系统挂载失败", 5: "目录访问失败",
    6: "文件打开、读取或关闭失败", 7: "根目录未找到WAV/MP3/PCM文件",
    8: "超过自检时间预算，检查未完成", 9: "报告容量达到上限，检查未完成",
    10: "自检通过", 11: "自检未通过，网关继续运行",
    12: "自检任务创建失败", 13: "音频文件为空",
}


def format_selftest(report):
    """report为状态接口自检对象；返回包含传输完整性和逐项结果的文本。"""
    if not report or not report.get("total"):
        return "上电自检：ESP32尚未收到报告（不能据此判断自检通过或失败）。"
    complete = report.get("complete") is True
    state = "报告接收完整" if complete else "报告接收中"
    lines = [f"上电自检 #{report.get('run', 0)}：{state} "
             f"({report.get('received', 0)}/{report.get('total', 0)})"]
    for item in report.get("records", []):
        code = item.get("code")
        label = LABELS.get(code, f"未知结果 {code}")
        if code == 10 and not complete:
            label = "设备已给出通过汇总，但报告尚未接收完整"
        try:
            name = bytes.fromhex(item.get("filename_hex", "")).decode("gbk")
            name = name.replace("\r", "\\r").replace("\n", "\\n")
        except (ValueError, UnicodeError, TypeError):
            name = "[文件名编码错误]"
        line = f"  {item.get('index', 0) + 1}. {label}"
        if name:
            line += f"：{name}，已读取 {item.get('read', 0)}/{item.get('size', 0)} 字节"
        if code in (10, 11):
            line += f"，检查音频文件 {item.get('size', 0)} 个"
        line += f"，耗时 {item.get('elapsed_ms', 0)} ms"
        if code not in (0, 1, 2, 10, 11):
            line += f"，FatFs={item.get('result', 0)}，HAL={item.get('hal_error', 0)}"
        lines.append(line)
    lines.append("本项验证文件可读取，不进行音频解码或播放。")
    return "\n".join(lines)
