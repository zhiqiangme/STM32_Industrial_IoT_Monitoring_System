# 项目改造方案 v4-final（已锁定）

> 目标：把 STM32 磨坊系统从「有人云」迁到自建 MQTT + API 服务器，并把 Flutter 客户端适配为「磨坊系统 / com.varka.mill」。

---

## 关键决策（已确认，不再讨论）

| 项 | 决策 |
|---|---|
| 架构路线 | **路线 A**：保留 STM32 作 Modbus 聚合从机（addr=10），G780s 只轮询 STM32 一台 |
| MQTT TLS | **本期一起做**（阶段 5 并入主路径） |
| 多设备 | 单设备 FM001，**不做** `/api/devices` / 设备表 / 切换 UI |
| 用户体系 | admin + user 两个固定账号，后台 seed，**不开放注册** |
| Flutter 身份 | name=`mill`，bundle id=`com.varka.mill`，应用名"磨坊系统"，图标默认 |
| G780s 工作模式 | Modbus 主站轮询 STM32 + **边缘 JSON 组包**（`AT+EDGERPTMOD=1`），不再透传二进制 |
| API 端解码 | **不写** Modbus 解码器；只写 G780s 边缘 JSON → 内部 tele schema 的字段映射器 |

---

## G780s 关键能力（手册已查实）

- 硬件 1.0.0 + 固件 ≥ V2.4.01.000000.0000 即支持边缘采集 + JSON 组包，FOTA 可升
- MQTT 3.1.1 双路通道，QoS 0/1/2，LWT/Retain/KeepAlive 全支持
- MQTTS / TLS 1.2，端口 8883，支持 PEER（验服务器）/ ALL（双向）
- Modbus RTU 主站，最多 200 点，支持 bool / int16 / int32(ABCD/CDAB) / float
- JSON 上报格式：`{"params":{"dir":"up","id":"<SN>","r_data":[{"name":"node0101","value":"35","err":"0"},...]}}`

---

## 链路图

```
传感器（PT100×4 / 称重×4 / 流量 / 继电器）
   │ Modbus RTU
   ▼
温度变送器(addr 1) / 称重变送器(addr 3) / 继电器模块
   │ RS485 共线
   ▼
STM32 F1 (RT-Thread, Modbus master 给变送器；slave addr=10 对 G780s)
   │ USART3 / Modbus RTU
   ▼
G780s（Modbus master 轮询 STM32 + 边缘 JSON 组包 + MQTT 客户端）
   │ MQTT 8883 (TLS), topic: device/FM001/up
   │ payload: {"params":{"dir":"up","id":"<SN>","r_data":[...]}}
   ▼
mqtt.varka.cn (Mosquitto + 密码 + ACL + LE 证书)
   │
   ▼
pipe-monitor-api (Node)
   ├─ MQTT 订阅 device/+/up
   ├─ g780s_mapper.js → 内部 tele schema
   ├─ users seed (admin / user)
   └─ 现有写库 + WS 广播流程
                                                    │
                                                    ▼
                                            api.varka.cn (Nginx + LE)
                                                    ▼
                                  Flutter "磨坊系统" (com.varka.mill)
```

---

## 阶段 0：准备

- [ ] G780s FOTA 升级到固件 ≥ V2.4.01.000000.0000（用户硬件操作）
- [ ] VPS 上配置 mosquitto `passwordfile` + `aclfile`（FM001 设备凭据 + pipe-monitor-api 服务凭据）
- [ ] 本地 `docker compose --profile private-mqtt --profile storage up -d` 跑通
- [ ] 用 `mosquitto_pub` 模拟一帧 G780s 边缘 JSON 投到 `device/FM001/up`，验证 g780s_mapper 的字段映射 / 字节序 / 倍率

预估 0.5 天。

---

## 阶段 1：G780s 配置（纯 AT，不写代码）

