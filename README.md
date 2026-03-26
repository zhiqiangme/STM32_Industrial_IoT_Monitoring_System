# STM32 Mill 控制与维护仓库

这是一个围绕 `STM32F1xx + RS485 + G780S` 搭起来的完整项目仓库，包含下位机固件、Bootloader、本地 OTA 工具、远程升级脚本、Windows 上位机和配套调试文档。

当前代码已经覆盖三条主线：

- 现场采集与继电器控制
- App 运行态远程维护
- Bootloader 本地/远程升级

## 1. 项目组成

| 目录 / 文件 | 作用 | 当前状态 |
|---|---|---|
| `STM32/` | STM32 主固件、Bootloader、Keil 工程 | 主体代码 |
| `Windows/` | WPF 上位机，轮询显示现场数据并控制继电器 | 可用 |
| `OTA/` | WPF 升级工具界面 | 本地升级可用，远程页仅预留 |
| `Modbus_CRC/` | Modbus RTU 单寄存器原始帧生成工具 | 可用 |
| `stm32_local_upgrade.py` | 串口 YMODEM 本地升级脚本 | 可用 |
| `stm32_remote_upgrade.py` | 通过 TCP 透传的远程升级脚本 | 可用 |
| `文档/` | 传感器、G780S、以太网模块、ModbusPoll 等资料 | 参考资料 |
| `远程维护指令手册.md` | App 运行态 Modbus 维护寄存器和报文说明 | 核心文档 |
| `第二阶段本地升级手册.md` | 本地升级联调说明 | 核心文档 |
| `第三阶段远程升级方案.md` | 远程升级链路方案 | 设计文档 |

## 2. 系统链路

### 2.1 运行态

`传感器 / 继电器模块 -> USART2(Modbus 主站) -> STM32 App -> USART3(Modbus 从站) -> G780S -> 远端工具`

STM32 App 主要负责：

- 轮询 PT100、称重模块、流量计、16 路继电器模块
- 汇总现场数据并镜像到 Modbus 输入寄存器
- 接收远程参数维护命令
- 在手动模式下响应本地按键和远程继电器控制

### 2.2 升级态

#### 本地升级

`维护电脑 -> USB 转 485 -> USART3 -> STM32 Bootloader`

流程：

1. App 阶段写入解锁和进入 Bootloader 命令
2. STM32 软件复位
3. Bootloader 驻留并等待 YMODEM
4. `stm32_local_upgrade.py` 发送 `App.bin`
5. Bootloader 校验整包 `CRC32 + SHA-256 + App 向量表`
6. 校验通过后复位，再次启动时继续复核 `CRC32 + SHA-256 + App 向量表`
7. 复核通过后跳回 App

#### 远程升级

`升级服务器 -> TCP -> G780S 透传 -> USART3 -> STM32 Bootloader`

流程：

1. 远端先通过 App 阶段维护命令请求进入 Bootloader
2. Bootloader 通过自定义升级协议收包
3. `stm32_remote_upgrade.py` 按块发送镜像
4. Bootloader 擦写、校验并复位回 App

## 3. 主要功能

### 3.1 现场采集与控制

- PT100 温度采集，当前实际使用 CH4
- 称重模块采集，当前实际使用 CH3
- 流量计频率换算与累计量统计
- 16 路继电器输出控制
- 16 路继电器输入读取与去抖
- 本地按键控制
- 手动 / 自动模式切换

### 3.2 远程维护

- 远程修改采集周期、流量采样周期、DI 去抖时间
- 远程修改温差触发阈值和流量换算参数
- 远程切换控制模式
- 远程查看维护状态、错误码、配置序号
- 远程查看诊断寄存器、升级快照和通信异常计数

### 3.3 升级能力

- App 侧写入升级请求标志并复位
- Bootloader 状态页持久化
- 本地 YMODEM 升级
- 本地升级整包 `CRC32 + SHA-256` 校验
- Bootloader 启动前再次复核镜像完整性
- 基于 TCP 透传的远程升级
- 升级来源、状态、错误码诊断回读

## 4. 开发环境

- STM32 固件：`Keil MDK-ARM`
- Windows 工具：`.NET SDK 10`
- Python 脚本：`Python 3` + `pyserial`

建议额外准备：

- USB 转 RS485 串口工具
- Modbus 调试工具
- G780S 或等价透传链路

安装 Python 依赖：

```powershell
pip install pyserial
```

## 5. 编译与运行

### 5.1 STM32 固件

打开 `STM32/MDK-ARM/Project.uvprojx`，编译后重点产物是：

- `STM32/MDK-ARM/Objects/App.bin`

这个文件会被本地升级脚本和 OTA 工具直接使用。

### 5.2 Windows 上位机

```powershell
dotnet build Windows\Project.slnx
```

上位机能力：

