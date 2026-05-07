import 'dart:async';
import 'dart:convert';

import 'package:shared_preferences/shared_preferences.dart';

import '../../utils/app_logger.dart';
import '../../utils/result.dart';
import '../models/alarm.dart';
import '../services/api_service.dart';
import '../services/realtime_service.dart';
import 'auth_repository.dart';
import 'measurement_repository.dart';

/// 告警仓库：聚合实时告警 + 历史告警 + 未读计数。
///
/// 未读计数会持久化到 SharedPreferences，App 重启后仍能保留角标状态。
class AlarmRepository {
  AlarmRepository({
    required ApiService api,
    required RealtimeService realtime,
    required SharedPreferences preferences,
    MeasurementRepository? measurementRepo,
    AuthRepository? auth,
  })  : _api = api,
        _realtime = realtime,
        _preferences = preferences,
        _auth = auth {
    // 从持久化存储恢复未读计数和本地告警列表。
    _unread = preferences.getInt(_unreadKey) ?? 0;
    _loadLocalAlarms();
    // 服务端推送的告警：只转发与累加未读，不写入本地缓存（服务端已持久化，
    // 否则下次 fetchHistory 合并时会重复出现）。
    _sub = _realtime.events.listen((evt) {
      if (evt is AlarmEvent) _onServerAlarm(evt.alarm);
    });
    // 客户端生成的告警（DEVICE_OFFLINE/ONLINE）：服务端不会持久化，
    // 必须本地缓存才能在历史区间内回看。
    measurementRepo?.onStatusAlarm = _onClientAlarm;
    // 退出登录或会话失效时清掉本地告警与未读计数，避免下个用户看到上个会话的痕迹。
    if (_auth != null) {
      _wasLoggedIn = _auth.isLoggedIn;
      _auth.addListener(_onAuthChanged);
    }
  }

  final ApiService _api;
  final RealtimeService _realtime;
  final SharedPreferences _preferences;
  final AuthRepository? _auth;
  bool _wasLoggedIn = false;
  StreamSubscription<RealtimeEvent>? _sub;

  /// 未读计数的持久化 key。
  static const _unreadKey = 'alarm_unread_count';

  /// 本地告警列表的持久化 key。
  static const _localAlarmsKey = 'alarm_local_list';

  /// 本地告警最大保留数量，避免数据膨胀。
  static const _maxLocalAlarms = 50;

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

  /// 时间范围闭区间过滤：等于 from / to 边界的告警也算命中。
  bool Function(Alarm) _inRange(DateTime from, DateTime to) {
    return (a) => !a.timestamp.isBefore(from) && !a.timestamp.isAfter(to);
  }

  /// 服务端实时告警：只转发并累加未读，不入本地缓存。
  void _onServerAlarm(Alarm a) {
    _liveCtrl.add(a);
    _unread++;
    _unreadCtrl.add(_unread);
    _persistUnread();
  }

  /// 客户端生成的状态告警：写入本地缓存，并按服务端告警一样转发与计数。
  void _onClientAlarm(Alarm a) {
    _localAlarms.add(a);
    _liveCtrl.add(a);
    _unread++;
    _unreadCtrl.add(_unread);
    _persistUnread();
    _persistLocalAlarms();
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
      final localInRange = _localAlarms.where(_inRange(from, to));
      final merged = [...localInRange, ...serverList];
      merged.sort((a, b) => b.timestamp.compareTo(a.timestamp));
      return Ok(merged);
    } catch (e, st) {
      appLog.w('fetchHistory (alarms) failed: $e');
      // 服务端失败时，返回本地保存的客户端告警。
      final localInRange = _localAlarms.where(_inRange(from, to));
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
    _persistUnread();
  }

  /// 清除本地缓存的客户端告警与未读计数。退出登录 / 会话失效时调用。
  void clearLocalAlarms() {
    _localAlarms.clear();
    _unread = 0;
    _unreadCtrl.add(0);
    _persistUnread();
    _persistLocalAlarms();
  }

  /// 监听登录态切换：从已登录切到未登录时清空本地告警，避免跨账号串扰。
  void _onAuthChanged() {
    final auth = _auth;
    if (auth == null) return;
    final nowLoggedIn = auth.isLoggedIn;
    if (nowLoggedIn == _wasLoggedIn) return;
    _wasLoggedIn = nowLoggedIn;
    if (!nowLoggedIn) {
      clearLocalAlarms();
    }
  }

  /// 持久化未读计数到 SharedPreferences。
  void _persistUnread() {
    _preferences.setInt(_unreadKey, _unread);
  }

  /// 从 SharedPreferences 加载本地告警列表。
  void _loadLocalAlarms() {
    try {
      final jsonStr = _preferences.getString(_localAlarmsKey);
      if (jsonStr == null || jsonStr.isEmpty) return;
      final List<dynamic> jsonList = jsonDecode(jsonStr);
      _localAlarms.clear();
      for (final item in jsonList) {
        if (item is Map<String, dynamic>) {
          _localAlarms.add(Alarm.fromJson(item));
        }
      }
    } catch (e) {
      appLog.w('loadLocalAlarms failed: $e');
      _localAlarms.clear();
    }
  }

  /// 持久化本地告警列表到 SharedPreferences。
  /// 只保留最近 [_maxLocalAlarms] 条，避免数据膨胀。
  void _persistLocalAlarms() {
    try {
      // 只保留最近的 N 条。
      final toSave = _localAlarms.length > _maxLocalAlarms
          ? _localAlarms.sublist(_localAlarms.length - _maxLocalAlarms)
          : _localAlarms;
      final jsonStr = jsonEncode(toSave.map((a) => a.toJson()).toList());
      _preferences.setString(_localAlarmsKey, jsonStr);
    } catch (e) {
      appLog.w('persistLocalAlarms failed: $e');
    }
  }

  Future<void> dispose() async {
    _auth?.removeListener(_onAuthChanged);
    await _sub?.cancel();
    await _liveCtrl.close();
    await _unreadCtrl.close();
  }
}
