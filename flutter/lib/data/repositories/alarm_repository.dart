import 'dart:async';

import '../../utils/app_logger.dart';
import '../../utils/result.dart';
import '../models/alarm.dart';
import '../services/api_service.dart';
import '../services/realtime_service.dart';

/// 告警仓库：聚合实时告警 + 历史告警 + 未读计数。
///
/// 未读徽标当前由内存状态驱动（[markAllRead] 后重置为 0）。
/// 后续可考虑把"上次已读 seq"持久化，让 App 重启后仍记得。
class AlarmRepository {
  AlarmRepository({
    required ApiService api,
    required RealtimeService realtime,
  })  : _api = api,
        _realtime = realtime {
    // 只关心告警事件，其它事件忽略。
    _sub = _realtime.events.listen((evt) {
      if (evt is AlarmEvent) _onAlarm(evt.alarm);
    });
  }

  final ApiService _api;
  final RealtimeService _realtime;
  StreamSubscription<RealtimeEvent>? _sub;

  // broadcast：允许多页面（如告警列表 + 顶栏徽标）同时订阅。
  final _liveCtrl = StreamController<Alarm>.broadcast();
  final _unreadCtrl = StreamController<int>.broadcast();
  int _unread = 0;

  Stream<Alarm> get liveStream => _liveCtrl.stream;
  Stream<int> get unreadStream => _unreadCtrl.stream;
  int get unreadCount => _unread;

  /// 收到一条新告警：转发到 live 流并增加未读数。
  void _onAlarm(Alarm a) {
    _liveCtrl.add(a);
    _unread++;
    _unreadCtrl.add(_unread);
  }

  /// 拉取一段时间范围内的历史告警。
  Future<Result<List<Alarm>>> fetchHistory({
    required DateTime from,
    required DateTime to,
    int limit = 200,
  }) async {
    try {
      final list = await _api.getAlarms(from: from, to: to, limit: limit);
      return Ok(list);
    } catch (e, st) {
      appLog.w('fetchHistory (alarms) failed: $e');
      return Err(e, st);
    }
  }

  /// 把未读计数清零。通常在用户进入告警页面时调用。
  void markAllRead() {
    _unread = 0;
    _unreadCtrl.add(0);
  }

  Future<void> dispose() async {
    await _sub?.cancel();
    await _liveCtrl.close();
    await _unreadCtrl.close();
  }
}
