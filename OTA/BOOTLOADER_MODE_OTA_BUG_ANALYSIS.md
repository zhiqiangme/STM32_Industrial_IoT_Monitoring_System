# OTA 在 Bootloader 模式下刷写 A/B 镜像问题分析

## 问题现象

当前 OTA 上位机存在以下现象：

1. 如果单片机已经处于 Bootloader 模式，上位机中的“固件”和“执行里”会显示为“未知”。
2. 如果单片机已经处于 Bootloader 模式：
   - 刷 `App_B.bin` 可以成功，设备最终能正确进入 `App_B`
   - 刷 `App_A.bin` 时，上位机会显示升级成功，但程序实际上没有正确进入 App，单片机仍停留在 Bootloader 模式

用户提供的一次失败日志如下：

```text
[18:26:43] 准备升级，模式 本地升级。
[18:26:43] 警告: 未读取到当前运行槽位，将按所选镜像继续升级。
[18:26:43] 所选镜像槽：A。
[18:26:43] 镜像复位向量：0x08008145。
[18:26:43] 固件: D:\Project\STM32_Mill\STM32\MDK-ARM\Objects\App_A.bin
[18:26:43] 固件大小: 26952 bytes (0x00006948)
[18:26:43] 固件 CRC32: 0xCFCF5B45
[18:26:43] 固件 SHA256: 62c3dd4a8e3b2f1e94b0fd18047716e097e2487925c5b375e3faf22401c2326e
[18:26:44] 打开串口 COM5，波特率 115200。
[18:26:44] TX 解锁: 0A060030A55A73D5
[18:26:49] 警告: 解锁 未在 5 秒内收到回包，继续尝试后续流程。
[18:26:49] TX 进入 Bootloader: 0A0600310005197D
[18:26:54] 警告: 进入 Bootloader 未在 5 秒内收到回包，继续尝试后续流程。
[18:26:54] 等待 2500ms 让设备从 App 切到 Bootloader。
[18:26:56] 等待 Bootloader YMODEM 握手 ('C')...
[18:26:57] 已收到 Bootloader 握手字符 'C'。
[18:26:57] YMODEM 头包已确认。
[18:26:58] DATA: 1024/26952 bytes (3%)
[18:26:58] DATA: 2048/26952 bytes (7%)
[18:26:58] DATA: 3072/26952 bytes (11%)
[18:26:58] DATA: 4096/26952 bytes (15%)
[18:26:58] DATA: 5120/26952 bytes (18%)
[18:26:58] DATA: 6144/26952 bytes (22%)
[18:26:58] DATA: 7168/26952 bytes (26%)
[18:26:58] DATA: 8192/26952 bytes (30%)
[18:26:59] DATA: 9216/26952 bytes (34%)
[18:26:59] DATA: 10240/26952 bytes (37%)
[18:26:59] DATA: 11264/26952 bytes (41%)
[18:26:59] DATA: 12288/26952 bytes (45%)
[18:26:59] DATA: 13312/26952 bytes (49%)
[18:26:59] DATA: 14336/26952 bytes (53%)
[18:26:59] DATA: 15360/26952 bytes (56%)
[18:27:00] DATA: 16384/26952 bytes (60%)
[18:27:00] DATA: 17408/26952 bytes (64%)
[18:27:00] DATA: 18432/26952 bytes (68%)
[18:27:00] DATA: 19456/26952 bytes (72%)
[18:27:00] DATA: 20480/26952 bytes (75%)
[18:27:00] DATA: 21504/26952 bytes (79%)
[18:27:00] DATA: 22528/26952 bytes (83%)
[18:27:00] DATA: 23552/26952 bytes (87%)
[18:27:01] DATA: 24576/26952 bytes (91%)
[18:27:01] DATA: 25600/26952 bytes (94%)
[18:27:01] DATA: 26624/26952 bytes (98%)
[18:27:01] DATA: 26752/26952 bytes (99%)
[18:27:01] DATA: 26880/26952 bytes (99%)
[18:27:01] DATA: 26952/26952 bytes (100%)
[18:27:01] EOT 已确认。
[18:27:01] 最终空包已确认。
[18:27:01] 升级传输完成，设备应完成校验并自动复位回 App。
[18:27:01] 流程结束。
[18:27:17] 已刷新串口列表：COM5 USB串行设备, COM11 USB串行设备
```

