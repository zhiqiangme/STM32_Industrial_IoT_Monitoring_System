import 'dart:async';

import 'package:flutter/foundation.dart';

import '../../../data/models/alarm.dart';
import '../../../data/repositories/alarm_repository.dart';
import '../../../data/repositories/auth_repository.dart';
import '../../../utils/result.dart';

/// 报警页面 ViewModel：维护历史告警列表 + 实时新增 + 未读计数。
///
/// 同时监听 [AuthRepository]：登录态切换时自动刷新
/// （登入 → 重新拉历史；登出 → 清空列表），与其他数据 Tab 行为一致。
class AlarmViewModel extends ChangeNotifier {
  AlarmViewModel({
    required AlarmRepository repository,
    required AuthRepository auth,
  })  : _repo = repository,
        _auth = auth {
    // 监听未读计数变化（顶部底部导航徽标共用）。
    _unreadSub = repository.unreadStream.listen((n) {
      _unread = n;
      _safeNotifyListeners();
    });
    // 监听实时告警：把新告警塞到列表头部，最近的排最上面。
    _liveSub = repository.liveStream.listen(_onLiveAlarm);
    _unread = repository.unreadCount;
    _wasLoggedIn = _auth.isLoggedIn;
    _auth.addListener(_onAuthChanged);
    load();
  }

  final AlarmRepository _repo;
  final AuthRepository _auth;
  bool _wasLoggedIn = false;
  StreamSubscription<Alarm>? _liveSub;
  StreamSubscription<int>? _unreadSub;
  bool _disposed = false;

  List<Alarm> _items = const [];
  bool _loading = false;
  Object? _error;
  int _unread = 0;

  /// 实时到达但尚未被服务端历史覆盖的告警。
  /// 包括客户端生成的（DEVICE_OFFLINE/ONLINE）和服务端推送的实时告警。
  /// load() 完成后会将这些与服务端历史合并，避免丢失。
  final List<Alarm> _liveAlarms = [];

  /// 当前筛选的严重等级，null 表示全部。
  AlarmSeverity? _filter;

  /// 已展开详情的告警 seq 集合。
  final Set<int> _expandedSeqs = {};

  /// 根据筛选条件过滤后的告警列表。
  List<Alarm> get items {
    if (_filter == null) return _items;
    return _items.where((a) => a.severity == _filter).toList();
  }

  bool get loading => _loading;
  Object? get error => _error;
  int get unread => _unread;
  AlarmSeverity? get filter => _filter;

  /// 判断指定 seq 的告警是否已展开详情。
  bool isExpanded(int seq) => _expandedSeqs.contains(seq);

  /// 切换告警详情的展开/收起状态。
  void toggleExpanded(int seq) {
    if (!_expandedSeqs.remove(seq)) {
      _expandedSeqs.add(seq);
    }
    _safeNotifyListeners();
  }

  /// 设置严重等级筛选。
  void setFilter(AlarmSeverity? severity) {
    _filter = severity;
    _safeNotifyListeners();
  }

  /// 实时告警到达：加入列表并保留到 _liveAlarms，避免被 load() 覆盖。
  /// 同 seq 的重复帧（hello 重放、WS 重连补传）直接丢弃，避免列表叠加。
  void _onLiveAlarm(Alarm a) {
    if (_items.any((existing) => existing.seq == a.seq)) {
      return;
    }
    _items = [a, ..._items];
    _liveAlarms.add(a);
    _safeNotifyListeners();
  }

  /// 拉取最近 30 天的历史告警。
  Future<void> load() async {
    if (_disposed) return;
    _loading = true;
    _error = null;
    _safeNotifyListeners();
    final to = DateTime.now();
    final from = to.subtract(const Duration(days: 30));
    final res = await _repo.fetchHistory(from: from, to: to);
    if (_disposed) return;
    switch (res) {
      case Ok(:final value):
        // 合并服务端历史与实时告警，实时告警排在前面。
        final serverSeqs = value.map((a) => a.seq).toSet();
        final preserved =
            _liveAlarms.where((a) => !serverSeqs.contains(a.seq)).toList();
        _items = [...preserved, ...value];
      case Err():
        // 未登录时接口返回 401；保持列表为空、不展示错误，
        // 让页面在登录前一直保持"暂无报警"空壳状态。
        _items = const [];
    }
    _loading = false;
    _safeNotifyListeners();
  }

  /// 标记全部为已读，徽标归零。
  void markAllRead() => _repo.markAllRead();

  /// 监听登录态切换：登入后重新拉历史；登出后清空列表。
  void _onAuthChanged() {
    final nowLoggedIn = _auth.isLoggedIn;
    if (nowLoggedIn == _wasLoggedIn) return;
    _wasLoggedIn = nowLoggedIn;
    if (nowLoggedIn) {
      load();
    } else {
      _items = const [];
      _liveAlarms.clear();
      _expandedSeqs.clear();
      _error = null;
      _loading = false;
      _safeNotifyListeners();
    }
  }

  void _safeNotifyListeners() {
    if (!_disposed) {
      notifyListeners();
    }
  }

  @override
  void dispose() {
    _disposed = true;
    _auth.removeListener(_onAuthChanged);
    _liveSub?.cancel();
    _unreadSub?.cancel();
    super.dispose();
  }
}
