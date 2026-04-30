# 项目改造方案 v4-final（已锁定，待下次开工）

## 锁定决策
- ✅ **路线 A**：保留 STM32 作 Modbus 聚合从机（addr=10），G780s 只轮询 STM32 一台
- ✅ **TLS 本期一起做**：阶段 5 并入主路径，不再标"二期"

## 阶段总览（最终版）
| 阶段 | 内容 | 预估 |
|---|---|---|
| 0 | G780s FOTA ≥ V2.4.01 / VPS mosquitto 鉴权文件 / 本地 docker-compose 联调 | 0.5 天 |
| 1 | G780s AT 配置（MQTT + 边缘 JSON + 16 项点表） | 1 天 |
| 2 | pipe-monitor-api：新增 `g780s_mapper.js` + 订阅入口分支 + users seed | 0.5 天 |
| 3 | Flutter：改名 `mill` / bundle id `com.varka.mill` / 应用名"磨坊系统" / temp 收 4 通道 / 隐藏未用卡片 | 0.5 天 |
| 4 | GitHub Secrets 补全 + `deploy-server.yml` 渲染 `.env` 与 `passwordfile` | 0.5 天 |
| 5 | mosquitto 8883 + Let's Encrypt + `AT+SSLCFG/SSLAUTH/SSLCRT` + 关闭 1883 公网 | 0.5 天 |

**合计 ≈ 3.5 天**

## Todo 清单（已写入，下次直接拉起）
0 准备 / 1 G780s 配置 / 2 API 映射 + seed / 3 Flutter 适配 / 4 CI / 5 TLS — 共 18 项，含端到端验证。

## 暂停状态
方案已冻结，不动代码。下次会话直接说"开工阶段 0"即可继续。