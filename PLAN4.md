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

## 任务清单（18 项，已写入 TodoWrite）

1. G780s FOTA 升级到固件 V2.4.01.000000.0000+（用户硬件操作）
2. VPS 上配置 mosquitto passwordfile + aclfile（FM001 + pipe-monitor-api 两组凭据）
3. 本地 docker-compose 跑通，用 mosquitto_pub 模拟 G780s JSON 上行，验证字段映射
4. G780s AT 配置：MQTT 客户端（broker/clientid/user/pass/LWT/pub/sub topic）
5. G780s AT 配置：边缘采集启用 + JSON 组包模式 + 周期/间隔/超时
6. G780s 录入 16 项点表（seq/temp[0-3]/weight[0-3]/flow/total/valid），与 STM32 寄存器布局对齐
7. 上线抓包验证：mosquitto -v 看到完整 JSON，字节序/倍率正确
8. 新增 pipe-monitor-api/src/g780s_mapper.js：r_data → Measurement schema
9. 改 pipe-monitor-api MQTT 订阅入口：JSON.parse 后按格式分支（params.dir=up 走 mapper）
10. pipe-monitor-api 启动时若 users 表为空，按 env seed admin / user 两个账号
11. Flutter 项目改名：pubspec.yaml name=mill / 应用名=磨坊系统 / bundle id=com.varka.mill（Android + iOS）
12. Flutter Measurement 模型 temp 收为 4 通道，Dashboard / History UI 同步收 4 条
13. Flutter UI：pres / v / heart_count 字段值为 0/null 时隐藏卡片
14. Flutter 复核：登录页若有注册入口则隐藏
15. GitHub Secrets 新增：JWT_SECRET / SEED_* / MYSQL_* / MQTT_* / MOSQUITTO_DEVICE_PWD
16. 改 deploy-server.yml：rsync 前 ssh 渲染 /opt/pipe-monitor/.env + 在 VPS 渲染 mosquitto passwordfile
17. push main 触发部署，验证 /health 200 + Flutter 登录 + Dashboard 出数
18. （本期）mosquitto 加 8883 + LE 证书 + AT+SSLCFG 切 TLS + 关闭 1883 公网

---

## 已完成

- ✅ 任务 8（部分）：`g780s_mapper.js` 已写入 `D:/Project/HTML/server/services/pipe-monitor-api/src/g780s_mapper.js`
- ✅ 任务 9：`state.js` ingest 已接入 mapper（`isG780sEdgePayload` 分流后转 tele）

> 下次会话直接接续任务 10（用户 seed）。
