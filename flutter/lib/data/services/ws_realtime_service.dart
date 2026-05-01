import 'dart:async';
import 'dart:convert';

import 'package:web_socket_channel/status.dart' as ws_status;
import 'package:web_socket_channel/web_socket_channel.dart';

import '../../config/env.dart';
import '../../utils/app_logger.dart';
import '../models/alarm.dart';
import '../models/device_status.dart';
import '../models/measurement.dart';
import 'realtime_service.dart';

/// 基于 WebSocket Secure 的 [RealtimeService] 真实实现。
///
/// 与 `server/services/pipe-monitor-api/src/ws-live.js` 中的服务端
/// 帧格式对齐：
/// - `hello` 帧：`{event:"hello", path, ts, latest:[…], stats:{…}}`
/// - 普通消息帧：`{event:"message", kind:"tele"|"alarm"|"ack",
///   device, topic, payload:{…}, receivedAt}`
///
/// 服务端目前 `/ws/live` 没有鉴权 —— [token] 仅为接口对齐保留，
/// 不会附加到 URL。
class WsRealtimeService implements RealtimeService {
  WsRealtimeService();

  WebSocketChannel? _channel;
  StreamSubscription<dynamic>? _sub;
  final StreamController<RealtimeEvent> _controller =
      StreamController<RealtimeEvent>.broadcast();
  bool _isConnected = false;

  /// 重连退避：失败一次翻倍，封顶 [Env.wsReconnectMaxBackoff]。
  Duration _backoff = const Duration(seconds: 1);
  Timer? _reconnectTimer;

  /// 是否需要在断线后自动重连。`disconnect()` 调用后会被设为 false。
  bool _shouldReconnect = true;

  @override
  Stream<RealtimeEvent> get events => _controller.stream;

  @override
  bool get isConnected => _isConnected;

  @override
  Future<void> connect({required String token}) async {
    _shouldReconnect = true;
    await _open();
  }

  /// 实际建立 WS 连接。失败会进入指数退避重连。
  Future<void> _open() async {
    final uri = Uri.parse(Env.wsUrl);
    try {
      _channel = WebSocketChannel.connect(uri);
      _sub = _channel!.stream.listen(
        _onMessage,
        onDone: _onClosed,
        onError: (Object e, StackTrace st) {
          appLog.w('WS error: $e');
          _onClosed();
        },
        cancelOnError: true,
      );
      _isConnected = true;
      // 一旦成功就把退避重置为初始值。
      _backoff = const Duration(seconds: 1);
      appLog.i('WS connected to $uri');
    } catch (e) {
      appLog.w('WS connect failed: $e');
      _scheduleReconnect();
    }
  }

  /// 接收一帧原始数据并尝试解析为 JSON。
  void _onMessage(dynamic data) {
    try {
      // 服务端可能发字符串，也可能发二进制帧；都按 UTF-8 处理。
      final text = data is String ? data : utf8.decode(data as List<int>);
      final frame = jsonDecode(text) as Map<String, dynamic>;
      _dispatch(frame);
    } catch (e) {
      appLog.w('WS parse failed: $e / $data');
    }
  }

  /// 根据帧形态分发到不同处理函数。
  void _dispatch(Map<String, dynamic> frame) {
    // 兼容老格式：Mock 推送的是扁平 `{t: "tele"|"alarm"|"ack", ...}`。
    if (frame['event'] == null && frame['t'] is String) {
      _dispatchFlat(frame);
      return;
    }
    switch (frame['event']) {
      case 'hello':
        _onHello(frame);
      case 'message':
        _onMessageFrame(frame);
    }
  }

