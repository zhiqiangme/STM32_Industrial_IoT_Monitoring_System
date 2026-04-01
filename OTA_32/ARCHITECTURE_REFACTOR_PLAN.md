# STM32 OTA 项目架构重构方案

## 文档目的

本文件用于固化 `D:\Project\STM32_Mill\OTA_32` 的架构重构方案，避免后续上下文过长后丢失关键决策。

当前文档同时作为重构过程记忆文件使用。
Phase 1 已开始落地，文档需要持续反映实际工程状态。

## 已确认决策

1. 采用分阶段重构。
2. 顺序为：先拆后端与物理项目结构，保住现有功能；再推进前端 MVVM。
3. 允许引入 `CommunityToolkit.Mvvm`，默认认为本机网络与 NuGet 恢复可用。
4. 协议真值保持不变：现有魔法数字、寄存器地址、帧内容、时序参数在重构阶段原封不动搬迁，不做语义修改。
5. 实机验证由用户负责执行。

## 当前项目现状

当前代码不是“多项目待整理”，而是“单个 WPF 项目承载全部职责”。

现有目录核心内容如下：

- `Project/Project.csproj`
- `Project/MainWindow.xaml`
- `Project/MainWindow.xaml.cs`
- `Project/App.xaml`
- `Project/App.xaml.cs`
- `Project/SerialPortHelper.cs`
- `Project/SerialOperationGate.cs`
- `Project/LocalUpgradeService.cs`
- `Project/UpgradeAbSupport.cs`

当前职责混杂情况：

- `MainWindow.xaml.cs` 同时承担 UI 事件、串口列表刷新、PowerShell 调用、槽位识别、升级流程编排、日志输出。
- `UpgradeAbSupport.cs` 同时包含模型定义、镜像识别规则、Modbus 读槽位协议逻辑。
- `LocalUpgradeService.cs` 同时包含升级编排、YMODEM 发包逻辑、CRC 算法、固件摘要计算。
- `App.xaml` 仍通过 `StartupUri` 启动主窗口，尚未建立依赖注入入口。
- `MainWindow.xaml` 仍是典型 code-behind 事件绑定模式，尚未进入 MVVM。

结论：这次重构的关键不是“搬文件”，而是“先拆职责，再按项目边界落位”。

## 最终目标架构

最终目标保留 5 层结构，但按当前项目实际情况分两大阶段完成。

### 最终项目划分

#### 1. `OTA.Models`

职责：

- 纯数据实体
- 枚举
- 事件参数
- 不依赖任何业务实现

初步候选内容：

- `FirmwareSlot`
- `FirmwareImageInfo`
- `LocalUpgradeOptions`
- 后续需要时再补充升级状态、进度事件参数、错误码枚举

#### 2. `OTA.Protocols`

职责：

- 串口通道访问
- Modbus/设备寄存器读写协议
- YMODEM 包结构与发包辅助
- CRC/Checksum 算法

初步候选内容：

- `SerialPortHelper`
- `SerialOperationGate`
- 运行槽位读取协议实现
- YMODEM 包构造与控制字节处理
- `CRC16/CRC32/Modbus CRC`

#### 3. `OTA.Core`

职责：

- 升级业务编排
- 槽位与镜像校验规则
- 任务流程控制
- 对 UI 暴露业务接口

初步候选内容：

- `LocalUpgradeService` 的流程编排部分
- `UpgradeAbSupport` 中的业务判断部分
- 后续抽象出的升级协调器、镜像检查服务

#### 4. `OTA.ViewModels`

职责：

- 主界面状态管理
- 命令绑定
- 属性通知
- UI 与 Core 的桥接

初步候选内容：

- `MainViewModel`
- 后续如有需要的 `SettingsViewModel`
- 跨 ViewModel 消息类

#### 5. `OTA.UI`

职责：

- WPF 启动层
- 纯 XAML 视图
- 依赖注入装配
- 转换器、控件、资源

初步候选内容：

- `App.xaml`
- `App.xaml.cs`
- `MainWindow.xaml`
- 精简后的 `MainWindow.xaml.cs`
- `Assets`

## 依赖方向