- 自动刷新串口
- 轮询读取 `0x0000` 开始的输入寄存器
- 显示温度、重量、流量、DO/DI 状态
- 按位写入 `0x0015` 控制继电器

说明：

- 首次构建需要从 NuGet 拉取 `NModbus`、`NModbus.Serial`、`System.IO.Ports`、`System.Management`
- 在无外网环境下，若本机没有缓存包，`dotnet restore/build` 会失败

### 5.3 OTA 工具

```powershell
dotnet build OTA\Project.slnx
```

当前实现：

- 本地升级模式已接通
- 会先发送解锁报文和进入 Bootloader 报文
- 随后调用 `stm32_local_upgrade.py`
- 远程升级页面目前只保留界面，未接进按钮逻辑

### 5.4 Modbus CRC 工具

```powershell
dotnet build Modbus_CRC\Project.slnx
```

这个工具用于：

- 生成 `06` 单寄存器写入原始帧
- 自动计算 Modbus CRC16
- 导入已有报文并校正 CRC
- 方便直接粘贴到网络调试窗口发送

## 6. 脚本用法

### 6.1 本地升级脚本

```powershell
python stm32_local_upgrade.py run --port COM6 --baudrate 115200 --image STM32\MDK-ARM\Objects\App.bin
```

当前脚本行为：

- 自动计算整包 `CRC32`
- 自动计算整包 `SHA-256`
- 把 `image_size / image_crc32 / target_fw_version / image_sha256` 作为 YMODEM 头包元数据发送给 Bootloader
- YMODEM 数据包大小自动在 `128` / `1024` 字节之间切换

常用参数：

- `--port`：串口号，默认 `COM6`
- `--baudrate`：默认 `115200`
- `--timeout`：单步超时秒数
- `--target-fw-version`：目标固件版本号
- `--verbose`：打印 YMODEM 收发字节

本地升级当前实际校验链路：

- YMODEM 单包 `CRC16-CCITT/XMODEM`
- 升级状态页 `CRC16`
- Flash 写入后的回读校验
- 整包 `CRC32`
- 整包 `SHA-256`
- App 向量表有效性校验
- Bootloader 在 `DONE` 状态下每次上电再次复核 `CRC32 + SHA-256 + 向量表`

### 6.2 远程升级脚本

```powershell
python stm32_remote_upgrade.py --mode listen --host 0.0.0.0 --port 8899 --enter-bootloader --image STM32\MDK-ARM\Objects\App.bin
```

常用参数：

- `--mode listen|connect`：监听或主动连接
- `--host` / `--port`：TCP 监听或对端地址
- `--enter-bootloader`：先走 App 阶段 Modbus 解锁和进入 Bootloader
- `--slave-addr`：默认 `0x0A`
- `--chunk-size`：远程升级每包数据长度
- `--verbose`：打印完整帧

## 7. 通信约定

### 7.1 设备地址

- STM32 App 对外 Modbus 从站地址：`10 (0x0A)`
- 当前远程维护链路通过 `USART3 + G780S` 暴露

### 7.2 关键寄存器

- `0x0000 ~ 0x0015`：现场数据与继电器控制
- `0x0020 ~ 0x0028`：远程配置区
- `0x0030 ~ 0x0037`：维护控制与状态
- `0x0038 ~ 0x0059`：诊断寄存器

详细定义请看：

- [远程维护指令手册](远程维护指令手册.md)

## 8. 目录结构

```text
STM32/
├─ BSP/                板级驱动与业务模块
├─ Bootloader/         Bootloader 入口与升级流程
├─ Drivers/            HAL / CMSIS
├─ System/             usart、ymodem、定时器、基础系统层
├─ User/               App 主循环与应用逻辑
└─ MDK-ARM/            Keil 工程与编译输出

Windows/
└─ Project/            WPF 上位机源码

OTA/
└─ Project/            WPF 升级工具源码

Modbus_CRC/
└─ Project/            Modbus 原始帧工具源码
```

## 9. 建议阅读顺序

1. [远程维护指令手册](远程维护指令手册.md)
2. [第二阶段本地升级手册](第二阶段本地升级手册.md)
3. [第三阶段远程升级方案](第三阶段远程升级方案.md)
4. `STM32/User/main.c`
5. `STM32/Bootloader/`

## 10. 当前边界与注意事项

- Windows 上位机已经在维护，不应再按“历史遗留工具”理解
- OTA WPF 工具的远程升级按钮逻辑尚未接入，远程升级请直接用 Python 脚本
- Windows 上位机构建依赖外网 NuGet；离线环境下可能因为缺包无法编译
- Bootloader 与 App 通过升级状态页协同，不建议手工改动相关 Flash 地址定义
- 修改远程维护寄存器后，只有执行“提交保存”才会写入 Flash 生效
