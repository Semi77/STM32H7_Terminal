# WHEELTEC C30D 驱动板（ROS 底层主控）硬件资料索引

> 搜集日期：本次会话
> 结论先行：**C30D 的完整硬件资料（原理图 / PCB / BOM / 接口手册 / 固件源码）官方不公开发布**，
> 只随整机附送（说明书 + 开发手册 + 源码 + ROS 镜像，一般走百度网盘 / 客服 / QQ 群）。
> 公开渠道能拿到的只有：产品参数、串口通信协议、上位机 ROS 驱动代码、实物照片。

---

## 0. C30D 是什么

| 项 | 内容 | 来源 |
|---|---|---|
| 官方定位 | ROS 机器人控制板，**四驱"驱控一体"底层主控**（板载电机驱动） | 官方商城标题 |
| 型号名 | C30D（新版）；商品 SKU `Wheeltec-C30D` | OpenELAB |
| 主控 MCU | **STM32F407VET6**（Cortex-M4，LQFP100，512KB Flash / 192KB SRAM，168MHz —— 该型号通用规格） | 商品页 |
| 通信 | 串口 + CAN | OpenELAB |
| 供电输出 | **5V / 5A**，可给树莓派、Jetson Nano 等上位机供电 | OpenELAB |
| 系统 | ROS / ROS2 兼容 | OpenELAB |
| 参考价 | USD 97.05（原价 107.00） | OpenELAB |

---

## 1. 串口通信协议（公开渠道能拿到的最有价值的"硬件资料"）

这是 C30D 与上位机（树莓派 / Jetson）之间的控制协议，**上下行同一套 0x7B / 0x7D 帧**。

### 1.1 下行控制帧（上位机 → C30D），共 11 字节

| 字节 | 字段 | 说明 |
|---|---|---|
| 0 | 帧头 | 固定 `0x7B` |
| 1 | 预留位 | 无意义 |
| 2 | 预留位 | 无意义 |
| 3–4 | X 轴目标速度 | 有符号 16 位，大端，单位 mm/s |
| 5–6 | Y 轴目标速度 | 有符号 16 位，大端，单位 mm/s |
| 7–8 | Z 轴目标速度 | 有符号 16 位，大端，**放大 1000 倍**，单位 rad/s |
| 9 | 校验位 | 前 9 字节**异或校验（BCC）** |
| 10 | 帧尾 | 固定 `0x7D` |

- 波特率：**115200** —— 官方文档原文是"主板波特率一般为 115200，也可以去看主板源码"，
  即它是**文档默认值而非硬约束**
- 该 11 字节布局**不是逆向出来的**：它印在随车附带的说明书里，CSDN 与 27daiwu 两个独立来源逐字节一致
- BCC 在线计算：http://www.ip33.com/bcc.html

### 1.2 实测报文样例（已按 BCC 规则核算通过）

| 动作 | 报文（hex） |
|---|---|
| 前进 | `7B 00 00 03 18 00 00 00 00 60 7D` |
| 后退 | `7B 00 00 FC E8 00 00 00 00 6F 7D` |
| 左移 | `7B 00 00 00 00 03 18 00 00 60 7D` |
| 右移 | `7B 00 00 00 00 FC E8 00 00 6F 7D` |
| 停止 | `7B 00 00 00 00 00 00 00 00 7B 7D` |

校验核算示例（前进）：`0x7B ^ 0x03 = 0x78`，`0x78 ^ 0x18 = 0x60` ✔

> ⚠️ 网上博客里手写的报文常量**部分是错的**（例如某"低速左转"帧标 `BBC校验位 0X03`，按严格异或不成立）。
> 抄样例可以，但**实际发送前一定自己重算 BCC**。

### 1.3 上行数据帧（C30D → 上位机），共 24 字节

