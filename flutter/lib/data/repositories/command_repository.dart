import 'dart:async';

import '../../config/env.dart';
import '../../utils/app_logger.dart';
import '../../utils/result.dart';
import '../models/command.dart';
import '../models/measurement.dart';
import '../services/api_service.dart';
import '../services/realtime_service.dart';

/// 下行命令仓库：发送命令并跟踪到完成。
///
/// 流程：
///   UI → `sendRelaySet` → API POST → 返回处于 sent 状态的 [Command]
///                                 → 注册超时定时器并等待 AckEvent
///                                 → 解析 `Future<Result<Command>>`
class CommandRepository {
  CommandRepository({
    required ApiService api,
    required RealtimeService realtime,
  })  : _api = api,
        _realtime = realtime {
    _sub = _realtime.events.listen((evt) {
      if (evt is AckEvent) {
        _onAck(evt);
      } else if (evt is TelemetryEvent) {
        _onTelemetry(evt.measurement);
      }
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

  /// `seq → 目标继电器位图`。
  /// 某些现场链路上 ack 回传不稳定，但实时遥测里的 relay_do 会先更新；
  /// 只要位图已经到达目标值，就不再继续把 UI 卡在 timeout。
  final Map<int, int> _expectedRelayMasks = {};

  /// `seq → 等待 ack 的超时定时器`。命令提前完成时按 seq 取消，
  /// 避免大量空 Timer 留在事件循环里直到 timeout 才触发。
  final Map<int, Timer> _timeoutTimers = {};

  /// 整个位图控制是“后发覆盖前发”语义：
  /// 当用户连续点多个开关时，新的目标位图已经包含了最新意图，
  /// 旧的未确认命令不应再等到 timeout。
  void _completeSupersededRelaySetCommands() {
    if (_pending.isEmpty) {
      return;
    }

    final now = DateTime.now();
    final pendingSeqs = _pending.keys.toList(growable: false);
    for (final seq in pendingSeqs) {
      final completer = _pending.remove(seq);
      final cmd = _inFlight.remove(seq);
      _expectedRelayMasks.remove(seq);
      _timeoutTimers.remove(seq)?.cancel();
      if (completer == null || cmd == null || completer.isCompleted) {
        continue;
      }

      completer.complete(cmd.copyWith(
        ackedAt: now,
        status: CommandStatus.acked,
        result: 'superseded',
      ));
    }
  }

  /// ack 到达：根据 result 决定状态是 acked 还是 failed。
  void _onAck(AckEvent evt) {
    final completer = _pending.remove(evt.cmdSeq);
    final cmd = _inFlight.remove(evt.cmdSeq);
    _expectedRelayMasks.remove(evt.cmdSeq);
    _timeoutTimers.remove(evt.cmdSeq)?.cancel();
    if (completer == null || cmd == null) return;
    final updated = cmd.copyWith(
      ackedAt: DateTime.now(),
      status: evt.result == 'ok' ? CommandStatus.acked : CommandStatus.failed,
      result: evt.result,
    );
    completer.complete(updated);
  }

  /// 遥测确认：如果最新继电器输出位图已经等于本次命令目标值，
  /// 就把命令直接视为成功，避免 ack 缺失时误报 timeout。
  void _onTelemetry(Measurement measurement) {
    final relayDo = measurement.relayDo;
    if (relayDo == null || _expectedRelayMasks.isEmpty) {
      return;
    }

    // 把所有目标位图与遥测一致的命令一次性确认掉，
    // 避免用户连点同一开关时只完成最早一条、剩余卡到 timeout。
    final matchedSeqs = _expectedRelayMasks.entries
        .where((entry) => entry.value == relayDo)
        .map((entry) => entry.key)
        .toList(growable: false);
    if (matchedSeqs.isEmpty) {
      return;
    }

    final now = DateTime.now();
    for (final seq in matchedSeqs) {
      final completer = _pending.remove(seq);
      final cmd = _inFlight.remove(seq);
      _expectedRelayMasks.remove(seq);
      _timeoutTimers.remove(seq)?.cancel();
      if (completer == null || cmd == null || completer.isCompleted) {
        continue;
      }
      completer.complete(cmd.copyWith(
        ackedAt: now,
        status: CommandStatus.acked,
        result: cmd.result ?? 'ok',
      ));
    }
  }

  /// 发起一次继电器位图控制命令并等待 ack。
  Future<Result<Command>> sendRelaySet(int mask) async {
    try {
      final cmd = await _api.sendRelaySet(mask: mask);
      // 继电器命令发送的是完整位图，新命令一旦发出，旧命令就按“被覆盖”处理。
      _completeSupersededRelaySetCommands();
      final completer = Completer<Command>();
      _pending[cmd.seq] = completer;
      _inFlight[cmd.seq] = cmd;
      _expectedRelayMasks[cmd.seq] = mask;

      // 超时兜底：ack 一直没来就把命令置为 failed("timeout")。
      // Timer 句柄存到 _timeoutTimers，命令提前完成时及时取消。
      _timeoutTimers[cmd.seq] = Timer(Env.commandAckTimeout, () {
        _timeoutTimers.remove(cmd.seq);
        if (!completer.isCompleted) {
          _pending.remove(cmd.seq);
          _inFlight.remove(cmd.seq);
          _expectedRelayMasks.remove(cmd.seq);
          completer.complete(cmd.copyWith(
            status: CommandStatus.failed,
            result: 'timeout',
          ));
        }
      });

      final finished = await completer.future;
      return Ok(finished);
    } catch (e, st) {
      appLog.w('sendRelaySet failed: $e');
      return Err(e, st);
    }
  }

  Future<void> dispose() async {
    for (final t in _timeoutTimers.values) {
      t.cancel();
    }
    _timeoutTimers.clear();
    await _sub?.cancel();
  }
}