必须保持单向依赖，禁止反向引用。

最终依赖方向如下：

`Models <- Protocols <- Core <- ViewModels <- UI`

补充说明：

- `UI` 在启动装配时可以引用 `Core` 和 `Protocols` 做 DI 注册，但业务调用路径仍应通过 `ViewModels` 进入。
- `Models` 不允许引用任何其他项目。
- `Protocols` 不能依赖 `Core` 或 `UI`。
- `Core` 不能依赖 `ViewModels` 或 `UI`。

## 重构原则

1. 先物理解耦，后 UI 解耦。
2. 先保持行为一致，再追求结构优雅。
3. 协议常量、串口帧、超时、寄存器地址不改语义。
4. 每一阶段都必须可编译。
5. 每一阶段都应尽量保留实机回归验证入口。
6. 单次改动聚焦一个目标，避免“架构升级”与“功能增强”混在一起。

## 分阶段实施计划

## Phase 0：基线冻结与重构准备

目标：

- 明确当前代码基线
- 固化本方案
- 为后续拆分建立追踪点

本阶段动作：

- 保留当前单项目结构不动
- 记录现有文件与职责混杂点
- 约定后续每阶段完成后都进行一次编译验证
- 约定涉及串口与 OTA 行为的验证由用户执行

完成标志：

- 本文档存在且作为后续唯一执行方案

用户参与：

- 无额外动作

## Phase 1：后端物理拆分，不引入 MVVM

目标：

- 在不改变现有 UI 使用方式的前提下，先把底层和核心逻辑从 WPF 项目中剥离出去
- 让 `UI` 不再直接承载全部业务实现

本阶段产物：

- 新解决方案或重构后的多项目解决方案
- 至少拆出 `Models`、`Protocols`、`Core`、`UI`
- `ViewModels` 项目可以先创建空壳，也可以延后到 Phase 2 再启用

推荐执行顺序：

1. 新建解决方案与目标项目。
2. 建立单向项目引用。
3. 先抽 `Models`。
4. 再抽 `Protocols`。
5. 再抽 `Core`。
6. 最后让 `UI` 重新引用新的 `Core` 与 `Models`。

### Phase 1 文件迁移策略

#### A. 直接迁入 `Models` 的内容

从现有代码中拆出以下纯数据类型：

- `FirmwareSlot`
- `FirmwareImageInfo`
- `LocalUpgradeOptions`

后续可能新增：

- 升级状态枚举
- 进度事件参数
- 错误码或结果对象

说明：

- 这些类型不能继续留在 `UpgradeAbSupport.cs` 或 `LocalUpgradeService.cs` 里混放。

#### B. 迁入 `Protocols` 的内容

现有以下文件整体方向正确，但内部可能需要继续细分：

- `SerialPortHelper.cs`
- `SerialOperationGate.cs`

从现有 `UpgradeAbSupport.cs` 中拆出的协议相关部分：

- Modbus 读当前运行槽位请求构造
- Modbus 回包解析
- 串口精确读取
- Modbus CRC 计算

从现有 `LocalUpgradeService.cs` 中拆出的协议相关部分：

- YMODEM 控制字节定义
- YMODEM 包构造
- 包确认与控制字节等待
- CRC16 XMODEM
- CRC32

说明：

- `Protocols` 只负责“怎么和设备通信”“怎么构包验包”，不负责“什么时候发什么业务步骤”。

#### C. 迁入 `Core` 的内容

从现有 `LocalUpgradeService.cs` 中保留到 `Core` 的内容：

- 升级流程编排
- 固件摘要记录
- 解锁、切换 Bootloader、等待握手、发送文件的流程顺序

从现有 `UpgradeAbSupport.cs` 中保留到 `Core` 的内容：

- 镜像槽位识别策略
- 文件名与向量表交叉校验
- 推荐镜像文件名判断
- 镜像提示文本生成

建议在本阶段把当前“大而全”的静态类拆成更清晰的职责，例如：

- `FirmwareInspectionService`
- `RunningSlotReader`
- `YModemProtocolService`
- `LocalUpgradeCoordinator`

说明：

