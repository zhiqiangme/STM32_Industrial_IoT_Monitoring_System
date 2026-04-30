import 'dart:async';

import '../../config/env.dart';
import '../../utils/app_logger.dart';
import '../../utils/result.dart';
import '../models/command.dart';
import '../services/api_service.dart';
import '../services/realtime_service.dart';

/// 下行命令仓库：发送命令并跟踪到完成。
///
/// 流程：
///   UI → [sendReboot] → API POST → 返回处于 sent 状态的 [Command]
///                                → 注册超时定时器并等待 AckEvent
///                                → 解析 `Future<Result<Command>>`
class CommandRepository {
  CommandRepository({
    required ApiService api,
    required RealtimeService realtime,
  })  : _api = api,
        _realtime = realtime {
    _sub = _realtime.events.listen((evt) {
      if (evt is AckEvent) _onAck(evt);
    });
  }

  final ApiService _api;
  final RealtimeService _realtime;
  StreamSubscription<RealtimeEvent>? _sub;

  /// `seq → 等待 ack 的 Completer`。
  /// 收到 ack 时按 `cmdSeq` 找回对应 Completer 并解析。
  final Map<int, Completer<Command>> _pending = {};

  /// `seq → 当前未完成的 [Command] 快照`，用于 ack 到达时拷贝出最终状态。
  final Map<int, Command> _inFlight = {};

  /// ack 到达：根据 result 决定状态是 acked 还是 failed。
  void _onAck(AckEvent evt) {
    final completer = _pending.remove(evt.cmdSeq);
    final cmd = _inFlight.remove(evt.cmdSeq);
    if (completer == null || cmd == null) return;
    final updated = cmd.copyWith(
      ackedAt: DateTime.now(),
      status: evt.result == 'ok' ? CommandStatus.acked : CommandStatus.failed,
      result: evt.result,
    );
    completer.complete(updated);
  }

  /// 发起一次"重启设备"命令并等待 ack。
  Future<Result<Command>> sendReboot() async {
    try {
      final cmd = await _api.sendReboot();
      final completer = Completer<Command>();
      _pending[cmd.seq] = completer;
      _inFlight[cmd.seq] = cmd;

      // 超时兜底：ack 一直没来就把命令置为 failed("timeout")。
      Timer(Env.commandAckTimeout, () {
        if (!completer.isCompleted) {
          _pending.remove(cmd.seq);
          _inFlight.remove(cmd.seq);
          completer.complete(cmd.copyWith(
            status: CommandStatus.failed,
            result: 'timeout',
          ));
        }
      });

      final finished = await completer.future;
      return Ok(finished);
    } catch (e, st) {
      appLog.w('sendReboot failed: $e');
      return Err(e, st);
    }
  }

  Future<void> dispose() async {
    await _sub?.cancel();
  }
}
