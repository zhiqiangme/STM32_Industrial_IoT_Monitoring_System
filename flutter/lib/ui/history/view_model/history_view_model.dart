import 'dart:async';

import 'package:flutter/foundation.dart';

import '../../../data/models/device_status.dart';
import '../../../data/models/history_point.dart';
import '../../../data/models/measurement.dart';
import '../../../data/repositories/auth_repository.dart';
import '../../../data/repositories/measurement_repository.dart';
import '../../../utils/app_logger.dart';
import '../../../utils/result.dart';

/// 历史曲线页面的 ViewModel。
///
/// 持有：当前选中的字段、时间区间、已加载的点、loading / error。
/// View 只读取这些状态并调用 [setField] / [setRange] / [load]。
///
/// 同时监听 [AuthRepository]：登录态切换时自动刷新（登入 → 重新拉一次；
/// 登出 → 清空已加载的曲线），保证两态切换时显示与权限一致。
class HistoryViewModel extends ChangeNotifier {
  static const Duration _autoReloadDebounce = Duration(seconds: 4);

  HistoryViewModel({
    required MeasurementRepository repository,
    required AuthRepository auth,
  })  : _repo = repository,
        _auth = auth {
    // 默认显示最近 7 天，进入页面立刻发起一次拉取。
    _to = DateTime.now();
    _from = _to.subtract(const Duration(days: 7));
    _wasLoggedIn = _auth.isLoggedIn;
    _lastStatusOnline = _repo.status.online;
    _lastStatusSeen = _repo.status.lastSeen;
    _auth.addListener(_onAuthChanged);
    _liveSub = _repo.liveStream.listen((_) => _scheduleAutoReload());
    _statusSub = _repo.statusStream.listen(_onStatusChanged);
    load();
  }

  final MeasurementRepository _repo;
  final AuthRepository _auth;
  bool _wasLoggedIn = false;
  bool _lastLoadFailed = false;
  bool _lastStatusOnline = false;
  int _loadRevision = 0;
  DateTime? _lastStatusSeen;
  StreamSubscription<Measurement>? _liveSub;
  StreamSubscription<DeviceStatus>? _statusSub;
  Timer? _autoReloadTimer;

  HistoryField _field = HistoryField.flow;
  late DateTime _from;
  late DateTime _to;
  List<HistoryPoint> _points = const [];
  bool _loading = false;
  Object? _error;

  HistoryField get field => _field;
  DateTime get from => _from;
  DateTime get to => _to;
  List<HistoryPoint> get points => _points;
  bool get loading => _loading;
  Object? get error => _error;

  /// 是否处于"加载完毕但无数据"的空态。
  bool get isEmpty => !_loading && _points.isEmpty && _error == null;

  /// 切换字段（流量 / 压力 / 温度通道等）。
  void setField(HistoryField f) {
    if (_field == f) return;
    _field = f;
    load();
  }

  /// 切换时间区间。
  void setRange(DateTime from, DateTime to) {
    _from = from;
    _to = to;
    load();
  }

  /// 重新拉取曲线数据。
  Future<void> load() async {
    final loadRevision = ++_loadRevision;
    final field = _field;
    final from = _from;
    final to = _to;
    _loading = true;
    _error = null;
    notifyListeners();
    final res = await _repo.fetchHistory(field: field, from: from, to: to);
    if (loadRevision != _loadRevision) {
      appLog.d(
        'history drop stale response field=${field.id} '
        'from=${from.toIso8601String()} to=${to.toIso8601String()}',
      );
      return;
    }
    switch (res) {
      case Ok(:final value):
        _points = value;
        _lastLoadFailed = false;
        appLog.i(
          'history loaded field=${field.id} from=${from.toIso8601String()} '
          'to=${to.toIso8601String()} count=${value.length}',
        );
      case Err():
        // 未登录时数据接口会返回 401；这里把错误静默吞掉，
        // 让页面停留在"此时间段内无数据"的空壳状态，
        // 等用户去"用户"Tab 登录后会自动 refresh。
        _points = const [];
        _lastLoadFailed = true;
        appLog.w(
          'history load failed field=${field.id} '
          'from=${from.toIso8601String()} to=${to.toIso8601String()}',
        );
    }
    _loading = false;
    notifyListeners();
  }

  /// 监听登录态切换。
  void _onAuthChanged() {
    final nowLoggedIn = _auth.isLoggedIn;
    if (nowLoggedIn == _wasLoggedIn) return;
    _wasLoggedIn = nowLoggedIn;
    if (nowLoggedIn) {
      // 登入：重新拉一次曲线。
      load();
    } else {
      // 登出：清空展示数据，回到"暂无数据"空壳。
      _loadRevision++;
      _autoReloadTimer?.cancel();
      _points = const [];
      _error = null;
      _loading = false;
      _lastLoadFailed = false;
      notifyListeners();
    }
  }

  /// 链路恢复或 lastSeen 前进时，如果历史页仍是空白，就延迟重新拉一次。
  void _onStatusChanged(DeviceStatus status) {
    final online = status.online;
    final lastSeen = status.lastSeen;
    final recovered = online && !_lastStatusOnline;
    final advanced = lastSeen != null &&
        (_lastStatusSeen == null || lastSeen.isAfter(_lastStatusSeen!));

    _lastStatusOnline = online;
    if (lastSeen != null) _lastStatusSeen = lastSeen;

    if (recovered || advanced) {
      _scheduleAutoReload();
    }
  }

  void _scheduleAutoReload() {
    if (!_auth.isLoggedIn || _loading || (!isEmpty && !_lastLoadFailed)) {
      return;
    }

    // 补传恢复时可能连续收到多帧，防抖后再查历史，避免打爆 REST 接口。
    _autoReloadTimer?.cancel();
    _autoReloadTimer = Timer(_autoReloadDebounce, () {
      if (_auth.isLoggedIn && !_loading && (isEmpty || _lastLoadFailed)) {
        load();
      }
    });
  }

  @override
  void dispose() {
    _autoReloadTimer?.cancel();
    _liveSub?.cancel();
    _statusSub?.cancel();
    _auth.removeListener(_onAuthChanged);
    super.dispose();
  }
}
