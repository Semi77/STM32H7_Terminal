# STM32H743VGT6 A/B OTA 与首次烧录

这份工程针对 STM32H743VGT6 的 1 MiB 内部 Flash。每个 Bank 为 512 KiB，包含四个 128 KiB 扇区；截图中“每 Bank 1 MiB、八个扇区”的表格对应 2 MiB 型号，不能直接用于 VGT6。应用原始 BIN 必须小于单个应用区的 393216 字节上限，具体大小以本次构建产物为准。

| 用途 | 逻辑地址 | 容量 |
| --- | --- | ---: |
| 活动 Bootloader | 0x08000000–0x0801FFFF | 128 KiB |
| 活动应用 | 0x08020000–0x0807FFFF | 384 KiB |
| 非活动 Bootloader 副本 | 0x08100000–0x0811FFFF | 128 KiB |
| 非活动应用 | 0x08120000–0x0817FFFF | 384 KiB |
| 外部 Flash 下载槽 | 0x00080000–0x000FFFFF | 512 KiB |
| 外部 Flash 下载记录 | 0x00100000、0x00101000 | 各 4 KiB |
| 外部 Flash A/B 状态 | 0x00103000、0x00104000 | 各 4 KiB |

应用始终链接到逻辑地址 0x08020000。Bootloader 只擦写非活动 Bank 的应用扇区 1–3，安装后读回 CRC，再修改 SWAP_BANK 并复位。两边的 Bootloader 扇区必须逐字节相同，交换前会检查。旧版镜像和两个状态副本保留在原位置，安装中断电不会擦除已确认版本。

新应用最多试启动两次。每次跳转前记录次数并启动约 32 秒的 IWDG；应用启动后会恢复原有短超时。业务健康条件连续满足 20 秒时，应用通过 RTC 备份寄存器写确认令牌并软件复位；Bootloader 校验令牌和复位原因，才将候选版本标记为已确认。如果两次都没有确认，就切回旧 Bank，并记住失败镜像的长度和 CRC，避免反复安装同一镜像。若首次交换Bank失败且旧版本仍在运行，Bootloader撤销候选并继续启动旧版本。这里的“健康”以现有启动标志、任务心跳等条件为准，无法保证应用所有业务功能都正常。

首次从旧单区固件迁移必须使用调试器完成一次工厂烧录，旧布局的 OTA 包不能用于新布局。操作顺序：

1. 保存需要保留的外部 Flash 离线缓存或配置。旧的 Clean 工程会擦除整片 8 MiB W25Q64，包括下载区、A/B 状态区和离线缓存；运行到 FLASH IS BLANK 后断电。现在清理程序会读回每个字节确认擦除成功。
2. 在 STM32CubeProgrammer 中确认芯片型号/修订号、内部 Flash 大小为 1 MiB，关闭 SWAP_BANK，确认启动地址为 0x08000000 且 IWDG 选项为软件启动。当前自动交换代码只接受 Rev.V；其他修订号需要按该修订版参考手册核实交换生效时序后再适配。
3. 编译 bootloader 和 wangguan 两个 Keil 工程。应用链接起点必须为 0x08020000，生成 wangguan.bin 与 wangguan.ota.bin。使用 fromelf --bin --output=bootloader.bin bootloader.axf 导出引导 BIN。
4. 运行：python project/shared/build_ab_factory.py --boot-bin project/bootloader/bootloader/MDK-ARM/bootloader/bootloader.bin --app-bin project/wangguan/MDK-ARM/wangguan/wangguan.bin --output project/shared/wangguan_ab_factory.hex --boot-bank2-output project/shared/bootloader_bank2.hex。脚本会检查两个向量表，把完全相同的 Bootloader 分别放到两个 Bank 首扇区，并把应用放在活动 Bank 后 384 KiB。输出包含两段各512 KiB的Bank地址，未使用字节填 FF。
5. 用调试器整片擦除内部 Flash，再烧录并回读校验生成的 wangguan_ab_factory.hex。采用单文件流程时无需随后分别烧录工程。上电后，空白的外部 A/B 状态区会登记当前 Bank 为工厂基线；若恰在首次登记状态时断电，必须重新清理外部状态区并按工厂流程烧录。

