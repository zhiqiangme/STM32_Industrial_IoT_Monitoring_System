/// 历史曲线上的单个采样点。
///
/// 由 `GET /api/history` 返回 —— 每行的顶层字段
/// 形如 `{ts, seq, flow, total, weight, temp[], valid, receivedAt, ...}`。
/// `ts` 优先使用服务端归一化后的时间戳；缺失时退回 `receivedAt`。
class HistoryPoint {
  /// 采样时间（本地时区）。
  final DateTime timestamp;

  /// 采样值。无效点存为 [double.nan]，方便绘图库自动断线。
  final double value;

  const HistoryPoint({required this.timestamp, required this.value});

  /// 从历史行中按 [field] 取出对应字段。
  ///
  /// `t1..t4` 通过 [HistoryField.tempIndex] 索引到 `temp[]` 数组；
  /// 其余字段直接读取顶层 key。
  factory HistoryPoint.fromJson(Map<String, dynamic> j, HistoryField field) {
    final ts = _parseHistoryTimestamp(j['ts'], receivedAt: j['receivedAt']);
    final num? raw;
    switch (field) {
      // 顶层标量字段（含流量 / 累计 / 重量的 CH1，仍保持 'flow'/'total'/'weight' 单值键）。
      case HistoryField.flow:
      case HistoryField.total:
      case HistoryField.weight:
      case HistoryField.relayDo:
      case HistoryField.relayDi:
        raw = j[field.id] as num?;
      // CH2~CH4：顶层独立 key（'flow_2' 等）。当前后端没下发就拿到 null → NaN，
      // 渲染时该通道呈空表，等硬件 / 后端补齐后自动有数据。
      case HistoryField.flow2:
      case HistoryField.flow3:
      case HistoryField.flow4:
      case HistoryField.total2:
      case HistoryField.total3:
      case HistoryField.total4:
      case HistoryField.weight2:
      case HistoryField.weight3:
      case HistoryField.weight4:
        raw = j[field.id] as num?;
      // 温度通道：到 temp[] 数组里取。
      case HistoryField.t1:
      case HistoryField.t2:
      case HistoryField.t3:
      case HistoryField.t4:
        final temps = (j['temp'] as List?)?.cast<num?>() ?? const [];
        raw = field.tempIndex < temps.length ? temps[field.tempIndex] : null;
    }
    return HistoryPoint(
      timestamp: ts,
      // 缺测点用 NaN 占位，绘图库会自动断开折线。
      value: raw?.toDouble() ?? double.nan,
    );
  }
}

/// 当前历史曲线支持的可选字段。
///
/// [id] 与服务端行的顶层 key 对齐；若是温度通道，则使用 [tempIndex]
/// 索引到 `temp[]` 数组（普通字段为 `-1`）。
enum HistoryField {
  // 瞬时流量：CH1 沿用 'flow' 单值键以保持后端兼容；CH2~CH4 用 'flow_n'。
  flow('flow', '瞬时流量 CH1', 'L/min', -1),
  flow2('flow_2', '瞬时流量 CH2', 'L/min', -1),
  flow3('flow_3', '瞬时流量 CH3', 'L/min', -1),
  flow4('flow_4', '瞬时流量 CH4', 'L/min', -1),
  // 累计量：同上。
  total('total', '累计量 CH1', 'L', -1),
  total2('total_2', '累计量 CH2', 'L', -1),
  total3('total_3', '累计量 CH3', 'L', -1),
  total4('total_4', '累计量 CH4', 'L', -1),
  // 重量：同上。
  weight('weight', '重量 CH1', 'g', -1),
  weight2('weight_2', '重量 CH2', 'g', -1),
  weight3('weight_3', '重量 CH3', 'g', -1),
  weight4('weight_4', '重量 CH4', 'g', -1),
  // 温度 4 通道：到 temp[] 数组里按下标取。
  t1('t1', 'T1 温度', '°C', 0),
  t2('t2', 'T2 温度', '°C', 1),
  t3('t3', 'T3 温度', '°C', 2),
  t4('t4', 'T4 温度', '°C', 3),
  relayDo('relay_do', '继电器输出', '', -1),
  relayDi('relay_di', '继电器输入', '', -1);

  /// 服务端字段名 / `temp[]` 之外的顶层 key。
  final String id;

  /// 中文展示名。
  final String label;

  /// 单位。
  final String unit;

  /// 当为温度通道时的数组下标；其它字段为 `-1`。
  final int tempIndex;

  const HistoryField(this.id, this.label, this.unit, this.tempIndex);
}

/// 历史页 UI 选项：每个选项要么对应一个 [HistoryField]，要么把若干字段合成一组。
///
/// 当 [fields] 长度 > 1 时，UI 会渲染多张共用时间轴的小表（如温度 T1~T4）。
enum HistoryView {
  flow('瞬时流量', [
    HistoryField.flow,
    HistoryField.flow2,
    HistoryField.flow3,
    HistoryField.flow4,
  ]),
  total('累计量', [
    HistoryField.total,
    HistoryField.total2,
    HistoryField.total3,
    HistoryField.total4,
  ]),
  weight('重量', [
    HistoryField.weight,
    HistoryField.weight2,
    HistoryField.weight3,
    HistoryField.weight4,
  ]),
  temperature(
    '温度',
    [HistoryField.t1, HistoryField.t2, HistoryField.t3, HistoryField.t4],
  ),
  relayDo('继电器输出', [HistoryField.relayDo]),
  relayDi('继电器输入', [HistoryField.relayDi]);

  final String label;
  final List<HistoryField> fields;
  const HistoryView(this.label, this.fields);

  bool get isMulti => fields.length > 1;
}

/// 历史接口时间戳兼容层。
DateTime _parseHistoryTimestamp(Object? raw, {Object? receivedAt}) {
  final direct = _parseWallClock(raw);
  if (direct != null) {
    return direct;
  }
  final fallback = _parseWallClock(receivedAt);
  if (fallback != null) {
    return fallback;
  }
  return DateTime.now();
}

DateTime? _parseWallClock(Object? raw) {
  if (raw == null) return null;
  if (raw is String) return DateTime.tryParse(raw)?.toLocal();
  if (raw is! num) return null;

  final value = raw.toInt();
  if (value == 0) return null;

  final abs = value.abs();
  final millis = abs >= 1000000000000 ? value : value * 1000;
  if (millis < 946684800000) {
    return null;
  }
  return DateTime.fromMillisecondsSinceEpoch(millis, isUtc: true).toLocal();
}
