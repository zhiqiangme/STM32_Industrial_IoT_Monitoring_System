import 'dart:async';
import 'dart:math';

import '../models/alarm.dart';
import '../models/command.dart';
import '../models/device_status.dart';
import '../models/history_point.dart';
import '../models/measurement.dart';
import 'api_service.dart';
import 'mock_realtime_service.dart';

/// 一个会"造数据"的假 [ApiService] 实现。
///
/// 与 [MockRealtimeService] 共享状态，保证 REST 拉到的"最新值"
/// 与正在订阅的实时流完全一致。通过 [Env.useMock] 开启。
class MockApiService implements ApiService {
  MockApiService({required this.realtime});

  final MockRealtimeService realtime;
  final _rng = Random();

  @override
  Future<String> login({
    required String username,
    required String password,
  }) async {
    // 模拟网络耗时。
    await Future<void>.delayed(const Duration(milliseconds: 400));
    // Mock 模式下：只要账号密码非空就放行。
    if (username.isEmpty || password.isEmpty) {
      throw Exception('用户名或密码错误');
    }
    return 'mock-jwt-${DateTime.now().millisecondsSinceEpoch}';
  }

  @override
  void setAuthToken(String? token) {
    // Mock 模式下不做鉴权，token 仅为接口对齐保留。
  }

  @override
  Future<Measurement> getLatest() async {
    await Future<void>.delayed(const Duration(milliseconds: 150));
    // 优先返回实时流里最近一帧；冷启动还没有则当场合成一帧。
    return realtime.latest ?? realtime.synthesize();
  }

  @override
  Future<List<HistoryPoint>> getHistory({
    required HistoryField field,
    required DateTime from,
    required DateTime to,
    int limit = 200,
  }) async {
    await Future<void>.delayed(const Duration(milliseconds: 250));
    // 在指定时间区间内按等步长生成点，叠加一个轻微正弦波 + 噪声。
    // 不同字段使用不同基线和振幅，曲线看起来才有差异。
    final span = to.difference(from);
    // 区间过大时用小时步长，否则用 10 分钟步长。
    final stepMinutes = span.inMinutes > 14400 ? 60 : 10;
    final steps = (span.inMinutes ~/ stepMinutes).clamp(0, limit);
    final base = _baseFor(field);
    final amplitude = _amplitudeFor(field);
    return List.generate(steps, (i) {
      final ts = from.add(Duration(minutes: i * stepMinutes));
      final phase = (i / 36.0) * 2 * pi;
      final v = base +
          amplitude * sin(phase) +
          (_rng.nextDouble() - 0.5) * amplitude * 0.2;
      return HistoryPoint(timestamp: ts, value: v);
    }, growable: false);
  }

  @override
  Future<List<Alarm>> getAlarms({
    required DateTime from,
    required DateTime to,
    int limit = 200,
  }) async {
    await Future<void>.delayed(const Duration(milliseconds: 200));
    // 两条样例告警：一条已确认、一条未确认。
    return [
      Alarm(
        seq: 1001,
        timestamp: DateTime.now().subtract(const Duration(hours: 2)),
        code: 'OVER_FLOW',
        value: 150.0,
        severity: AlarmSeverity.warn,
        acknowledged: false,
      ),
      Alarm(
        seq: 1000,
        timestamp: DateTime.now().subtract(const Duration(days: 1, hours: 5)),
        code: 'SENSOR_OFFLINE',
        value: null,
        severity: AlarmSeverity.critical,
        acknowledged: true,
      ),
    ];
  }

  @override
  Future<DeviceStatus> getStatus() async {
    await Future<void>.delayed(const Duration(milliseconds: 100));
    return DeviceStatus(
      online: realtime.isOnline,
      lastSeen: realtime.latest?.timestamp,
      wsClients: realtime.isConnected ? 1 : 0,
    );
  }

  @override
  Future<Command> sendReboot() async {
    await Future<void>.delayed(const Duration(milliseconds: 300));
    final cmd = Command(
      seq: DateTime.now().millisecondsSinceEpoch ~/ 1000,
      cmd: 'reboot',
      sentAt: DateTime.now(),
      status: CommandStatus.sent,
    );
    // 模拟设备 3 秒后回 ack，让 UI 能完整跑一遍 reboot 流程。
    Timer(const Duration(seconds: 3), () => realtime.simulateAck(cmd.seq, 'ok'));
    return cmd;
  }

  /// 各字段的基线值（曲线均值）。
  double _baseFor(HistoryField f) => switch (f) {
        HistoryField.flow => 12.0,
        HistoryField.total => 1000.0,
        HistoryField.velocity => 0.85,
        HistoryField.pressure => 0.52,
        HistoryField.t0 ||
        HistoryField.t1 ||
        HistoryField.t2 ||
        HistoryField.t3 =>
          23.0,
      };

  /// 各字段的波动幅度。
  double _amplitudeFor(HistoryField f) => switch (f) {
        HistoryField.flow => 2.0,
        HistoryField.total => 200.0,
        HistoryField.velocity => 0.1,
        HistoryField.pressure => 0.05,
        _ => 1.0,
      };
}
