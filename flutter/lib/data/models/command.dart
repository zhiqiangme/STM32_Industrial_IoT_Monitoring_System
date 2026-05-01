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
  factory Command.fromJson(Map<String, dynamic> j) {
    // 兼容当前服务端与早期草案的不同返回形状：
    // 可能是扁平对象，也可能包在 data / command 里；序号也可能叫 cmd_seq。
    final src = _unwrapCommandJson(j);
    final seq = _readInt(src, const ['seq', 'cmd_seq', 'cmdSeq', 'command_seq']);
    if (seq == null) {
      throw StateError('命令响应缺少 seq/cmd_seq 字段: ${src.keys.toList()}');
    }

    return Command(
      seq: seq,
      cmd: _readString(src, const ['cmd', 'type']) ?? 'relay_set',
      params: _readMap(src, const ['params', 'payload']),
      sentAt: _parseTs(_readField(src, const ['sent_at', 'sentAt', 'created_at', 'ts'])) ??
          DateTime.now(),
      ackedAt: _parseTs(_readField(src, const ['acked_at', 'ackedAt'])),
      status: _parseStatus(_readString(src, const ['status'])) ?? CommandStatus.sent,
      result: _readString(src, const ['result', 'message']),
    );
  }
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

Map<String, dynamic> _unwrapCommandJson(Map<String, dynamic> j) {
  final data = j['data'];
  if (data is Map) {
    return Map<String, dynamic>.from(data);
  }
  final command = j['command'];
  if (command is Map) {
    return Map<String, dynamic>.from(command);
  }
  return j;
}

Object? _readField(Map<String, dynamic> j, List<String> keys) {
  for (final key in keys) {
    if (j.containsKey(key)) {
      return j[key];
    }
  }
  return null;
}

String? _readString(Map<String, dynamic> j, List<String> keys) {
  final value = _readField(j, keys);
  if (value == null) return null;
  return value.toString();
}

int? _readInt(Map<String, dynamic> j, List<String> keys) {
  final value = _readField(j, keys);
  if (value is num) return value.toInt();
  if (value is String) return int.tryParse(value);
  return null;
}

Map<String, dynamic>? _readMap(Map<String, dynamic> j, List<String> keys) {
  final value = _readField(j, keys);
  if (value is Map) {
    return Map<String, dynamic>.from(value);
  }
  return null;
}

CommandStatus? _parseStatus(String? value) {
  if (value == null || value.isEmpty) return null;
  for (final status in CommandStatus.values) {
    if (status.name == value) return status;
  }
  return null;
}