## 定位结论

结论分为两部分：

1. “当前运行槽位显示未知”是因为设备已经不在 App，而在 Bootloader，导致上位机读取运行槽位寄存器失败。
2. “Bootloader 模式下刷 `App_A.bin` 假成功、刷 `App_B.bin` 真成功”的根因是 Bootloader 端自己决定写入目标槽位，而不是按上位机当前选中的 `App_A.bin` 或 `App_B.bin` 来决定。

## 原因分析

### 1. 为什么 Bootloader 模式下运行槽位会显示未知

上位机读取“当前运行槽位”的逻辑在：

- [RunningSlotProtocol.cs](/D:/Project/STM32_Mill/OTA/OTA.Protocols/State/RunningSlotProtocol.cs)

该逻辑通过 Modbus 读取寄存器 `0x005A`。

设备侧这个寄存器由 App 的 G780s 从站提供，相关逻辑在：

- [G780s.h](/D:/Project/STM32_Mill/STM32/BSP/G780s.h)
- [G780s.c](/D:/Project/STM32_Mill/STM32/BSP/G780s.c)

其中 `REG_DIAG_RUNNING_SLOT = 0x005A`，寄存器值来自：

- `Upgrade_GetRunningSlot()`

而 `Upgrade_GetRunningSlot()` 的实现位于：

- [Upgrade.c](/D:/Project/STM32_Mill/STM32/BSP/Upgrade.c)

其本质是根据当前 `SCB->VTOR` 所在地址判断当前 App 槽位。

但是当设备已经进入 Bootloader 后：

1. USART3 不再运行 App 的 G780s Modbus 从站逻辑
2. 同一串口被 Bootloader 接管，用于 YMODEM 收发

因此此时 PC 端去读 `0x005A` 不会得到正常 App 回包，最终上位机只能把当前运行槽位显示为“未读取”或“未知”。

所以这个现象不是“上位机识别错了 App_A / App_B”，而是因为设备当前根本不在 App 环境中。

### 2. 为什么 Bootloader 模式下 `App_B.bin` 能刷，`App_A.bin` 不能刷

上位机在本地升级时会先分析镜像槽位，但真正进入 YMODEM 发送后，发送流程本身并不会把“目标槽位”作为独立字段传给 Bootloader。

上位机 YMODEM 相关代码位于：

- [YModemProtocol.cs](/D:/Project/STM32_Mill/OTA/OTA.Protocols/YModem/YModemProtocol.cs)

YMODEM 头包里包含的信息是：

- 文件名
- 文件大小
- CRC32
- `target_fw_version`
- SHA256

这里并没有“目标槽位 A/B”这个字段。

设备侧 YMODEM 头包解析位于：

- [ymodem.c](/D:/Project/STM32_Mill/STM32/System/ymodem.c)

其解析内容同样只有：

- `file_size`
- `image_crc32`
- `target_fw_version`
- `image_sha256`

也就是说，Bootloader 并不会根据 `App_A.bin` 或 `App_B.bin` 这个选择直接得到“请写入 A 槽”或“请写入 B 槽”。

真正决定写入哪个槽的是 Bootloader 端：

- [boot_flash.c](/D:/Project/STM32_Mill/STM32/Bootloader/boot_flash.c)

在 `BootFlash_BeginImage()` 中，会先执行：

- `runtime->transfer_slot = BootFlash_SelectDownloadSlot(runtime);`

而 `BootFlash_SelectDownloadSlot()` 的策略是：

1. 先根据 `boot_control.confirmed_slot`
2. 再根据 `boot_control.active_slot`
3. 找到当前认为“稳定可运行”的槽位
4. 然后选择“另一槽”作为下载槽

这意味着：

- Bootloader 写入目标槽位是它自己算出来的
- 它不是按上位机现在选了 `App_A.bin` 还是 `App_B.bin` 来决定的

### 3. 为什么这会导致 `App_A.bin` 传完但无法启动

以当前现象为例：

1. 设备已经在 Bootloader 中
2. Bootloader 当前根据 `boot_control` 认为“稳定槽”为 A
3. 那么它会把新的下载目标固定选为 B

