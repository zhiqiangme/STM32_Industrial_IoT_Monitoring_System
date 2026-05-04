import 'dart:async';
import 'dart:math';

import '../models/alarm.dart';
import '../models/device_status.dart';
import '../models/measurement.dart';
import 'api_service.dart';
import 'realtime_service.dart';

/// 假的 [RealtimeService]：每 3 秒合成一帧遥测，偶尔丢一条告警，
/// 还会定期切换在线 / 离线状态，方便完整跑通整套 UI。
///
/// 通常通过 [Env.useMock] 与 [MockApiService] 配套启用。
class MockRealtimeService implements RealtimeService {
  MockRealtimeService();

  final StreamController<RealtimeEvent> _controller =
      StreamController<RealtimeEvent>.broadcast();

  // 内部定时器：遥测 / 状态切换。
  Timer? _teleTimer;
  Timer? _statusTimer;
  bool _connected = false;
  int _seq = 0;
  int _heartCount = 0;
  final _rng = Random();

  /// 最近一次合成的帧，供 [MockApiService.getLatest] 复用。
  Measurement? latest;

  /// 当前是否处于"在线"状态（每 2 分钟自动翻转一次）。
  bool isOnline = true;

  @override
  Stream<RealtimeEvent> get events => _controller.stream;

  @override
  bool get isConnected => _connected;

  @override
  void setUnauthorizedHandler(UnauthorizedHandler? handler) {
    // Mock 模式下不走真实 WS，无需处理鉴权失败。
  }

  @override
  Future<void> connect({required String token}) async {
    if (_connected) return;
    _connected = true;
    // 3 秒一帧遥测。
    _teleTimer =
        Timer.periodic(const Duration(seconds: 3), (_) => _emitTelemetry());
    // 按时间序列模拟不同类型的告警。
    _scheduleAlarmSequence();
    // 每 2 分钟切换一次在线 / 离线，方便测试离线 UI。
    _statusTimer = Timer.periodic(const Duration(minutes: 2), (_) {
      isOnline = !isOnline;
      _controller.add(StatusEvent(
        DeviceStatus(online: isOnline, lastSeen: latest?.timestamp),
      ));
    });
    // 立即发一帧，免得 Dashboard 第一帧画面没数据。
    _emitTelemetry();
  }

  @override
  Future<void> disconnect() async {
    _teleTimer?.cancel();
    _statusTimer?.cancel();
    _teleTimer = null;
    _statusTimer = null;
    _connected = false;
  }

  /// 外部可调（[MockApiService.sendRelaySet] 用）：在仿真延时后
  /// 推送一条 ack 事件。
  void simulateAck(int cmdSeq, String result) {
    _controller.add(AckEvent(cmdSeq: cmdSeq, result: result));
  }

  /// 模拟设备切换继电器输出，并立刻推一帧新 telemetry。
  void simulateRelayMask(int mask) {
    final current = latest ?? synthesize();
    latest = Measurement(
      timestamp: DateTime.now(),
      seq: current.seq + 1,
      flow: current.flow,
      total: current.total,
      velocity: current.velocity,
      weight: current.weight,
      temperatures: current.temperatures,
      relayDo: mask,
      relayDi: current.relayDi,
      statusBits: current.statusBits,
      heartCount: (current.heartCount ?? 0) + 1,
      validBits: current.validBits,
    );
    _controller.add(TelemetryEvent(latest!));
  }

  /// 即便定时器还没触发，也能合成一帧 [Measurement]。
  /// 主要用于 [MockApiService.getLatest] 冷启动。
  Measurement synthesize() {
    final now = DateTime.now();
    final m = Measurement(
      timestamp: now,
      seq: ++_seq,
      flow: 12.0 + _jitter(2.0),
      total: 1234.5 + _seq * 0.2,
      velocity: 0.85 + _jitter(0.1),
      weight: 1200 + _jitter(150),
      // 当前设备只展示 4 路 PT100 槽位。
      temperatures: List.generate(4, (i) => i == 3 ? 23.0 + _jitter(1.0) : null),
      relayDo: 0,
      relayDi: 0,
      statusBits: 0x07,
      heartCount: ++_heartCount,
      validBits: 0x07,
    );
    latest = m;
    return m;
  }

  void _emitTelemetry() {
    final m = synthesize();
    _controller.add(TelemetryEvent(m));
  }

  void _scheduleAlarmSequence() {
    // 25s: 单片机重启。
    Timer(const Duration(seconds: 25), () {
      _controller.add(AlarmEvent(Alarm(
        seq: 5000 + _rng.nextInt(100),
        timestamp: DateTime.now(),
        code: 'MCU_RESTART',
        severity: AlarmSeverity.critical,
      )));
    });
    // 55s: 网关掉线。
    Timer(const Duration(seconds: 55), () {
      _controller.add(AlarmEvent(Alarm(
        seq: 6000 + _rng.nextInt(100),
        timestamp: DateTime.now(),
        code: 'GATEWAY_OFFLINE',
        severity: AlarmSeverity.critical,
      )));
    });
    // 85s: 网关恢复在线。
    Timer(const Duration(seconds: 85), () {
      _controller.add(AlarmEvent(Alarm(
        seq: 7000 + _rng.nextInt(100),
        timestamp: DateTime.now(),
        code: 'GATEWAY_ONLINE',
        severity: AlarmSeverity.info,
      )));
    });
    // 115s: 流量超限。
    Timer(const Duration(seconds: 115), () {
      _controller.add(AlarmEvent(Alarm(
        seq: 8000 + _rng.nextInt(100),
        timestamp: DateTime.now(),
        code: 'OVER_FLOW',
        value: 155.0,
        severity: AlarmSeverity.warn,
      )));
    });
  }

  /// 在 [-amp/2, +amp/2] 区间生成抖动量。
  double _jitter(double amp) => (_rng.nextDouble() - 0.5) * amp;
}
