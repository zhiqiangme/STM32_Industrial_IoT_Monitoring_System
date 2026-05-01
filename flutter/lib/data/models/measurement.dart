/// 设备上报的一条遥测样本。
///
/// 与 STM32 端 `uplink_encoder` 生成的 JSON 帧字段一一对应：
/// ```json
/// {"t":"tele","ts":...,"seq":123,"flow":12.3,"total":1234.5,
///  "v":0.85,"pres":0.52,"temp":[...7...],"heart_count":10,"valid":63}
/// ```
class Measurement {
  /// 帧时间戳（已转本地时区）。
  final DateTime timestamp;

  /// 设备侧自增序号，用于乱序检测和去重。
  final int seq;

  /// 瞬时流量，单位 L/min。
  final double? flow;

  /// 累计流量，单位 L（开机以来或上次清零后的累积值）。
  final double? total;

  /// 流速，单位 m/s。
  final double? velocity;

  /// 压力。
  final double? pressure;

  /// 4 路 PT100 温度（T0-T3，对应 STM32 寄存器 0x0001-0x0004）。
  final List<double?> temperatures;

  /// 单片机每完成一轮采集轮询递增一次，用于判断程序是否仍在运行。
  final int? heartCount;

  /// 字段有效位掩码：bit0=flow, bit1=total, ... 与固件保持一致。
  final int validBits;

  const Measurement({
    required this.timestamp,
    required this.seq,
    this.flow,
    this.total,
    this.velocity,
    this.pressure,
    required this.temperatures,
    this.heartCount,
    required this.validBits,
  });

  // 下面这组 getter 不使用 validBits 位图，而是直接以"是否为有限数值"作为
  // 判断依据。这样即使固件位图标错也不会误显示 NaN。
  bool get flowValid => flow != null && flow!.isFinite;
  bool get totalValid => total != null && total!.isFinite;
  bool get velocityValid => velocity != null && velocity!.isFinite;
  bool get pressureValid => pressure != null && pressure!.isFinite;

  /// 判断指定通道的温度是否可用：要求设备上报了一个有限数值。
  /// 这里**不**信任 `valid` 位图——当前 STM32 固件即使接了 T3–T6
  /// 传感器也不会在位图里置位，光看位图会漏显有效数据。
  bool temperatureValid(int i) {
    if (i < 0 || i >= temperatures.length) return false;
    final v = temperatures[i];
    return v != null && v.isFinite;
  }

  /// 解析设备级载荷 `{dev, t, ts, seq, flow, total, v, pres, temp[], valid}`。
  /// `ts` 当前由 STM32 用 Unix 秒上报；这里同时兼容秒 / 毫秒，避免前后端切换时 UI 跑偏。
  factory Measurement.fromJson(Map<String, dynamic> j) {
    final tempList = (j['temp'] as List?)?.cast<num?>() ?? const [];
    return Measurement(
      timestamp: _parseDeviceTimestamp(j['ts']),
      seq: (j['seq'] as num?)?.toInt() ?? 0,
      flow: (j['flow'] as num?)?.toDouble(),
      total: (j['total'] as num?)?.toDouble(),
      velocity: (j['v'] as num?)?.toDouble(),
      pressure: (j['pres'] as num?)?.toDouble(),
      // 4 路 PT100 通道，缺位补 null。
      temperatures: List.generate(
        4,
        (i) => i < tempList.length ? tempList[i]?.toDouble() : null,
      ),
      heartCount: (j['heart_count'] as num?)?.toInt(),
      validBits: (j['valid'] as num?)?.toInt() ?? 0,
    );
  }

  /// 反向序列化为 JSON。主要供 Mock 服务和单测使用。
  Map<String, dynamic> toJson() => {
        't': 'tele',
        'ts': timestamp.toUtc().millisecondsSinceEpoch,
        'seq': seq,
        if (flow != null) 'flow': flow,
        if (total != null) 'total': total,
        if (velocity != null) 'v': velocity,
        if (pressure != null) 'pres': pressure,
        'temp': temperatures,
        if (heartCount != null) 'heart_count': heartCount,
        'valid': validBits,
      };
}

/// 设备时间戳兼容层：小于 1e12 视为 Unix 秒，否则按毫秒处理。
DateTime _parseDeviceTimestamp(Object? raw) {
  final value = (raw as num?)?.toInt() ?? 0;
  final millis = value.abs() < 1000000000000 ? value * 1000 : value;
  return DateTime.fromMillisecondsSinceEpoch(millis, isUtc: true).toLocal();
}