- 是否在 Phase 1 就改成接口注入，可以灵活处理。
- 如果一次性抽接口导致改动面太大，可以先保留部分类为具体实现，先把项目边界立住。

#### D. `UI` 保留但瘦身

本阶段 `MainWindow.xaml` 和 `MainWindow.xaml.cs` 可以暂时继续保留事件驱动，不强行一步切 MVVM。

但应尽量把以下内容移出 `MainWindow.xaml.cs`：

- 升级流程编排
- 串口底层访问
- 镜像识别核心逻辑
- 协议构包与校验细节

`MainWindow.xaml.cs` 在 Phase 1 结束后理想上只保留：

- 事件入口
- 读取界面输入
- 调用 Core
- 回填界面状态
- 日志桥接

### Phase 1 风险点

1. `UpgradeAbSupport.cs` 不能整文件搬迁，必须拆。
2. `LocalUpgradeService.cs` 不能整文件搬迁，必须拆。
3. `MainWindow.xaml.cs` 当前体积过大，拆分时容易遗漏隐式状态依赖。
4. 静态类较多，若直接上接口注入，改动面会瞬间扩大。

### Phase 1 验收标准

1. 解决方案按目标项目完成物理拆分。
2. 项目引用方向正确。
3. 程序可编译通过。
4. 本地升级主流程行为不变。
5. 用户完成一次实机烟测：
   - 串口可识别
   - 当前槽位可读取
   - 能进入 Bootloader
   - YMODEM 发送可走通

用户参与：

- 执行实机烟测
- 若某一步行为与旧版本不一致，提供日志与现象

## Phase 2：建立 DI 入口，稳定 UI 与 Core 边界

目标：

- 去掉 `StartupUri` 直启窗口方式
- 由 `App.xaml.cs` 接管对象装配
- 为后续 MVVM 做准备

本阶段动作：

- 将 `App.xaml` 改为不直接指定 `StartupUri`
- 在 `App.xaml.cs` 中建立 `ServiceCollection`
- 注册 `Protocols`、`Core`、`UI`
- 由容器创建 `MainWindow`

说明：

- 此阶段仍可暂不引入 `MainViewModel`
- 重点是把启动方式从“WPF 直接 new 窗口”切成“容器装配”

验收标准：

1. 程序能从 DI 容器启动。
2. 主窗口功能行为不变。
3. 编译通过。

用户参与：

- 启动程序并完成一次基本功能点击回归

## Phase 3：前端 MVVM 化

目标：

- 把 `MainWindow.xaml.cs` 中的界面逻辑迁移到 `ViewModels`
- 引入 `CommunityToolkit.Mvvm`

本阶段动作：

1. 创建 `OTA.ViewModels` 项目。
2. 引入 `CommunityToolkit.Mvvm`。
3. 创建 `MainViewModel`。
4. 把以下内容迁入 `MainViewModel`：
   - 当前模式状态
   - Busy 状态
   - 串口列表数据源
   - 当前槽位文本
   - 推荐镜像文本
   - 镜像路径
   - 日志文本
   - 启动升级命令
   - 切换本地/远程模式命令
5. 把 `MainWindow.xaml` 改成绑定方式。
6. 把 `MainWindow.xaml.cs` 精简为只保留视图层必要代码。

说明：

- 自定义标题栏拖动、窗口消息钩子这类纯视图行为，可以继续留在 `MainWindow.xaml.cs`。
- 业务无关但强依赖 WPF 可视树的逻辑，不必强行塞进 ViewModel。

### Phase 3 推荐保留在视图层的内容

- `WndProc`
- 标题栏拖动
- 设备变更消息挂钩
- 纯样式和视觉行为

### Phase 3 推荐迁入 `MainViewModel` 的内容

- 启动升级逻辑入口
- 串口列表状态
- 当前槽位显示文本
- 推荐镜像文案
- 镜像路径状态
- 状态栏文字
- 日志聚合
- 模式切换状态

### Phase 3 风险点

1. `DispatcherTimer` 与 ViewModel 的线程边界要处理清楚。
2. 日志追加和 UI 线程切换不能粗暴照搬。
3. 如果先前 Phase 1 没有把 Core 边界清干净，MVVM 会被拖慢。

