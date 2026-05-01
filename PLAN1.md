# STM32_Mill 透明传输 + 保留远程 OTA 方案

## Summary
- 放弃 `PLAN4.md` 里的“G780S 边缘采集/边缘 JSON 组包”路线，改为“`STM32_Mill` 自己组织业务协议，`G780S` 只做透明传输，服务器负责 MQTT/API/WebSocket”。
- 远程 OTA 保留现有 `USR-VCOM + 虚拟串口 + Bootloader + YMODEM` 链路，不改 Bootloader 传输协议。
- 日常运行和 OTA 采用“两种网关工作状态”，不要求同时在线：平时跑业务透传，升级时临时切到 OTA 透传。

## Key Changes
- `PLAN4.md` 已完成项只回调边缘采集相关部分，不整单回退。
  - 回调服务端的 `G780S 边缘 JSON` 适配逻辑，也就是最近提交里新增的 `g780s_mapper.js` 和 `state.js` 中对边缘报文的映射接入。
  - 保留与边缘采集无关的已完成项，尤其是用户种子账号、现有 MQTT 基础能力、`cloud_ack`、历史数据、在线状态恢复、Docker/GitHub Actions 发布链路。
  - Flutter“改名/四温度适配”不要按 `PLAN4.md` 视为已完成，后续按当前真实代码重新做一遍最小适配。

- `STM32_Mill` 固件改为“业务 JSON + OTA 保留”双态架构。
  - 日常业务态：`USART3` 面向 `G780S` 输出单行 JSON，上报测量值、状态、报警、继电器状态；下行接收控制命令。
  - 下行最小命令固定为 `relay_set` 和 `ota_prepare`。`relay_set` 控制继电器；`ota_prepare` 负责停业务、回确认、进入升级准备态。
  - 保留现有 Bootloader、YMODEM、升级口令/进入升级流程，不把 OTA 改成 MQTT 分片下载，不新增新的固件传输协议。
  - 现有继电器/维护相关内部能力继续复用，新增 JSON 协议只是外层封装，不推翻底层控制逻辑。

- `D:\Project\HTML\server` 继续沿用现有 `pipe-monitor-api` 路线，不新建第二套后端。
  - 保留当前 MQTT 接入、入库、WebSocket 推送、`cloud_ack`、历史查询、告警查询。
  - 新增设备命令接口，由 API 发 MQTT 下行命令到设备；最小范围只做继电器控制和 OTA 准备。
  - 设备上行 topic/down topic 在 v1 保持当前基线命名，不先做大规模重命名，先保证链路打通。
  - GitHub Actions + Docker 发布方式保持不变，只更新服务逻辑和环境变量。

- 客户端沿用现有 Flutter 基线，不新起项目。
  - 先只做你当前明确需要的两件事：显示数据、控制继电器。
  - 页面保留现有数据看板思路，替换成 `Mill` 的字段与状态。
  - 控制继电器走 API，不让手机直连 MQTT，不碰 OTA 工具链。
  - OTA 在 v1 不放进手机端执行，只保留状态展示或维护入口文案即可。

- OTA 共存方案固定为“临时切换模式”，不做同口并发。
  - 正常运行：`G780S` 工作在业务透传模式，承载 JSON 上报/下行控制。
  - 触发升级：服务端先下发 `ota_prepare`，设备停业务并返回确认。
  - 之后切换 `G780S` 到现有 `USR-VCOM`/虚拟串口升级通道，继续使用当前 WPF OTA 工具完成 YMODEM 升级。
  - 升级完成后设备重启，再切回业务透传模式。
  - 如果 `G780S` 不能稳定远程自动切换配置，v1 默认接受“人工切换或半自动切换”，先保留 OTA 能力，再谈自动化。

## Test Plan
- 固件业务态测试：
  - 周期数据能通过 `G780S -> MQTT -> API -> WebSocket/HTTP` 到手机端显示。
  - `cloud_ack` 正常返回；云端失败时设备按既有策略进入待补传/重试。
  - `relay_set` 能正确控制继电器并回 `ack`。

- OTA 回归测试：
  - 日常业务态下执行 `ota_prepare`，设备能进入升级准备态并停止业务上报。
  - 切到 `USR-VCOM` 后，现有 OTA 工具可继续完成解锁、进 Bootloader、YMODEM 传输。
  - 升级完成后设备恢复业务态，并重新接入 MQTT/API。

- 服务端/发布测试：
  - `pipe-monitor-api` 新增命令接口能正确鉴权、发布 MQTT、记录命令回执。
  - GitHub Actions 构建、Docker 部署、`/health` 检查维持现有通过标准。

## Assumptions
- v1 不要求“业务上报”和“远程 OTA”在同一时刻同时可用，升级期间允许暂停实时数据。
- v1 继续保留现有 `USR-VCOM` 远程 OTA 工具链，不改 Bootloader 传输协议。
- `PLAN4.md` 中只回调边缘采集相关代码，不回调无关的账号、部署、基础服务能力。
- 当前最近几次提交的判断结论是：需要回调的是边缘映射接入，不是整套服务器能力重做。