  /// `hello` 帧：服务端在握手时送上最近若干帧 + 当前 stats。
  /// UI 进入页面立即就能看到一个初值。
  void _onHello(Map<String, dynamic> frame) {
    final latest = (frame['latest'] as List?) ?? const [];
    for (final item in latest.cast<Map<String, dynamic>>()) {
      final payload = item['payload'];
      if (payload is Map<String, dynamic>) {
        _controller.add(
          TelemetryEvent(
            Measurement.fromJson(payload, receivedAt: item['receivedAt']),
          ),
        );
      }
    }
    final stats = frame['stats'];
    if (stats is Map<String, dynamic>) {
      _controller.add(StatusEvent(DeviceStatus.fromStatusJson(stats)));
    }
  }

  /// 普通消息帧：根据 kind 字段拆分。
  void _onMessageFrame(Map<String, dynamic> frame) {
    final kind = frame['kind'] as String?;
    switch (kind) {
      case 'tele':
        final payload = frame['payload'];
        if (payload is! Map<String, dynamic>) return;
        _controller.add(
          TelemetryEvent(
            Measurement.fromJson(payload, receivedAt: frame['receivedAt']),
          ),
        );
      case 'alarm':
        final payload = frame['payload'];
        if (payload is! Map<String, dynamic>) return;
        // Alarm.fromJson 同时接受外层封装 + payload，这里把两者合并传入。
        _controller.add(AlarmEvent(Alarm.fromJson({...frame, 'payload': payload})));
      case 'ack':
        final payload = frame['payload'];
        final ackSource = payload is Map<String, dynamic> ? payload : frame;
        _controller.add(AckEvent(
          // 兼容扁平 ack 与 payload ack 两种形状。
          cmdSeq: _readAckSeq(ackSource),
          result: _readAckResult(ackSource),
        ));
    }
  }

  /// 老格式（扁平）的兼容分发。
  void _dispatchFlat(Map<String, dynamic> frame) {
    switch (frame['t']) {
      case 'tele':
        _controller.add(TelemetryEvent(Measurement.fromJson(frame)));
      case 'alarm':
        _controller.add(AlarmEvent(Alarm.fromJson(frame)));
      case 'ack':
        _controller.add(AckEvent(
          cmdSeq: _readAckSeq(frame),
          result: _readAckResult(frame),
        ));
    }
  }

  /// 通道关闭：清理订阅并尝试重连。
  void _onClosed() {
    _isConnected = false;
    _sub?.cancel();
    _sub = null;
    _channel = null;
    if (_shouldReconnect) _scheduleReconnect();
  }

  /// 安排一次延时重连，按退避翻倍。
  void _scheduleReconnect() {
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer(_backoff, () {
      appLog.i('WS reconnecting after ${_backoff.inSeconds}s');
      _open();
    });
    final next = _backoff * 2;
    _backoff =
        next > Env.wsReconnectMaxBackoff ? Env.wsReconnectMaxBackoff : next;
  }

  @override
  Future<void> disconnect() async {
    _shouldReconnect = false;
    _reconnectTimer?.cancel();
    await _sub?.cancel();
    await _channel?.sink.close(ws_status.normalClosure);
    _channel = null;
    _sub = null;
    _isConnected = false;
  }
}

int _readAckSeq(Map<String, dynamic> j) {
  final value = j['cmd_seq'] ?? j['cmdSeq'];
  if (value is num) return value.toInt();
  if (value is String) return int.tryParse(value) ?? 0;
  return 0;
}

String _readAckResult(Map<String, dynamic> j) {
  final result = j['result'];
  if (result is String && result.isNotEmpty) {
    return result;
  }

  final ok = j['ok'];
  if (ok is bool) {
    return ok ? 'ok' : 'failed';
  }
  if (ok is num) {
    return ok != 0 ? 'ok' : 'failed';
  }
  if (ok is String) {
    final normalized = ok.trim().toLowerCase();
    if (normalized == 'true' || normalized == '1' || normalized == 'ok') {
      return 'ok';
    }
    if (normalized == 'false' || normalized == '0') {
      return 'failed';
    }
  }

  return 'unknown';
}