### 1.1 MQTT 客户端（通道 1）
```
AT+MQTTCFG=1,broker=mqtt.varka.cn,port=1883,clientid=dr154-fm001,keepalive=60,cleansession=1
AT+MQTTUSER=1,dr154-fm001
AT+MQTTPSW=1,<密码>
AT+MQTTWILL=1,1,device/FM001/up,1,0,{"t":"offline"}
AT+MQTTPUBTP=1,1,device/FM001/up,1,0
AT+MQTTSUBTP=1,1,device/FM001/down,1
AT+MQTTMOD=1,0
```

### 1.2 边缘采集 + JSON 组包
```
AT+EDGEEN=1
AT+EDGERPTMOD=1
AT+COLLECTTIME=2
AT+POLLTIME=100
AT+POLLTIMEOUT=300
```

### 1.3 点表（16 项，对应 STM32 寄存器布局 G780s.h:10-25）

| name | slave | fc | reg | type | 倍率 | 上报 |
|---|---|---|---|---|---|---|
| `seq` | 10 | 0x03 | 0x0000 | uint16 | 1 | 直接 |
| `temp0` | 10 | 0x03 | 0x0001 | int16 | 0.1 | 直接 |
| `temp1` | 10 | 0x03 | 0x0002 | int16 | 0.1 | 直接 |
| `temp2` | 10 | 0x03 | 0x0003 | int16 | 0.1 | 直接 |
| `temp3` | 10 | 0x03 | 0x0004 | int16 | 0.1 | 直接 |
| `weight0` | 10 | 0x03 | 0x0005 | int32 ABCD | 1 | 直接 |
| `weight1` | 10 | 0x03 | 0x0007 | int32 ABCD | 1 | 直接 |
| `weight2` | 10 | 0x03 | 0x0009 | int32 ABCD | 1 | 直接 |
| `weight3` | 10 | 0x03 | 0x000B | int32 ABCD | 1 | 直接 |
| `flow` | 10 | 0x03 | 0x000D | uint16 | 待 STM32 端核 | 直接 |
| `total` | 10 | 0x03 | 0x000E | uint32 ABCD | 待核 | 直接 |
| `valid` | 10 | 0x03 | (扩展位) | uint16 bitmask | 1 | 直接 |

> ⚠️ 字节序（ABCD / CDAB）需在阶段 0 用一帧已知值反推确认；STM32 端默认大端则保 ABCD，否则切 CDAB。

### 1.4 上线抓包验证
- 抓 mosquitto `-v` 日志确认 JSON 格式正确
- 字节序、倍率核对通过

预估 1 天。

---

## 阶段 2：服务端 API 适配

### 2.1 新增 `pipe-monitor-api/src/g780s_mapper.js`
- `isG780sEdgePayload(payload)` 判别 G780s 边缘 JSON 形态
- `mapG780sEdgePayload(payload, fallbackDeviceId)` 输出兼容现有 Measurement schema 的 tele 对象
- err≠0 字段置 null；valid 缺省时按各点 err 拼简化 mask

### 2.2 改 `state.js` ingest
- `JSON.parse` 后立即调 `isG780sEdgePayload` 分流，转出后再走原 switch 分支
- 保留对 `t:"tele"` 直发设备的兼容（未来其他 DTU）

### 2.3 用户 seed
- 启动时若 `users` 表为空 → 读 env 创建 admin / user
- env：`SEED_ADMIN_USER` / `SEED_ADMIN_PASS` / `SEED_USER_USER` / `SEED_USER_PASS`
- 幂等（仅空表插入）

### 2.4 不做
- ❌ Modbus 二进制解码器
- ❌ `devices` 表 / `/api/devices`
- ❌ 表结构 / db.js schema 变更

预估 0.5 天。

---

## 阶段 3：Flutter 适配

### 3.1 项目身份
- `pubspec.yaml`：`name: project` → `name: mill`，description "磨坊系统"
- `MaterialApp.title` / 各页 AppBar → "磨坊系统"
- Android：`android/app/build.gradle` `applicationId = "com.varka.mill"`；`AndroidManifest.xml` `android:label="磨坊系统"`
- iOS：`ios/Runner/Info.plist` `CFBundleDisplayName=磨坊系统`、`CFBundleName=mill`；`project.pbxproj` 三处 `PRODUCT_BUNDLE_IDENTIFIER=com.varka.mill`
- 图标默认 ✅

