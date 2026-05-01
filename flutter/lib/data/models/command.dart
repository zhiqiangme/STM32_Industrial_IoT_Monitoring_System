/// 下行命令的生命周期：UI → 后端 → 设备 → ack 一路追踪。
enum CommandStatus {
  /// 已入后端队列，但 MQTT broker 还没确认收到。
  pending,

  /// 已发布到设备，等待 ack JSON 回执。
  sent,

  /// 设备已 ack，且 result="ok"。
  acked,

  /// ack 超时，或设备返回错误结果。
  failed,
}

/// 一次完整的命令往返记录。
///
/// 当前已接入 `relay_set`，但字段保持通用，
/// 后续 `set_param` 之类命令直接复用同一个数据结构即可。
class Command {
  /// 命令序号（由后端分配，用于匹配 ack）。
  final int seq;

  /// 命令名，例如 `relay_set`、未来的 `set_param` 等。
  final String cmd;

  /// 命令参数（自由 JSON）。
  final Map<String, dynamic>? params;

  /// 发送时间（本地时区）。
  final DateTime sentAt;

  /// ack 到达时间，未 ack 时为 null。
  final DateTime? ackedAt;

  /// 当前状态，参见 [CommandStatus]。
  final CommandStatus status;

  /// 设备返回的结果文本，如 `"ok"` 或 `"error:<msg>"`。
  final String? result;

  const Command({
    required this.seq,
    required this.cmd,
    this.params,
    required this.sentAt,
    this.ackedAt,
    required this.status,
    this.result,
  });

  /// 命令在 UI 中状态推进时的不可变拷贝。
  Command copyWith({
    DateTime? ackedAt,
    CommandStatus? status,
    String? result,
  }) =>
      Command(
        seq: seq,
        cmd: cmd,
        params: params,
        sentAt: sentAt,
        ackedAt: ackedAt ?? this.ackedAt,
        status: status ?? this.status,
        result: result ?? this.result,
      );

  /// 解析后端 `/api/commands` 行或 WS 推送。
  factory Command.fromJson(Map<String, dynamic> j) => Command(
        seq: (j['seq'] as num).toInt(),
        cmd: j['cmd'] as String,
        params: j['params'] as Map<String, dynamic>?,
        sentAt: _parseTs(j['sent_at'])!,
        ackedAt: _parseTs(j['acked_at']),
        status: CommandStatus.values
            .firstWhere((e) => e.name == (j['status'] as String)),
        result: j['result'] as String?,
      );
}

/// 后端时间戳输出为 Unix 秒（int）。早期 Mock 数据使用 ISO 字符串，
/// 这里两种格式都接受，保证升级期间兼容。
DateTime? _parseTs(Object? v) {
  if (v == null) return null;
  if (v is num) {
    // Unix 秒 → 毫秒 → DateTime。
    return DateTime.fromMillisecondsSinceEpoch(v.toInt() * 1000, isUtc: true)
        .toLocal();
  }
  if (v is String) return DateTime.parse(v).toLocal();
  return null;
}