也可以在 Clean 完成后分三次烧录。第二个Bank从0x08100000开始，0x08080000至0x080FFFFF是地址空洞：

1. 关闭SWAP_BANK，设置启动地址0x08000000；只在开始时整片擦除一次内部Flash。
2. 烧录并校验 shared/bootloader_bank2.hex，文件内地址为0x08100000；这是同一份Bootloader在第二个Bank的副本，向量表仍指向逻辑低地址。
3. 烧录并校验 wangguan/MDK-ARM/wangguan/wangguan.hex，文件内地址为0x08020000。
4. 最后烧录并校验 bootloader/bootloader/MDK-ARM/bootloader/bootloader.hex，文件内地址为0x08000000。这三次写入之间不得再次选择整片擦除，否则会删除前面已经写入的文件。
5. 烧录期间尽量禁止“烧录后自动运行”，完成三个文件后再复位。读回检查两个128 KiB引导扇区完全相同，且0x08020000处为主程序向量表。

### 全程使用 Keil µVision 编译与下载

Clean 已运行完成后，可以只用 Keil 做内部 Flash 下载。先在 Keil 中分别编译 `bootloader/bootloader/MDK-ARM/bootloader.uvprojx` 和 `wangguan/MDK-ARM/wangguan.uvprojx`。Bootloader 编译成功后会自动更新 `shared/bootloader_bank2.hex`；主程序编译成功后生成 `wangguan.hex`。确认两个工程均显示 `0 Error(s)`，不要编译 `shared/flash_bootloader_bank2.uvprojx`，它是只供下载已有 HEX 的工程。

三个工程的 `Options for Target > Utilities > Settings > Flash Download` 都选 `Erase Sectors`、`Program`、`Verify`，取消 `Reset and Run`，不要选择 `Erase Full Chip`。在第二 Bank 专用工程中，Flash 算法必须覆盖 `Start 0x08100000, Size 0x00080000`；已提供的工程配置使用 `STM32H7x_2048.FLM`，打开后请在界面再核对一次。SWAP_BANK 应为 0，芯片启动地址应为 0x08000000。Keil 的 `Flash > Download` 会下载当前打开工程的输出文件；切换工程时确认标题栏与目标名称。

1. 打开 `shared/flash_bootloader_bank2.uvprojx`，直接点 `Flash > Download`，检查下载和校验成功。这个工程的输出名称已设为 `bootloader_bank2.hex`，地址为 0x08100000。
2. 打开 `wangguan/MDK-ARM/wangguan.uvprojx`，点 `Flash > Download`，检查下载和校验成功。应用地址为 0x08020000。
3. 最后打开 `bootloader/bootloader/MDK-ARM/bootloader.uvprojx`，点 `Flash > Download`，检查下载和校验成功。引导地址为 0x08000000。退出调试后按 RESET 或重新上电。

三次下载都只能按扇区擦除。任一步失败时不要继续启动，先处理下载或校验错误。若后来重新编译 Bootloader，需要重新下载 Bank2 副本和 Bank1 Bootloader，保持两边一致。

后续升级只选 wangguan.ota.bin；上位机拒绝旧地址 0x08100000 的包和超过 384 KiB 的包。下载完成后点击“安装固件”。若安装中断电，重新上电会保留旧版本，重新发起安装即可；若候选启动失败，会自动回滚。已被拒绝的相同长度和 CRC 的镜像需重新编译出不同内容后才能再次安装。Bootloader 和应用都必须先完成本次工厂迁移，之后普通 OTA 只更新应用，不能单独升级 Bootloader。

如果重新使用 CubeMX 生成工程文件，须再次检查应用链接起点 0x08020000、向量表地址 0x08020000、384 KiB 大小以及 Bootloader 的两份引导配置；生成器可能覆盖手工修改。

当前验证覆盖两份固件的 Keil 编译、应用打包检查，以及主机模拟的 200 个安装断电点、未确认回滚、确认提交和日志损坏。SWAP_BANK 选项写入、实机 IWDG 时序、RTC 备份域跨复位保留以及真实断电恢复还需要在板上验收。建议依次验证：正常升级、升级后立即断电、候选应用连续两次启动失败、恢复旧版，以及在新已确认版本基础上再次升级回另一 Bank。