### 3.2 数据模型适配
- `Measurement.temp` 收为 4 通道（去掉 7 通道硬假设）
- Dashboard / History 温度卡片与图表系列同步收 4 条
- `pres` / `v` / `heart_count` 值为 0/null 时隐藏对应卡片

### 3.3 复核
- 登录页若有注册入口则隐藏

### 3.4 不做
- ❌ 设备选择器（保留 `Env.deviceId='FM001'`）

预估 0.5 天。

---

## 阶段 4：CI / 部署

### 4.1 GitHub Secrets 新增
- `JWT_SECRET`（必须）
- `SEED_ADMIN_USER` / `SEED_ADMIN_PASS`
- `SEED_USER_USER` / `SEED_USER_PASS`
- `MYSQL_ROOT_PASSWORD` / `MYSQL_DATABASE` / `MYSQL_USER` / `MYSQL_PASSWORD`
- `MQTT_USERNAME` / `MQTT_PASSWORD`（pipe-monitor-api 连本地 mosquitto）
- `MOSQUITTO_DEVICE_PWD`（FM001 设备凭据，VPS 上渲染 passwordfile 用）

### 4.2 改 `.github/workflows/deploy-server.yml`
- rsync 前 ssh 渲染 `/opt/pipe-monitor/.env`（`.env` 已在 rsync exclude ✅）
- VPS 上用 `mosquitto_passwd -c -b` 渲染 `mqtt/passwordfile`
- 健康检查保留 `/health`

### 4.3 端到端验证
- push main → Action 通过
- `curl https://api.varka.cn/health` 返回 200
- Flutter 登录 admin / user 都能进
- Dashboard 出实时数据

预估 0.5 天。

---

## 阶段 5：MQTT TLS（本期一起做）

- [ ] mosquitto 加 8883 listener，挂 LE 证书
- [ ] certbot 签 `mqtt.varka.cn`（HTTP-01 或 DNS-01）
- [ ] G780s AT：`AT+SSLCFG=1,1` + `AT+SSLAUTH=1,1`（PEER）+ `AT+SSLCRT=1,0,<LE root CA>`
- [ ] 端口切到 8883
- [ ] 防火墙关闭 1883 公网

预估 0.5 天。

---

## 工作量合计

| 阶段 | 预估 |
|---|---|
| 0 准备 + 固件升级 | 0.5 天 |
| 1 G780s 配置 + 联调 | 1 天 |
| 2 API 映射 + seed | 0.5 天 |
| 3 Flutter 重命名 + UI 收敛 | 0.5 天 |
| 4 CI secrets + .env 渲染 | 0.5 天 |
| 5 TLS | 0.5 天 |

**主路径合计 ≈ 3.5 天**

---

## 任务清单（共 18 项，状态实时同步）

