/// 设备上报的一条遥测样本。
///
/// 与 STM32 端透明上报 JSON 字段一一对应：
/// ```json
/// {"t":"tele","ts":...,"seq":123,"flow":12.3,"total":1234.5,
///  "temp":[null,null,null,23.4],"weight":1200,"relay_do":3,
///  "relay_di":1,"heart_count":10,"status":7,"valid":7}
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

  /// 当前重量，单位 g。
  final double? weight;

  /// 4 路 PT100 温度槽位（T1-T4）。
  final List<double?> temperatures;

  /// 继电器输出位图。
  final int? relayDo;

  /// 继电器输入位图。
  final int? relayDi;

  /// 设备状态位，和固件侧 `REG_SYSTEM_STATUS` 保持一致。
  final int statusBits;

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
    this.weight,
    required this.temperatures,
    this.relayDo,
    this.relayDi,
    required this.statusBits,
    this.heartCount,
    required this.validBits,
  });

  // 下面这组 getter 不使用 validBits 位图，而是直接以"是否为有限数值"作为
  // 判断依据。这样即使固件位图标错也不会误显示 NaN。
  bool get flowValid => flow != null && flow!.isFinite;
  bool get totalValid => total != null && total!.isFinite;
  bool get velocityValid => velocity != null && velocity!.isFinite;
  bool get weightValid => weight != null && weight!.isFinite;
  bool get autoMode => (statusBits & 0x08) != 0;

  /// 判断指定通道的温度是否可用：要求设备上报了一个有限数值。
  /// 这里**不**信任 `valid` 位图——当前 STM32 固件即使接了 T3–T6
  /// 传感器也不会在位图里置位，光看位图会漏显有效数据。
  bool temperatureValid(int i) {
    if (i < 0 || i >= temperatures.length) return false;
    final v = temperatures[i];
    return v != null && v.isFinite;
  }

  /// 解析设备级载荷 `{dev, t, ts, seq, flow, total, temp[], weight, relay_do, relay_di, status, valid}`。
  /// 如果设备侧 `ts` 还没切到 Unix 时间，就退回服务端 `receivedAt`。
  factory Measurement.fromJson(
    Map<String, dynamic> j, {
    Object? receivedAt,
  }) {
    final tempList = (j['temp'] as List?)?.cast<num?>() ?? const [];
    return Measurement(
      timestamp: _parseDeviceTimestamp(j['ts'], receivedAt: receivedAt),
      seq: (j['seq'] as num?)?.toInt() ?? 0,
      flow: (j['flow'] as num?)?.toDouble(),
      total: (j['total'] as num?)?.toDouble(),
      velocity: (j['v'] as num?)?.toDouble(),
      weight: (j['weight'] as num?)?.toDouble(),
      // 4 路 PT100 通道，缺位补 null。
      temperatures: List.generate(
        4,
        (i) => i < tempList.length ? tempList[i]?.toDouble() : null,
      ),
      relayDo: (j['relay_do'] as num?)?.toInt(),
      relayDi: (j['relay_di'] as num?)?.toInt(),
      statusBits: (j['status'] as num?)?.toInt() ?? 0,
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
        if (weight != null) 'weight': weight,
        'temp': temperatures,
        if (relayDo != null) 'relay_do': relayDo,
        if (relayDi != null) 'relay_di': relayDi,
        'status': statusBits,
        if (heartCount != null) 'heart_count': heartCount,
        'valid': validBits,
      };
}

/// 设备时间戳兼容层：
/// 1. 优先接受看起来像真实 Unix 秒 / 毫秒的 `ts`
/// 2. 如果设备仍在上报开机秒数，则退回服务端接收时间
/// 3. 两者都没有时，最后退回本地当前时间，避免 UI 落到 1970 年
DateTime _parseDeviceTimestamp(Object? raw, {Object? receivedAt}) {
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
