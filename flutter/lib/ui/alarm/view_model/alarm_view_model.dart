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
    _liveSub = repository.liveStream.listen((a) {
      _items = [a, ..._items];
      _safeNotifyListeners();
    });
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

  List<Alarm> get items => _items;
  bool get loading => _loading;
  Object? get error => _error;
  int get unread => _unread;

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
        _items = value;
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