这时出现两种情况：

#### 情况 A：发送 `App_B.bin`

`App_B.bin` 的复位向量位于 B 槽地址范围内。

Bootloader 将它写到 B 槽后，最终校验阶段会检查：

- CRC32
- SHA256
- 向量表是否合法

相关逻辑在：

- [boot_verify.c](/D:/Project/STM32_Mill/STM32/Bootloader/boot_verify.c)

如果当前写入槽位是 B，而镜像本身也是 B 槽镜像，那么向量表校验可以通过，最终升级成功，设备复位后进入 `App_B`。

#### 情况 B：发送 `App_A.bin`

`App_A.bin` 的复位向量位于 A 槽地址范围内，例如日志中的：

- `0x08008145`

但 Bootloader 实际却把它写进了 B 槽。

这样会出现：

1. YMODEM 传输过程本身是成功的
2. 上位机会看到头包、数据包、EOT 都完成
3. 因此上位机日志会显示“升级传输完成”

但是在 Bootloader 最终校验时，会按“B 槽镜像”去校验向量表是否合法。

由于 `App_A.bin` 的 ResetHandler 地址落在 A 槽，不在 B 槽范围内，因此：

- 向量表校验失败
- Bootloader 不会放行进入 App
- 设备继续停留在 Bootloader

这就形成了表面现象：

- PC 端看起来像“刷成功了”
- 实际设备没有进入 App

本质上不是“程序没传进去”，而是“传输了，但最终没通过 Bootloader 放行校验”。

## 现象与代码的对应关系

### 上位机侧

- [LocalUpgradeViewModel.cs](/D:/Project/STM32_Mill/OTA/OTA.ViewModels/LocalUpgradeViewModel.cs)
  - 负责准备升级、读取运行槽位、输出日志
- [RunningSlotProtocol.cs](/D:/Project/STM32_Mill/OTA/OTA.Protocols/State/RunningSlotProtocol.cs)
  - 读取 `0x005A`，设备在 Bootloader 时会失败
- [YModemProtocol.cs](/D:/Project/STM32_Mill/OTA/OTA.Protocols/YModem/YModemProtocol.cs)
  - 发送 YMODEM 头包和数据包，但不传目标槽位

### 设备 App 侧

- [G780s.c](/D:/Project/STM32_Mill/STM32/BSP/G780s.c)
  - App 运行时提供 `0x005A` 运行槽位寄存器
- [Upgrade.c](/D:/Project/STM32_Mill/STM32/BSP/Upgrade.c)
  - `Upgrade_GetRunningSlot()` 根据 `VTOR` 判断当前槽位

### Bootloader 侧

- [boot_flash.c](/D:/Project/STM32_Mill/STM32/Bootloader/boot_flash.c)
  - `BootFlash_SelectDownloadSlot()` 决定实际写入槽位
- [boot_main.c](/D:/Project/STM32_Mill/STM32/Bootloader/boot_main.c)
  - 升级完成后做最终校验并决定是否复位进入 App
- [boot_verify.c](/D:/Project/STM32_Mill/STM32/Bootloader/boot_verify.c)
  - 对 CRC32、SHA256、向量表进行最终放行校验
- [ymodem.c](/D:/Project/STM32_Mill/STM32/System/ymodem.c)
  - 解析 YMODEM 头包，但不解析目标槽位

## 最终结论

当前问题的核心不是上位机“错误地把 `App_A.bin` 发到了 A 槽”，而是：

1. 设备已在 Bootloader 时，上位机无法通过 App 的 Modbus 寄存器读取当前运行槽位，所以界面显示为“未知”。
2. Bootloader 模式下，实际下载目标槽位由 Bootloader 自己依据 `boot_control` 决定。
3. 上位机虽然允许用户选择 `App_A.bin` / `App_B.bin`，但这个选择不会直接约束 Bootloader 的目标槽。
4. 当 Bootloader 当前决定把镜像写入 B 槽时：
   - 发送 `App_B.bin` 可以通过最终校验
   - 发送 `App_A.bin` 会在传输完成后因向量表与目标槽不匹配而校验失败

因此，当前表现完全符合现有代码逻辑。

## 本次处理范围

本次仅完成原因定位与分析，未修改任何代码。
