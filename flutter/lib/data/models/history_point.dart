/// 历史曲线上的单个采样点。
///
/// 由 `GET /api/history` 返回 —— 每行的顶层字段
/// 形如 `{ts, seq, flow, total, v, pres, temp[], valid, ...}`。
/// `ts` 当前由后端原样透传设备侧 Unix 秒；这里同时兼容秒 / 毫秒。
class HistoryPoint {
  /// 采样时间（本地时区）。
  final DateTime timestamp;

  /// 采样值。无效点存为 [double.nan]，方便绘图库自动断线。
  final double value;

  const HistoryPoint({required this.timestamp, required this.value});

  /// 从历史行中按 [field] 取出对应字段。
  ///
  /// `t0..t6` 通过 [HistoryField.tempIndex] 索引到 `temp[]` 数组；
  /// 其余字段直接读取顶层 key。
  factory HistoryPoint.fromJson(Map<String, dynamic> j, HistoryField field) {
    final ts = _parseHistoryTimestamp(j['ts']);
    final num? raw;
    switch (field) {
      // 顶层数值字段。
      case HistoryField.flow:
      case HistoryField.total:
      case HistoryField.velocity:
      case HistoryField.pressure:
        raw = j[field.id] as num?;
      // 温度通道：到 temp[] 数组里取。
      case HistoryField.t0:
      case HistoryField.t1:
      case HistoryField.t2:
      case HistoryField.t3:
      case HistoryField.t4:
      case HistoryField.t5:
      case HistoryField.t6:
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
  flow('flow', '瞬时流量', 'L/min', -1),
  total('total', '累计量', 'L', -1),
  velocity('v', '流速', 'm/s', -1),
  pressure('pres', '压力', 'MPa', -1),
  t0('t0', 'T0 温度', '°C', 0),
  t1('t1', 'T1 温度', '°C', 1),
  t2('t2', 'T2 温度', '°C', 2),
  t3('t3', 'T3 温度', '°C', 3),
  t4('t4', 'T4 温度', '°C', 4),
  t5('t5', 'T5 温度', '°C', 5),
  t6('t6', 'T6 温度', '°C', 6);

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

/// 历史接口的时间戳兼容层：小于 1e12 视为 Unix 秒，否则按毫秒处理。
DateTime _parseHistoryTimestamp(Object? raw) {
  final value = (raw as num?)?.toInt() ?? 0;
  final millis = value.abs() < 1000000000000 ? value * 1000 : value;
  return DateTime.fromMillisecondsSinceEpoch(millis, isUtc: true).toLocal();
}
