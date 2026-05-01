# 服务端修复：历史数据查询空白

## 问题

Flutter 历史曲线页始终显示"此时间段内无数据"。

## 根因

STM32 固件发送的 `ts` 字段是设备运行秒数（`HAL_GetTick() / 1000`），不是 Unix 时间戳。服务端将此值原样存入 `payload_ts` 列。

Flutter 查询历史时发送 Unix 秒作为 `from`/`to`，服务端 SQL 用 `WHERE payload_ts >= ? AND payload_ts <= ?` 过滤，但 `payload_ts`（如 7200）远小于 Unix 时间戳（如 1746086400），导致永远匹配不到任何行。

## 修复

修改文件：`D:\Project\HTML\server\services\stm32-mill-api\src\db.js`

找到 `buildQuery` 函数（第 297-321 行），将时间过滤从 `payload_ts` 改为 `received_at`：

```javascript
// ===== 改前 =====
if (query.from !== null) {
  clauses.push("payload_ts >= ?");
  params.push(query.from);
}

if (query.to !== null) {
  clauses.push("payload_ts <= ?");
  params.push(query.to);
}

// ===== 改后 =====
if (query.from !== null) {
  clauses.push("received_at >= FROM_UNIXTIME(?)");
  params.push(query.from);
}

if (query.to !== null) {
  clauses.push("received_at <= FROM_UNIXTIME(?)");
  params.push(query.to);
}
```

## 原理

- `received_at` 是服务端收到 MQTT 消息时的 UTC 时间（`DATETIME(3)`），值可靠
- `FROM_UNIXTIME(?)` 将客户端发来的 Unix 秒转为 MySQL datetime，连接 timezone=Z 下为 UTC
- 两者时区一致，比较正确

## 不需要改动的部分

- **Flutter 客户端**：发送的 Unix 秒格式正确，`HistoryPoint.fromJson` 已通过 `receivedAt` fallback 正确处理设备 uptime 时间戳
- **数据库表结构**：`received_at` 列已存在且有索引
- **其他 API**：`/api/latest`、WebSocket 实时推送均不涉及此查询

## 部署步骤

1. 修改 `db.js` 中 `buildQuery` 函数（改 2 行）
2. 重启 `stm32-mill-api` 容器
3. 验证：Flutter 登录后进入历史页，确认折线图正常显示