| # | 状态 | 任务 | 责任侧 |
|---|---|---|---|
| 1 | ⬜ pending | G780s FOTA 升级到固件 ≥ V2.4.01.000000.0000 | 用户硬件 |
| 2 | ⬜ pending | VPS 上配置 mosquitto passwordfile + aclfile（FM001 + pipe-monitor-api 两组凭据） | 部署 |
| 3 | ⬜ pending | 本地 docker-compose 跑通，用 mosquitto_pub 模拟 G780s JSON 上行，验证字段映射 | 联调 |
| 4 | ⬜ pending | G780s AT 配置：MQTT 客户端（broker/clientid/user/pass/LWT/pub/sub topic） | 用户硬件 |
| 5 | ⬜ pending | G780s AT 配置：边缘采集启用 + JSON 组包模式 + 周期/间隔/超时 | 用户硬件 |
| 6 | ⬜ pending | G780s 录入 16 项点表（seq/temp[0-3]/weight[0-3]/flow/total/valid），与 STM32 寄存器布局对齐 | 用户硬件 |
| 7 | ⬜ pending | 上线抓包验证：mosquitto -v 看到完整 JSON，字节序/倍率正确 | 联调 |
| 8 | ✅ done | 新增 `pipe-monitor-api/src/g780s_mapper.js`：r_data → Measurement schema | 代码 |
| 9 | ✅ done | 改 pipe-monitor-api MQTT 订阅入口：`isG780sEdgePayload` 分流后转 tele | 代码 |
| 10 | ✅ done | pipe-monitor-api 启动时若 users 表为空，按 env seed admin / user 两个账号 | 代码 |
| 11 | ✅ done | Flutter 项目改名：name=mill / 应用名=磨坊系统 / bundle id=com.varka.mill（Android-only，无 iOS 目录） | 代码 |
| 12 | ✅ done | Flutter Measurement 模型 temp 收为 4 通道，Dashboard / History UI 同步收 4 条 | 代码 |
| 13 | 🟡 in_progress | Flutter UI：pres / v / heart_count 字段值为 0/null 时隐藏卡片 | 代码 |
| 14 | ⬜ pending | Flutter 复核：登录页若有注册入口则隐藏（已初步确认无注册入口，仅复核） | 代码 |
| 15 | ⬜ pending | GitHub Secrets 新增：JWT_SECRET / SEED_* / MYSQL_* / MQTT_* / MOSQUITTO_DEVICE_PWD | 部署 |
| 16 | ⬜ pending | 改 deploy-server.yml：rsync 前 ssh 渲染 /opt/pipe-monitor/.env + 在 VPS 渲染 mosquitto passwordfile | 部署 |
| 17 | ⬜ pending | push main 触发部署，验证 /health 200 + Flutter 登录 + Dashboard 出数 | 联调 |
| 18 | ⬜ pending | mosquitto 加 8883 + LE 证书 + AT+SSLCFG 切 TLS + 关闭 1883 公网 | 部署+硬件 |

---

## 已完成详情

### ✅ 任务 8 — G780s 边缘 JSON 映射器
**文件**：`D:/Project/HTML/server/services/pipe-monitor-api/src/g780s_mapper.js`（新增）
- `isG780sEdgePayload(payload)`：判别 `{params:{dir:"up", r_data:[...]}}` 形态
- `mapG780sEdgePayload(payload, fallbackDeviceId)`：按 name 索引出 seq / temp[0-3] / weight[0-3] / flow / total / valid，输出兼容 Measurement schema 的 tele 对象
- 温度倍率 0.1，err≠"0" 字段置 null，valid 缺省时按各点 err 拼简化 mask

### ✅ 任务 9 — 接入订阅入口
**文件**：`D:/Project/HTML/server/services/pipe-monitor-api/src/state.js`（修改）
- 顶部 `import { isG780sEdgePayload, mapG780sEdgePayload } from "./g780s_mapper.js"`
- `JSON.parse` 后立即检测 G780s 形态并归一化，下游 switch 分支无需感知设备厂商
- 保留对 `t:"tele"` 直发设备的兼容

### ✅ 任务 10 — 用户 seed
**文件**：
- `D:/Project/HTML/server/services/pipe-monitor-api/src/seed_users.js`（新增）
- `D:/Project/HTML/server/services/pipe-monitor-api/src/config.js`（修改：增 `seed.admin` / `seed.user`）
- `D:/Project/HTML/server/services/pipe-monitor-api/src/server.js`（修改：`createDbStore` 后调 `seedUsers`）

env 约定：`SEED_ADMIN_USER` / `SEED_ADMIN_PASS` / `SEED_USER_USER` / `SEED_USER_PASS`，可选 `SEED_ADMIN_DISPLAY` / `SEED_USER_DISPLAY`。仅在用户名不存在时插入，幂等；env 缺省或 db 未启用则 warn 跳过。