来源：轮趣官方 ROS 包 `wheeltec_robot` 里串口解析逻辑的复刻
（[简书《轮趣下位机读取 -2》](https://www.jianshu.com/p/e211ba47bb54)、[《-1》](https://www.jianshu.com/p/80fd09234ea4)）

| 字节 | 字段 | 单位 / 换算 |
|---|---|---|
| 0 | 帧头 `0x7B` | — |
| 1 | 预留位（Flag_Stop） | — |
| 2–3 | X 方向速度 | 16 位大端，mm/s，÷1000 → m/s |
| 4–5 | Y 方向速度 | 同上（全向底盘才有效） |
| 6–7 | Z 方向角速度 | 同上，rad/s |
| 8–9 | IMU 加速度 X | 16 位有符号 |
| 10–11 | IMU 加速度 Y | 16 位有符号 |
| 12–13 | IMU 加速度 Z | 16 位有符号 |
| 14–15 | IMU 角速度 X | 16 位有符号 |
| 16–17 | IMU 角速度 Y | 16 位有符号 |
| 18–19 | IMU 角速度 Z | 16 位有符号 |
| 20–21 | 电池电压 | 16 位大端，**mV** |
| 22 | 校验位 | 前 22 字节异或（BCC） |
| 23 | 帧尾 `0x7D` | — |

**由此可推出的硬件信息（有价值）：**

- 串口设备名：`/dev/ttyACM0` → 板载 **USB CDC（虚拟串口）**，非 USB-TTL 芯片
- 波特率 **115200**
- IMU 量程（官方固件注释）：**加速度 ±2g（19.6 m/s²）、陀螺仪 ±500°/s**
  （"因机器人 Z 轴速度不快，降低量程以提高精度"）
- IMU 型号不固定：**MPU6050 或 MPU9250 都可能**（官方注释明确写的）
- 里程计：上位机对速度积分得到 X/Y/Z 位移（m / rad）
- 协议里还预留了"自动回充"数据结构（`RECEIVE_AutoCharge_DATA`）
- ⚠️ **上行 Z 的单位有歧义**：公开的读取实现把 X/Y/Z **三轴都**用 mm/s 的换算 `(v/1000)+(v%1000)*0.001` 处理，
  而下行 Z 是 rad/s×1000。所以**别直接把上行 Z 当成真实偏航角速度**，要自己标定。
- ⚠️ 另有传闻存在**第二套扩展帧**（`0xFC`/`0xFD` 开头），来自官方论坛关于"N100 读取 0xfc 0xfd 结果不对"的提问
  （[tid=3725](http://www.minibalance.com/forum.php?mod=viewthread&tid=3725)，需登录，**未经证实**）。

### 1.4 注意：与你手上 LFL 平衡小车的协议**同族不同版**

你仓库 `LFL/轮趣小车/HARDWARE/usartx.h` 用的是同一套帧头帧尾：

```c
#define FRAME_HEADER 0X7B
#define FRAME_TAIL   0X7D
#define SEND_DATA_SIZE    26   // 上行：速度+电压+加速度XYZ+角速度XYZ
#define RECEIVE_DATA_SIZE 13   // 下行：float X/Y/Z 速度
```

→ 帧头帧尾相同，但**长度与字段类型不同**（平衡小车是 float 速度、13/26 字节；C30D 四驱是 16 位整型、11 字节）。
交叉参考时不要混用。

---

## 2. 官方 / 半官方渠道（拿真资料的地方）

| 渠道 | 链接 | 能拿到什么 |
|---|---|---|
| 官网首页 | https://wheeltec.net/ | 产品目录（已翻遍全部分类与分页：**没有 C30D 独立产品页**） |
| 下载中心 | https://wheeltec.net/down/class/index.php | **只有 3 个 PDF**：R550 功能介绍、R550 ROS2 手册、24V 锂电池手册（另有 12V 电池手册）。**没有 C30D 条目，也没有任何网盘链接** |
| 常见问题 | https://wheeltec.net/page/cjwt/153.php | 页面为空（"更新中，敬请关注"） |
| 官方论坛 | http://bbs.wheeltec.net | 有帖子 "stm32板子上有没有C30D有什么区别"（tid=4754，已被搜索引擎收录）。**Discuz 反爬 `attackevasive_1`（"页面重载开启"），已试过 viewthread / mobile=2 / inajax=1 / printable / archiver / 伪静态等全部形式均被拦，正文取不到，需浏览器打开** |
| 老论坛 | http://www.minibalance.com | 同上，同一套反爬 |
| 天猫店 | https://wheeltec.tmall.com/ | 购买 / 详情页 |
| 官方文档仓库 | https://github.com/wheeltec/docs | R550、B585 上手文档 + ROS 镜像百度网盘链接；**没有 C30D / 底层主控内容** |
| 官方文档站 | https://lubancat.wheeltec.net/ | 由上面仓库用 Sphinx 构建的《WHEELTEC ROS机器人与鲁班猫1S使用手册》。可确认上位机包名 `turn_on_wheeltec_robot`（mapping/navigation/map_saver.launch）与 `wheeltec_robot_rc`（keyboard_teleop.launch）——**只有上电/使用流程，没有引脚定义** |
| 官方 ROS 开发教程 | https://gitee.com/shu-peixuan/akm_driver （内含 `ROS开发教程.pdf`，2.6MB） | 轮趣 ROS 车开发流程、坐标系、串口通信 |
| ~~docs.wheeltec.net~~ | ❌ **域名不存在**（DNS 解析失败） | 不存在官方 docs 子域 |

> **拿完整 C30D 资料的唯一途径：找官方客服索取随车"资料包"**（说明书 / 开发手册 / 原理图 / 底层源码 / ROS 镜像，
> 一般随车以 microSD/U盘 形式提供）。

---

## 3. 上位机侧代码（了解接口的另一半）

| 资源 | 链接 | 说明 |
|---|---|---|
| STM32 当上位机控制轮趣 ROS 车 | https://github.com/27daiwu/STM32_CONTROL_WHEELTEC | 完整实现了上面的 11 字节协议（`order.c` 里的 forward/left/right/stop） |
| 同作者 CSDN 教程 | https://blog.csdn.net/hebegetcode/article/details/147932755 | 逐字节讲解协议 + 报文截图 |
| 轮趣 ROS2 实验代码 | https://github.com/Aent-8/WHEELTEC_ROS2 | 小车实验代码 |
| 轮趣 ROS 工程（社区镜像） | https://github.com/1417265678/wheeltec | ROS 无人驾驶小车工程 |
| 下位机读取复刻（ROS2） | https://www.jianshu.com/p/e211ba47bb54 · [《-1》](https://www.jianshu.com/p/80fd09234ea4) | **上行 24 字节帧的完整解析实现**（本文 §1.3 的来源）。作者说是因为"参照原厂代码"编译不过才自己重写 → 说明**官方 ROS 驱动源码确实存在于随车镜像里，但网上没有托管** |

---

## 4. 还值得自己去浏览器里下的 PDF（最有希望的未挖线索）

抓取工具**不能解析 PDF**，所以这几个必须用浏览器打开下载：

| 资料 | 链接 | 为什么值得看 |
|---|---|---|
| 轮趣 B570 平衡小车开发手册 | [USTC GitLab 镜像](https://git.ustc.edu.cn/pocket/robo-walker2022_infantry/-/blob/747128402fb2a3cf92b3248e21fee7a6a4254eab/2.WHEELTEC%20B570%20%E5%B9%B3%E8%A1%A1%E5%B0%8F%E8%BD%A6%E5%BC%80%E5%8F%91%E6%89%8B%E5%86%8C.pdf) | **目前能找到的最接近"官方底层文档"的东西**（姊妹产品，含底层接口/协议） |
| 俄语经销商镜像的轮趣手册 | `supereyes.ru/img/instructions/1_wheeltec_manual.pdf`、`wheeltec_mecanum_wheel_manual_rus.pdf`、`WHEELTEC_tracked_vehicle_ ROS_manual.pdf`、`dev.supereyes.ru/.../manual_WHEELTEC_Ackerman_ROS.pdf` | 索引片段里出现了"由高字节的最高位决定"这类**符号位约定**描述，与 16 位补码速度字段吻合；可能含接口/协议图 |

---

## 5. 第三方商城（有参数与实物图）

| 商城 | 链接 | 备注 |
|---|---|---|
| OpenELAB | https://openelab.com/products/wheeltec-c30d-stm32f407vet6-ros-four | 参数完整；有 3 张实物图（Shopify CDN） |
| iCEasy 云汉芯城 | https://www.iceasy.net/12172/1028867892 | "新版C30D ROS底层主控（四驱）STM32F407VET6" |
| 什么值得买参数页 | https://wiki.smzdm.com/p/5j9vx6d/canshu/ | 规格参数表（页面为 JS 渲染，需浏览器打开） |

---

## 6. 公开渠道**找不到**的东西（不要浪费时间搜）

- ❌ **没有 C30D 官方产品页**（官网全部产品分类 + 分页 + 产品索引均已翻遍）
- ❌ 下载中心无 C30D 条目，全网没找到百度网盘链接 / 提取码
- ❌ C30D 原理图 / PCB 源文件（无立创EDA / AD 工程公开）
- ❌ 引脚定义、接插件丝印表、排针定义（无公开的接口说明文档）
- ❌ BOM、物料清单、尺寸图、3D 模型
- ❌ 底层主控固件源码（Keil `uvprojx` 工程 / HAL 工程；GitHub 用户 `wheeltec` 只有 docs 仓库，
      Gitee API 搜 `C30D` 为空）
- ❌ 各芯片数据手册（连板上电机驱动芯片型号都查不到）
- ❌ **轮径 / 轮距 / 编码器线数 / 减速比** —— 常被引用的"13 线霍尔 / 30:1"在**任何可查来源中都不存在，属无出处数据，不要采信**
- ❌ 电池允许电压范围（上行只给了 mV 字段，量程未公开）
- ❌ CAN 协议文档（这块板是否真有 CAN 都未证实）

→ 这些只能从**官方资料包**或**实物反推**获得。

---

## 7. 与本地工程的衔接

`C:\Users\28478\Desktop\stm32h7\project`（本地 STM32H7 工程）
`gitee.com/scauaoi/scauaoi` → `XHX/ProPrj_瞬喵-STM32F407VET6主控板_2026-09-01.epro2`
（立创EDA专业版工程，同为 F407VET6 主控板，可作对照设计参考）

参考：`LFL/轮趣小车/` 是轮趣官方 Mini 平衡小车 D 版（霍尔编码器）例程改的，
引脚分配已整理（见会话记录），可作为轮趣硬件风格的旁证。

---

## 8. 本次搜集的环境限制

- 沙箱内 shell 无法联网（curl 报 `schannel: SEC_E_NO_CREDENTIALS`，exit 35），
  因此**没法把 PDF / 图片直接下载到工作区**，只能给出链接清单。
- 抓取工具**不能解析 PDF**，也**不能执行 JS**。所以：
  - `supereyes.ru` 的俄语手册 PDF → 必须浏览器下载
  - 官方论坛（Discuz 反爬）、iCEasy、什么值得买参数页 → 必须浏览器打开
- 需要实际落盘下载时，可在获授权后重试下载，或手动用浏览器保存。

---

## 附：资料可信度分级

| 级别 | 内容 |
|---|---|
| ✅ **官方文档印载**（可信） | 下行 11 字节帧格式、波特率默认 115200 |
| ✅ **官方固件行为**（可信，来自源码解析） | 上行 24 字节帧、`/dev/ttyACM0`、IMU 量程 ±2g/±500°/s、IMU 可能是 MPU6050 或 MPU9250 |
| ⚠️ **有歧义，需自测** | 上行 Z 轴单位、实际波特率、`0xFC/0xFD` 第二套帧 |
| ❌ **无出处，勿采信** | 轮径、轮距、编码器线数、减速比、引脚定义、CAN 协议 |
