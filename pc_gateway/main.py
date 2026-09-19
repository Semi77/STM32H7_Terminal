"""兼容原启动入口，界面及上传逻辑统一使用可独立运行的h7_wifi_tool.py。"""
import tkinter as tk
from h7_wifi_tool import GatewayWindow

if __name__ == "__main__":
    root = tk.Tk()
    GatewayWindow(root)
    root.mainloop()