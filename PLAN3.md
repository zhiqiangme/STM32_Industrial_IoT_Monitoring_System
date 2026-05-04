# PLAN3.md - 项目进度总结与待办事项

> 基于 PLAN1.md 和 PLAN2.md 的完成情况整理，创建于 2026-05-04

---

## 已完成事项

### Flutter 客户端（任务 11, 12, 13, 14）

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务 11 — Flutter 项目改名 | ✅ 完成 | name=mill / 应用名=磨坊系统 / bundle id=com.varka.mill（Android-only） |
| 任务 12 — 温度通道收为 4 路 | ✅ 完成 | HistoryField 枚举只有 t1-t4，Dashboard/History UI 同步收 4 条 |
| 任务 13 — 隐藏未使用卡片 | ✅ 完成 | velocity/pressure 字段未在 UI 中定义和显示 |
| 任务 14 — 登录页注册入口 | ✅ 完成 | 已确认无注册入口，只有登录表单 |

### 架构决策（PLAN1.md）

- ✅ 从"边缘采集"路线改为"透明传输"路线已确认
- ✅ 服务端已新增 relay-set / ota-prepare 命令接口
- ✅ 服务端已补设备伪时间戳兜底
- ✅ STM32 固件已具备业务 JSON 上报、cloud_ack 等待、relay_set / ota_prepare 下行处理
- ✅ Flutter 已接入继电器控制页、Mill 仪表盘字段、历史字段改造

---

## 未完成事项

### 服务端代码（任务 8, 9, 10）— 标记为完成但实际缺失

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务 8 — g780s_mapper.js | ❌ 未完成 | 文件不存在于 `D:/Project/HTML/server/services/pipe-monitor-api/src/` |
| 任务 9 — state.js 接入 | ❌ 未完成 | state.js 中无 isG780sEdgePayload 分流逻辑 |
| 任务 10 — seed_users.js | ❌ 未完成 | 文件不存在，config.js 中无 seed 配置 |

**需要重新实现：**

1. **g780s_mapper.js**
   - `isG780sEdgePayload(payload)`：判别 `{params:{dir:"up", r_data:[...]}}` 形态
   - `mapG780sEdgePayload(payload, fallbackDeviceId)`：按 name 索引出 seq / temp[0-3] / weight[0-3] / flow / total / valid，输出兼容 Measurement schema 的 tele 对象
   - 温度倍率 0.1，err≠"0" 字段置 null，valid 缺省时按各点 err 拼简化 mask

2. **state.js 修改**
   - 顶部 import g780s_mapper
   - JSON.parse 后立即检测 G780s 形态并归一化
   - 保留对 `t:"tele"` 直发设备的兼容

3. **seed_users.js**
   - 启动时若 users 表为空 → 读 env 创建 admin / user
   - env：SEED_ADMIN_USER / SEED_ADMIN_PASS / SEED_USER_USER / SEED_USER_PASS
   - 幂等（仅空表插入）

### 硬件配置任务（任务 1-7）— 需要用户操作

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务 1 — G780s FOTA 升级 | ⬜ 待办 | 升级到固件 ≥ V2.4.01.000000.0000 |
| 任务 2 — VPS mosquitto 配置 | ⬜ 待办 | 配置 passwordfile + aclfile |
| 任务 3 — 本地 docker-compose 联调 | ⬜ 待办 | 用 mosquitto_pub 模拟 G780s JSON 上行 |
| 任务 4 — G780s MQTT 客户端配置 | ⬜ 待办 | AT 配置 MQTT broker/clientid/user/pass/LWT |
| 任务 5 — G780s 边缘采集配置 | ⬜ 待办 | AT 启用边缘采集 + JSON 组包模式 |
| 任务 6 — G780s 点表录入 | ⬜ 待办 | 16 项点表与 STM32 寄存器布局对齐 |
| 任务 7 — 上线抓包验证 | ⬜ 待办 | mosquitto -v 看 JSON，验证字节序/倍率 |

### 部署任务（任务 15-18）— 未开始

| 任务 | 状态 | 说明 |
|------|------|------|
| 任务 15 — GitHub Secrets 配置 | ⬜ 待办 | JWT_SECRET / SEED_* / MYSQL_* / MQTT_* / MOSQUITTO_DEVICE_PWD |
| 任务 16 — deploy-server.yml 修改 | ⬜ 待办 | rsync 前 ssh 渲染 .env + VPS 渲染 mosquitto passwordfile |
| 任务 17 — 端到端验证 | ⬜ 待办 | push main → /health 200 + Flutter 登录 + Dashboard 出数 |
| 任务 18 — MQTT TLS | ⬜ 待办 | mosquitto 8883 + LE 证书 + AT+SSLCFG 切 TLS |

### 待验证事项（PLAN1.md）

- 🟡 Flutter analyze 未跑完，本轮执行超时
- 🟡 STM32 固件与 pipe-monitor-api 尚未做联调回归

---

## 下一步行动建议

### 优先级 1：服务端代码补全（任务 8, 9, 10）
这是整个链路打通的前提，需要先完成 g780s_mapper.js、state.js 修改和 seed_users.js。

### 优先级 2：本地联调验证（任务 3）
完成服务端代码后，用 mosquitto_pub 模拟 G780s JSON 上行，验证字段映射是否正确。

### 优先级 3：硬件配置（任务 1, 4-7）
需要用户进行 G780s 硬件操作，包括 FOTA 升级和 AT 命令配置。

### 优先级 4：部署配置（任务 15-18）
完成代码和硬件配置后，进行 GitHub Secrets 配置和 CI/CD 部署。

---

## 工作量估算

| 类别 | 预估 |
|------|------|
| 服务端代码补全（任务 8, 9, 10） | 0.5 天 |
| 本地联调验证（任务 3） | 0.5 天 |
| 硬件配置（任务 1, 4-7） | 1 天（用户操作） |
| 部署配置（任务 15-18） | 1 天 |
| 端到端验证（任务 17） | 0.5 天 |

**剩余主路径合计 ≈ 3.5 天**

---

## 附录：原始 PLAN 文件位置

- ~~PLAN1.md~~ → 已归档至 `archive/PLAN1_透明传输OTA方案.md`
- ~~PLAN2.md~~ → 已归档至 `archive/PLAN2_项目改造方案v4.md`
- PLAN3.md：本文件（进度总结与待办事项）