### Phase 3 验收标准

1. `MainWindow.xaml.cs` 明显缩小。
2. `MainWindow.xaml` 主要依靠绑定和命令。
3. 主流程行为与 Phase 2 保持一致。
4. 编译通过。

用户参与：

- 执行完整 UI 回归与实机验证

## Phase 4：清理与增强

目标：

- 在结构稳定后再做命名、测试、抽象和远程升级扩展准备

可选动作：

- 统一命名空间与程序集名称
- 补充接口定义
- 增加结果对象与错误码
- 为远程升级预留服务接口
- 增加单元测试或集成测试入口
- 梳理日志输出接口

说明：

- 本阶段不应和前面主拆分工作混在一起。
- 必须等结构稳定后再做“优雅化”。

## 现有文件到目标结构的映射

### 现有文件：`Project/SerialPortHelper.cs`

目标：

- Phase 1 迁入 `Protocols`

### 现有文件：`Project/SerialOperationGate.cs`

目标：

- Phase 1 迁入 `Protocols`

### 现有文件：`Project/UpgradeAbSupport.cs`

目标：

- 不整文件迁移
- 拆分后分别进入 `Models`、`Protocols`、`Core`

拆分方向：

- `FirmwareSlot` -> `Models`
- `FirmwareImageInfo` -> `Models`
- 槽位读寄存器协议 -> `Protocols`
- 镜像识别与推荐规则 -> `Core`

### 现有文件：`Project/LocalUpgradeService.cs`

目标：

- 不整文件迁移
- 拆分后分别进入 `Models`、`Protocols`、`Core`

拆分方向：

- `LocalUpgradeOptions` -> `Models`
- YMODEM 包与 CRC -> `Protocols`
- 升级流程编排 -> `Core`

### 现有文件：`Project/MainWindow.xaml`

目标：

- 保留在 `UI`
- Phase 3 开始逐步转为绑定驱动

### 现有文件：`Project/MainWindow.xaml.cs`

目标：

- Phase 1 先瘦身
- Phase 3 大幅迁移到 `ViewModels`

### 现有文件：`Project/App.xaml`

目标：

- 保留在 `UI`
- Phase 2 去掉 `StartupUri`

### 现有文件：`Project/App.xaml.cs`

目标：

- 保留在 `UI`
- Phase 2 建立 DI 入口

### 现有文件：`Assets`

目标：

- 保留在 `UI`

## 需要用户参与的节点

以下事项不能由代码重构自动完成，需要用户配合：

1. 实机验证串口、槽位读取、Bootloader 切换和 OTA 传输。
2. 如 NuGet 恢复出现企业网络或证书问题，用户处理本机环境。
3. 如果后续发现协议行为与旧版本不一致，用户提供板端现象和串口日志。

## 当前不做的事情

为避免一次改动过大，以下内容暂不进入当前轮次：

- 修改协议语义
- 修改寄存器地址或帧格式
- 新增远程 OTA 功能
- 优化界面样式
- 批量补单元测试
- 对全部静态类一次性接口化

## 后续执行规则

后续实际开始改造时，默认按以下规则执行：

1. 每次只推进一个 Phase 或一个明确子任务。
2. 每次修改后先保证可编译，再讨论下一步。
3. 先保持行为一致，再做结构优化。
4. 若发现本文档与现状冲突，以“保留旧行为”为优先原则修订本文档。

## 下一步建议

真正开始动代码时，建议从 Phase 1 开始，优先做下面三件事：

1. 创建新解决方案与 `Models/Protocols/Core/UI` 项目骨架。
2. 先把 `FirmwareSlot`、`FirmwareImageInfo`、`LocalUpgradeOptions` 抽到 `Models`。
3. 再拆 `UpgradeAbSupport.cs` 和 `LocalUpgradeService.cs`，不要整文件搬家。

## 最终文件结构示意

按当前方案，重构完成后会是多项目结构，默认是 5 个 `.csproj`。

推荐目录示意如下：

