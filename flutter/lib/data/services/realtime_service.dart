import '../models/alarm.dart';
import '../models/command.dart';
import '../models/device_status.dart';
import '../models/measurement.dart';
import 'api_service.dart';

/// 服务端 WebSocket 推送的事件。
///
/// 数据层把原始 JSON 帧统一收敛成一个有限可枚举的 sealed 类，
/// 仓库层用模式匹配处理事件，避免 instanceof 散落。
sealed class RealtimeEvent {
  const RealtimeEvent();
}

/// 遥测帧事件。
final class TelemetryEvent extends RealtimeEvent {
  final Measurement measurement;
  const TelemetryEvent(this.measurement);
}

/// 告警事件。
final class AlarmEvent extends RealtimeEvent {
  final Alarm alarm;
  const AlarmEvent(this.alarm);
}

/// 命令 ack 事件：携带命令序号、结果文本，可选附带最新 [Command] 快照。
final class AckEvent extends RealtimeEvent {
  final int cmdSeq;
  final String result;
  final Command? updated;
  const AckEvent({required this.cmdSeq, required this.result, this.updated});
}

/// 链路 / 服务状态变化事件。
final class StatusEvent extends RealtimeEvent {
  final DeviceStatus status;
  const StatusEvent(this.status);
}

/// 实时通道接口。每个 App 会话只创建一个实例，
/// 仓库层订阅 [events] 后各自过滤需要的事件类型。
abstract interface class RealtimeService {
  /// broadcast 流 —— 多个仓库可以并发订阅。
  Stream<RealtimeEvent> get events;

  /// 建立连接。可重复调用（幂等）。
  Future<void> connect({required String token});

  /// 关闭连接并释放资源。
  Future<void> disconnect();

  /// 底层传输报告通道处于 open 状态时为 `true`。
  bool get isConnected;

  /// 注册握手阶段鉴权失败（401）的回调。传 null 取消注册。
  void setUnauthorizedHandler(UnauthorizedHandler? handler);
}
