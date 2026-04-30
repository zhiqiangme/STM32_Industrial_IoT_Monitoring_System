import 'dart:async';

import 'package:flutter/foundation.dart';

import '../../../data/models/device_status.dart';
import '../../../data/models/measurement.dart';
import '../../../data/repositories/auth_repository.dart';
import '../../../data/repositories/measurement_repository.dart';
import '../../../utils/result.dart';

/// 实时仪表盘页面的 ViewModel。
///
/// 订阅 [MeasurementRepository.liveStream] 与 `statusStream`，
/// 把最近一帧测量数据和设备状态以 [ChangeNotifier] 形式暴露给 UI 绑定。
///
/// 由于"设备多数时间离线"是常态，这里把 [status] 当成一等公民状态处理，
/// 不当作错误。UI 层会用 [StatusBanner] 来呈现。
///
/// 监听 [AuthRepository]：登入后主动重拉一次最新数据；登出后清空已展示的
/// 测量值与状态，回到"暂无数据"空壳。
class DashboardViewModel extends ChangeNotifier {
  DashboardViewModel({
    required MeasurementRepository repository,
    required AuthRepository auth,
  })  : _repo = repository,
        _auth = auth {
    // 进来先用仓库里已有的最新缓存值占位，避免首帧"暂无数据"。
    _measurement = repository.lastMeasurement;
    _status = repository.status;
    // 订阅实时事件，更新自身状态后通知 UI 重建。
    _liveSub = repository.liveStream.listen(_onMeasurement);
    _statusSub = repository.statusStream.listen(_onStatus);
    _wasLoggedIn = _auth.isLoggedIn;
    _auth.addListener(_onAuthChanged);
    _loadInitial();
  }

  final MeasurementRepository _repo;
  final AuthRepository _auth;
  bool _wasLoggedIn = false;
  StreamSubscription<Measurement>? _liveSub;
  StreamSubscription<DeviceStatus>? _statusSub;

  Measurement? _measurement;
  DeviceStatus _status = DeviceStatus.unknown();
  bool _loading = false;
  Object? _error;

  Measurement? get measurement => _measurement;
  DeviceStatus get status => _status;
  bool get loading => _loading;
  Object? get error => _error;
  bool get isLoggedIn => _auth.isLoggedIn;

  /// 首次 / 下拉刷新：主动拉一次最新值。
  Future<void> _loadInitial() async {
    // 已经有缓存值就不再多打一次 REST，直接等实时流推。
    if (_measurement != null) return;
    _loading = true;
    notifyListeners();
    final res = await _repo.fetchLatest();
    switch (res) {
      case Ok(:final value):
        _measurement = value;
        _error = null;
      case Err():
        // 真实后端在设备从未上报时会 404，未登录时会 401。
        // 这里不向 UI 暴露这个错误，等 WS hello 或下一帧 tele 自然填满。
        _error = null;
    }
    _loading = false;
    notifyListeners();
  }

  /// 下拉刷新：同时刷新状态和测量值。
  Future<void> refresh() async {
    await _repo.fetchStatus();
    await _loadInitial();
  }

  /// 实时帧到达：更新状态、清除错误、通知重建。
  void _onMeasurement(Measurement m) {
    _measurement = m;
    _error = null;
    notifyListeners();
  }

  /// 状态变化（在线 / 离线 / lastSeen 更新）。
  void _onStatus(DeviceStatus s) {
    _status = s;
    notifyListeners();
  }

  /// 监听登录态切换：登入后强制重拉最新值；登出后清空展示。
  void _onAuthChanged() {
    final nowLoggedIn = _auth.isLoggedIn;
    if (nowLoggedIn == _wasLoggedIn) return;
    _wasLoggedIn = nowLoggedIn;
    if (nowLoggedIn) {
      // 登入：清掉之前可能残留的占位值，再拉一次最新。
      _measurement = null;
      notifyListeners();
      _loadInitial();
    } else {
      // 登出：清掉测量值与状态，回到空壳。
      _measurement = null;
      _status = DeviceStatus.unknown();
      _error = null;
      _loading = false;
      notifyListeners();
    }
  }

  @override
  void dispose() {
    // 取消订阅，避免页面销毁后还在更新已 dispose 的 notifier。
    _auth.removeListener(_onAuthChanged);
    _liveSub?.cancel();
    _statusSub?.cancel();
    super.dispose();
  }
}
