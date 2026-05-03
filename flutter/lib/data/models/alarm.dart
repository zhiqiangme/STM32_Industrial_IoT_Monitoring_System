import 'package:flutter/material.dart';

/// 告警严重等级。
enum AlarmSeverity {
  /// 提示信息。
  info,

  /// 警告，需要注意但暂未影响生产。
  warn,

  /// 严重故障，需要立即处理。
  critical;

  /// 字符串解析为枚举值。未知值一律落到 [info]，避免抛异常。
  static AlarmSeverity parse(String? s) => switch (s) {
        'critical' => AlarmSeverity.critical,
        'warn' => AlarmSeverity.warn,
        _ => AlarmSeverity.info,
      };
}

/// 告警事件。对应设备 `{"t":"alarm",...}` 帧。
class Alarm {
  /// 告警序号。
  final int seq;

  /// 告警发生时间（本地时区）。
  final DateTime timestamp;

  /// 告警代码，如 `OVER_FLOW`、`SENSOR_OFFLINE`。
  final String code;

  /// 触发告警的实际值（若有），便于 UI 展示阈值差距。
  final double? value;

  /// 严重等级。
  final AlarmSeverity severity;

  /// 是否已确认（用户在 UI 上点过"确认"）。
  final bool acknowledged;

  const Alarm({
    required this.seq,
    required this.timestamp,
    required this.code,
    this.value,
    required this.severity,
    this.acknowledged = false,
  });

  /// 不可变拷贝：常用于把 [acknowledged] 翻为 true。
  Alarm copyWith({bool? acknowledged}) => Alarm(
        seq: seq,
        timestamp: timestamp,
        code: code,
        value: value,
        severity: severity,
        acknowledged: acknowledged ?? this.acknowledged,
      );

  /// 同时兼容两种入参格式：
  /// - 服务端封装（`/api/alarms` 列表行 / WS `{event:"message", kind:"alarm"}`）：
  ///   `{device, topic, ts, seq, code, val, severity, payload, receivedAt}`；
  /// - 设备裸载荷 `{dev, t:"alarm", ts, seq, code, val, severity}`。
  ///
  /// `ts` 当前由设备按 Unix 秒上报；这里同时兼容秒 / 毫秒。
  /// 当设备仍上报开机秒或缺字段时，退回服务端 receivedAt。
  factory Alarm.fromJson(Map<String, dynamic> j) {
    // 如果是服务端封装格式，payload 里才是设备的原始字段。
    // 用 spread 合并：外层字段优先级高于 payload，便于服务端覆盖。
    final payload = j['payload'];
    final src = payload is Map<String, dynamic> ? {...payload, ...j} : j;
    return Alarm(
      seq: (src['seq'] as num?)?.toInt() ?? 0,
      timestamp: _parseAlarmTimestamp(
        src['ts'],
        receivedAt: src['receivedAt'],
      ),
      code: src['code'] as String? ?? 'UNKNOWN',
      value: (src['val'] as num?)?.toDouble(),
      severity: AlarmSeverity.parse(src['severity'] as String?),
      acknowledged: (src['acked'] as bool?) ?? false,
    );
  }

  /// 告警标题的中文映射。
  String get displayTitle => switch (code) {
    'OVER_FLOW' => '瞬时流量超上限',
    'UNDER_FLOW' => '瞬时流量低下限',
    'OVER_PRESSURE' => '压力超上限',
    'OVER_TEMP' => '温度超上限',
    'SENSOR_FAULT' => '传感器故障',
    'MCU_RESTART' => '单片机已重启',
    'GATEWAY_OFFLINE' => '网关掉线',
    'GATEWAY_ONLINE' => '网关恢复在线',
    'DEVICE_OFFLINE' => '设备离线',
    'DEVICE_ONLINE' => '设备恢复在线',
    _ => code,
  };

  /// 根据告警码返回对应图标。
  IconData get displayIcon => switch (code) {
    'OVER_FLOW' || 'UNDER_FLOW' => Icons.water_drop_outlined,
    'OVER_PRESSURE' => Icons.compress,
    'OVER_TEMP' => Icons.thermostat,
    'SENSOR_FAULT' => Icons.sensors_off,
    'MCU_RESTART' => Icons.power_settings_new,
    'GATEWAY_OFFLINE' || 'GATEWAY_ONLINE' => Icons.router,
    'DEVICE_OFFLINE' || 'DEVICE_ONLINE' => Icons.cloud_off,
    _ => Icons.warning_amber_rounded,
  };
}

/// 告警时间戳兼容层：优先使用真实 Unix 秒 / 毫秒，否则退回服务端接收时间。
DateTime _parseAlarmTimestamp(Object? raw, {Object? receivedAt}) {
  return _parseWallClock(raw) ??
      _parseWallClock(receivedAt) ??
      DateTime.now();
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
