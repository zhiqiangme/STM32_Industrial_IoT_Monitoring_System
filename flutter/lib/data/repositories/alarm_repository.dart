import 'dart:async';

import '../../utils/app_logger.dart';
import '../../utils/result.dart';
import '../models/alarm.dart';
import '../services/api_service.dart';
import '../services/realtime_service.dart';
import 'measurement_repository.dart';

/// 告警仓库：聚合实时告警 + 历史告警 + 未读计数。
///
/// 未读徽标当前由内存状态驱动（[markAllRead] 后重置为 0）。
/// 后续可考虑把"上次已读 seq"持久化，让 App 重启后仍记得。
class AlarmRepository {
  AlarmRepository({
    required ApiService api,
    required RealtimeService realtime,
    MeasurementRepository? measurementRepo,
  })  : _api = api,
        _realtime = realtime {
    // 只关心告警事件，其它事件忽略。
    _sub = _realtime.events.listen((evt) {
      if (evt is AlarmEvent) _onAlarm(evt.alarm);
    });
    // 接收 MeasurementRepository 的客户端告警（设备离线/恢复在线）。
    measurementRepo?.onStatusAlarm = _onAlarm;
  }

  final ApiService _api;
  final RealtimeService _realtime;
  StreamSubscription<RealtimeEvent>? _sub;

  // broadcast：允许多页面（如告警列表 + 顶栏徽标）同时订阅。
  final _liveCtrl = StreamController<Alarm>.broadcast();
  final _unreadCtrl = StreamController<int>.broadcast();
  int _unread = 0;

  /// 本地保存的客户端告警（如 DEVICE_OFFLINE/ONLINE）。
  /// 这些告警不会被服务端记录，需要在 fetchHistory 时合并返回。
  final List<Alarm> _localAlarms = [];

  Stream<Alarm> get liveStream => _liveCtrl.stream;
  Stream<int> get unreadStream => _unreadCtrl.stream;
  int get unreadCount => _unread;

  /// 收到一条新告警：保存到本地列表、转发到 live 流并增加未读数。
  void _onAlarm(Alarm a) {
    _localAlarms.add(a);
    _liveCtrl.add(a);
    _unread++;
    _unreadCtrl.add(_unread);
  }

  /// 拉取一段时间范围内的历史告警。
  /// 会合并服务端历史和本地保存的客户端告警（如 DEVICE_OFFLINE/ONLINE）。
  Future<Result<List<Alarm>>> fetchHistory({
    required DateTime from,
    required DateTime to,
    int limit = 200,
  }) async {
    try {
      final serverList = await _api.getAlarms(from: from, to: to, limit: limit);
      // 合并本地客户端告警，按时间倒序排列（最新的在前）。
      final localInRange = _localAlarms.where((a) =>
          a.timestamp.isAfter(from) && a.timestamp.isBefore(to));
      final merged = [...localInRange, ...serverList];
      merged.sort((a, b) => b.timestamp.compareTo(a.timestamp));
      return Ok(merged);
    } catch (e, st) {
      appLog.w('fetchHistory (alarms) failed: $e');
      // 服务端失败时，返回本地保存的客户端告警。
      final localInRange = _localAlarms.where((a) =>
          a.timestamp.isAfter(from) && a.timestamp.isBefore(to));
      if (localInRange.isNotEmpty) {
        return Ok(localInRange.toList());
      }
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
