/// 链路 / 服务健康状态。
///
/// MVP 之后语义不再是"单个设备在线"，而是**整体管道健康**——
/// 从 `/api/status` 派生（`mqttConnected`、`lastMessageAt`、`wsClients`）。
/// 设备本身通常处于离线状态，[lastSeen] 仍可告诉用户最近一帧数据有多新。
class DeviceStatus {
  /// 服务侧链路是否在线（MQTT broker 是否可达）。
  final bool online;

  /// 最近一次收到设备消息的时间。
  final DateTime? lastSeen;

  /// 当前连接到 WS 的客户端数。
  final int wsClients;

  const DeviceStatus({
    required this.online,
    this.lastSeen,
    this.wsClients = 0,
  });

  /// 未知态。启动初期或网络不可达时使用。
  factory DeviceStatus.unknown() =>
      const DeviceStatus(online: false, lastSeen: null);

  /// 解析 `/api/status` 响应或 WS `hello.stats` 内嵌块。
  factory DeviceStatus.fromStatusJson(Map<String, dynamic> j) => DeviceStatus(
        online: (j['mqttConnected'] as bool?) ?? false,
        lastSeen: _parseIsoOrMs(j['lastMessageAt']),
        wsClients: (j['wsClients'] as num?)?.toInt() ?? 0,
      );
}

/// 兼容 ISO 字符串与 epoch 毫秒两种时间戳格式。
DateTime? _parseIsoOrMs(Object? v) {
  if (v == null) return null;
  if (v is String) return DateTime.tryParse(v)?.toLocal();
  if (v is num) {
    return DateTime.fromMillisecondsSinceEpoch(v.toInt(), isUtc: true).toLocal();
  }
  return null;
}
