import 'dart:async';

import 'package:dio/dio.dart';
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
/// 持有：当前选中的字段、X 轴分度值、已加载的点、loading / error。
/// View 只读取这些状态并调用 [setField] / [setIntervalMinutes] / [load]。
///
/// 时间范围固定为最近 7 天，X 轴分度值决定一个大格代表多少分钟。
/// 用户通过拖动查看历史数据。
///
/// 同时监听 [AuthRepository]：登录态切换时自动刷新（登入 → 重新拉一次；
/// 登出 → 清空已加载的曲线），保证两态切换时显示与权限一致。
class HistoryViewModel extends ChangeNotifier {
  static const Duration _autoReloadDebounce = Duration(seconds: 4);
  static const Duration _historyAssumedSamplePeriod = Duration(seconds: 30);
  static const int _historyMinLimit = 600;
  static const int _historyMaxLimit = 20160;

  HistoryViewModel({
    required MeasurementRepository repository,
    required AuthRepository auth,
  })  : _repo = repository,
        _auth = auth {
    // 默认显示最近 7 天，进入页面立刻发起一次拉取。
    _to = DateTime.now();
    _from = _to.subtract(const Duration(days: 7));
    // 默认分度值 10 分钟：X 轴一个大格代表 10 分钟。
    _intervalMinutes = 10;
    _wasLoggedIn = _auth.isLoggedIn;
    _lastStatusOnline = _repo.status.online;
    _lastStatusSeen = _repo.status.lastSeen;
    _auth.addListener(_onAuthChanged);
    _liveSub = _repo.liveStream.listen((_) => _scheduleAutoReload());
    _statusSub = _repo.statusStream.listen(_onStatusChanged);
    load();
  }

  static const List<HistoryPoint> _emptyPoints = <HistoryPoint>[];

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
  bool _disposed = false;

  HistoryView _view = HistoryView.temperature;
  late DateTime _from;
  late DateTime _to;
  late int _intervalMinutes;
  Map<HistoryField, List<HistoryPoint>> _pointsByField = const {};
  bool _loading = false;
  Object? _error;

  HistoryView get view => _view;

  /// 当前视图涉及的字段列表（单字段视图为长度 1，温度组为 4）。
  List<HistoryField> get fields => _view.fields;
  DateTime get from => _from;
  DateTime get to => _to;

  /// X 轴一个大格代表的分钟数。
  int get intervalMinutes => _intervalMinutes;

  /// 取某个字段当前已加载的曲线点；找不到时返回空列表。
  List<HistoryPoint> pointsOf(HistoryField field) =>
      _pointsByField[field] ?? _emptyPoints;

  bool get loading => _loading;
  Object? get error => _error;

  /// 是否处于"加载完毕但无数据"的空态：所有字段都拿不到点。
  bool get isEmpty {
    if (_loading || _error != null) return false;
    for (final f in _view.fields) {
      if ((_pointsByField[f] ?? _emptyPoints).isNotEmpty) return false;
    }
    return true;
  }

  /// 切换 UI 视图（流量 / 重量 / 温度组 等）。
  void setView(HistoryView v) {
    if (_view == v) return;
    _view = v;
    _pointsByField = const {};
    load();
  }

  /// 切换 X 轴分度值（一个大格代表多少分钟）。
  void setIntervalMinutes(int minutes) {
    if (_intervalMinutes == minutes) return;
    _intervalMinutes = minutes;
    notifyListeners();
  }

  /// 切换时间区间。
  void setRange(DateTime from, DateTime to) {
    _from = from;
    _to = to;
    load();
  }

  /// 重新拉取曲线数据：当前视图的所有字段并发拉取。
  Future<void> load() async {
    if (_disposed) return;
    final loadRevision = ++_loadRevision;
    final view = _view;
    final from = _from;
    final to = _to;
    final limit = _historyLimitForRange(from, to);
    _loading = true;
    _error = null;
    _safeNotifyListeners();

    final results = await Future.wait([
      for (final f in view.fields)
        _repo.fetchHistory(field: f, from: from, to: to, limit: limit),
    ]);
    if (_disposed) return;
    if (loadRevision != _loadRevision) {
      appLog.d(
        'history drop stale response view=${view.name} '
        'from=${from.toIso8601String()} to=${to.toIso8601String()}',
      );
      return;
    }

    final next = <HistoryField, List<HistoryPoint>>{};
    Object? firstError;
    var anySuccess = false;
    for (var i = 0; i < view.fields.length; i++) {
      final f = view.fields[i];
      final res = results[i];
      switch (res) {
        case Ok(:final value):
          next[f] = value;
          anySuccess = true;
        case Err(:final error):
          next[f] = const [];
          firstError ??= error;
      }
    }
    _pointsByField = next;
    if (anySuccess) {
      _lastLoadFailed = false;
      _error = null;
      appLog.i(
        'history loaded view=${view.name} from=${from.toIso8601String()} '
        'to=${to.toIso8601String()} limit=$limit '
        'counts={${[
          for (final f in view.fields) '${f.id}=${next[f]!.length}',
        ].join(',')}}',
      );
    } else {
      // 全部字段都失败：未登录的 401 静默吞掉，其余暴露给页面。
      _lastLoadFailed = true;
      _error =
          firstError != null && _shouldHideHistoryError(firstError) ? null : firstError;
      appLog.w(
        'history load failed view=${view.name} '
        'from=${from.toIso8601String()} to=${to.toIso8601String()} '
        'limit=$limit',
      );
    }
    _loading = false;
    _safeNotifyListeners();
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
      _pointsByField = const {};
      _error = null;
      _loading = false;
      _lastLoadFailed = false;
      _safeNotifyListeners();
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
      if (_disposed) return;
      if (_auth.isLoggedIn && !_loading && (isEmpty || _lastLoadFailed)) {
        load();
      }
    });
  }

  void _safeNotifyListeners() {
    if (!_disposed) {
      notifyListeners();
    }
  }

  int _historyLimitForRange(DateTime from, DateTime to) {
    final span = to.difference(from).abs();
    final estimated = (span.inSeconds / _historyAssumedSamplePeriod.inSeconds)
        .ceil();
    // 按 30 秒/点粗估历史密度，长时间范围主动放大 limit，避免 7 天查询只拿到最近 200 条。
    return estimated.clamp(_historyMinLimit, _historyMaxLimit);
  }

  bool _shouldHideHistoryError(Object error) {
    // 仅对未登录/会话过期保留“空态”体验，其他网络或服务端异常直接暴露给页面。
    return error is DioException && error.response?.statusCode == 401;
  }

  @override
  void dispose() {
    _disposed = true;
    _autoReloadTimer?.cancel();
    _liveSub?.cancel();
    _statusSub?.cancel();
    _auth.removeListener(_onAuthChanged);
    super.dispose();
  }
}