```text
OTA_32/
├── OTA.sln
├── ARCHITECTURE_REFACTOR_PLAN.md
├── Directory.Build.props
├── Assets/
│   ├── OTA.ico
│   ├── OTA.png
│   └── ...
├── OTA.Models/
│   ├── OTA.Models.csproj
│   ├── Enums/
│   ├── Entities/
│   └── Events/
├── OTA.Protocols/
│   ├── OTA.Protocols.csproj
│   ├── Channels/
│   ├── Algorithms/
│   └── Packets/
├── OTA.Core/
│   ├── OTA.Core.csproj
│   ├── Services/
│   ├── Interfaces/
│   └── StateMachine/
├── OTA.ViewModels/
│   ├── OTA.ViewModels.csproj
│   ├── MainViewModel.cs
│   └── Messages/
└── OTA.UI/
    ├── OTA.UI.csproj
    ├── App.xaml
    ├── App.xaml.cs
    ├── MainWindow.xaml
    ├── MainWindow.xaml.cs
    ├── Views/
    ├── Controls/
    └── Converters/
```

## 当前工程状态补充

截至当前轮重构，仓库已经落地以下工程骨架：

- `OTA.Models`
- `OTA.Protocols`
- `OTA.Core`
- `OTA.ViewModels`
- `OTA.UI`
- `OTA.sln`
- `OTA.slnx`

当前命令行构建说明：

- 单项目构建可直接使用 `dotnet build OTA.UI\OTA.UI.csproj`
- 解决方案构建当前建议使用 `dotnet build OTA.slnx -m:1`
- 兼容保留命令：`dotnet build OTA.sln -m:1`

说明：

- 这是针对当前 `.NET 10 SDK / MSBuild` 在该仓库项目图上的并行构建异常做的临时规避。
- 该约束不影响本轮架构拆分本身，也不改变协议和业务行为。
- `OTA.sln` 当前保留作为过渡入口，至少一周内不删除；后续默认以 `OTA.slnx` 为主。

Phase 2 已完成的内容：

- 已新增标准 `OTA.slnx`，并完成 5 个项目挂载。
- `OTA.UI/App.xaml` 已去掉 `StartupUri`，改为显式 `App` 启动入口。
- `OTA.UI/App.xaml.cs` 已接入基础 DI，负责注册 `PortDiscoveryService`、`LocalUpgradeCoordinator`、`MainViewModel` 和 `MainWindow`。
- `MainWindow.xaml.cs` 中的串口枚举与设备描述查询已抽到 `OTA.Core/Services/PortDiscoveryService.cs`。
- `MainWindow.xaml.cs` 中的升级前参数校验、镜像识别、槽位探测结果整合、推荐镜像路径判断已抽到 `OTA.Core/Services/LocalUpgradeCoordinator.cs`。
- `OTA.Models` 已补充 `LocalSerialSettings`、`PortOption`、`RunningSlotRefreshResult`、`LocalUpgradePreparation` 等跨层数据对象。

Phase 3 当前已落地的内容：

- `OTA.ViewModels` 已正式启用，并引入 `CommunityToolkit.Mvvm`。
- 已新增 `OTA.ViewModels/MainViewModel.cs`，承接主界面的模式状态、串口列表、镜像路径、槽位提示、状态栏文字、日志文本和升级命令。
- `OTA.UI/MainWindow.xaml` 已改为以绑定和命令为主驱动。
- `OTA.UI/MainWindow.xaml.cs` 已收缩为视图桥接层，只保留设备变更消息、空闲轮询定时器、文件选择对话框、标题栏拖动和窗口关闭等纯 UI 行为。
- `MainViewModel` 已通过轻量消息对象向视图层上送参数错误和执行失败提示，避免直接依赖 WPF 弹窗。
- `OTA.slnx` 已可完成 `dotnet build OTA.slnx -m:1` 构建。

说明：

- 是，按这个方案会有 5 个 `.csproj`。
- 命名会是 `OTA.Models.csproj`、`OTA.Protocols.csproj`、`OTA.Core.csproj`、`OTA.ViewModels.csproj`、`OTA.UI.csproj`。
- `OTA.UI` 就是最终的 WPF 启动项目。
- `Assets` 可以先继续放在解决方案根目录，优先减少资源路径改动；后续如果需要再并入 `OTA.UI`。