### ✅ 任务 11 — Flutter 项目改名
- `flutter/pubspec.yaml`：`name: project` → `name: mill`，描述改为"磨坊系统"
- `flutter/android/app/build.gradle.kts`：`namespace` + `applicationId` → `com.varka.mill`
- Kotlin 源码目录从 `kotlin/com/varka/pipemonitor/` 移到 `kotlin/com/varka/mill/`，`MainActivity.kt` 包声明同步
- `flutter/android/app/src/main/AndroidManifest.xml`：`android:label="磨坊系统"`
- `flutter/lib/app.dart`、`lib/ui/core/shell/home_shell.dart`、`lib/ui/user/view/user_page.dart`：标题"磨坊系统"
- `flutter/web/manifest.json` + `flutter/web/index.html`：所有"管道监控系统" → "磨坊系统"
- `flutter/Flutter_Reinstall_Clean.bat`：`adb uninstall com.varka.pipemonitor` → `com.varka.mill`
- `FlowmeterApp` 类名保留（不影响用户可见 UI）
- iOS 跳过：项目无 `ios/` 目录

### ✅ 任务 12 — 温度通道收为 4 路
- `flutter/lib/data/models/measurement.dart`：`temperatures` 注释改为"4 路 PT100（T0-T3）"，`fromJson` 中 `List.generate(7, ...)` 改为 `List.generate(4, ...)`
- `flutter/lib/data/models/history_point.dart`：`HistoryField` 枚举去掉 `t4 / t5 / t6`；`fromJson` switch 分支同步收
- `flutter/lib/ui/dashboard/view/dashboard_page.dart`：`temperatureTiles` 由 7 改为 4；注释"7 路温度"改为"4 路温度"
- `flutter/lib/data/services/mock_api_service.dart` + `mock_realtime_service.dart`：mock 数据同步收 4 通道

---

## 已发现并记录的非计划信息

- Flutter 项目原本只有 Android 平台（无 `ios/` 目录），故所有 iOS 相关步骤跳过
- 原 namespace `com.varka.pipemonitor` 表明该模板源自 Varka 的"管道监控"项目（与服务端 pipe-monitor-api 同源）
- Dart 代码全部使用相对 import，无 `package:project/...` 引用，所以 pubspec name 改动**不影响**任何 import 语句
- pipe-monitor-api 的 `db.js` 已带 `findUserByUsername` / `createUser`，seed 直接复用，不需新增 db API
- mosquitto 现有 `aclfile.template` 已有 FM001 条目（设备 user `dr154-fm001` 上行 `device/FM001/up`、下行 `device/FM001/down`），与本期 topic 完全一致，无需改动
- pipe-monitor-api 的 `state.js` 还有一条 gateway 离线告警分支（订阅 `device/+/gateway/status`），用 LWT 实现 — 阶段 1 配置 G780s LWT 时可以选择把遗嘱发到该 topic 触发告警

---

## 下次会话接续点

**当前进行中**：任务 13 — Flutter UI 隐藏未使用的卡片（pres / velocity / heart_count）

**实施位置**：
- `flutter/lib/ui/dashboard/view/dashboard_page.dart`：在 `metricTiles` / `temperatureAndHeartTiles` 拼装时按字段是否有效条件加入；或直接删除"压力 / 流速 / 心跳计数"三个 ValueTile
- `flutter/lib/ui/history/view/history_page.dart`（待读）：HistoryField 选择器去掉 `velocity` / `pressure`
- 配套：`mock_api_service.dart` / `mock_realtime_service.dart` 把对应 mock 字段去掉或保留只用于联调

**实施策略选择**：
- **方案 A（彻底删）**：直接删卡片，HistoryField 枚举里去掉 velocity/pressure，前端永远不显示。优点：UI 干净；缺点：后续若加传感器要恢复多处代码
- **方案 B（条件隐藏）**：保留卡片定义，运行时 `m?.pressure == null || pressure == 0` 时不加入列表。优点：随设备能力自动适配；缺点：代码稍乱

> 推荐方案 A — 本期就单设备且确定不上压力/流速，删干净更明确。下次会话开工时按 A 执行。

**之后顺序**：14 → 15 → 16 → 18 → 2 → 3 → 17（最后端到端验证）。任务 1 / 4-7 是用户硬件操作，并行进行。
