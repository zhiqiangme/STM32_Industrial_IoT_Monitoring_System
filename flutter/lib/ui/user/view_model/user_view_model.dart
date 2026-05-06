import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../data/models/command.dart';
import '../../../data/repositories/auth_repository.dart';
import '../../../data/repositories/command_repository.dart';
import '../../../utils/result.dart';

const _uploadPeriodPreferenceKey = 'telemetry_upload_period_seconds';

/// 用户 Tab 的 ViewModel。
///
/// 复用 [AuthRepository]：
/// - 登录态由 repository 直接暴露（[isLoggedIn] / [currentUsername]）；
/// - 登录 / 退出动作转发给 repository，由它写 token、连 / 断 WS、并通知监听者。
///
/// 监听 repository 的变化把它转成本 ViewModel 的 notifyListeners，
/// 让用户页在登录态切换时立刻刷新两态 UI。
class UserViewModel extends ChangeNotifier {
  UserViewModel({
    required AuthRepository repository,
    required CommandRepository commands,
    required SharedPreferences preferences,
  })  : _repo = repository,
        _commands = commands,
        _preferences = preferences {
    _repo.addListener(_onAuthChanged);
    _uploadPeriodSeconds =
        _normalizeUploadPeriod(_preferences.getInt(_uploadPeriodPreferenceKey));
  }

  final AuthRepository _repo;
  final CommandRepository _commands;
  final SharedPreferences _preferences;

  bool _busy = false;
  bool _uploadBusy = false;
  late int _uploadPeriodSeconds;
  String? _uploadMessage;
  String? _error;
  bool _disposed = false;

  /// 是否处于已登录态——决定页面渲染登录表单还是设置面板。
  bool get isLoggedIn => _repo.isLoggedIn;

  /// 已登录用户的用户名；未登录时为 null。
  String? get currentUsername => _repo.currentUsername;

  /// 登录请求是否进行中（用于禁用按钮 + 显示菊花）。
  bool get busy => _busy;

  /// 上报周期设置是否正在等待设备 ack。
  bool get uploadBusy => _uploadBusy;

  /// 当前选择的透明链路上报周期。
  int get uploadPeriodSeconds => _uploadPeriodSeconds;

  /// 上报周期设置结果提示。
  String? get uploadMessage => _uploadMessage;

  /// 登录失败时的错误信息；登录成功或开始下一次登录时清空。
  String? get error => _error;

  /// 登录表单提交。返回 true 表示成功。
  Future<bool> submit({
    required String username,
    required String password,
  }) async {
    if (_busy) return false;
    _busy = true;
    _error = null;
    _safeNotifyListeners();

    final res = await _repo.login(username: username, password: password);
    if (_disposed) return false;
    _busy = false;
    switch (res) {
      case Ok():
        // 这里不再单独 notify：repository 已经 notify 过了，
        // _onAuthChanged 会把变化透传。
        _safeNotifyListeners();
        return true;
      case Err(:final error):
        _error = _humanize(error);
        _safeNotifyListeners();
        return false;
    }
  }

  /// 退出登录。委托给 repository，UI 通过 [_onAuthChanged] 自动刷新。
  Future<void> logout() => _repo.logout();

  Future<void> setUploadPeriod(int seconds) async {
    if (_uploadBusy || !_repo.isLoggedIn) return;
    if (!_isAllowedUploadPeriod(seconds)) {
      _uploadMessage = '不支持的上报周期';
      _safeNotifyListeners();
      return;
    }

    _uploadBusy = true;
    _uploadMessage = null;
    _safeNotifyListeners();

    final res = await _commands.sendUploadPeriod(seconds);
    if (_disposed) return;
    _uploadBusy = false;
    switch (res) {
      case Ok(:final value):
        if (value.status == CommandStatus.acked) {
          _uploadPeriodSeconds = seconds;
          await _preferences.setInt(_uploadPeriodPreferenceKey, seconds);
          _uploadMessage = '上报周期已设置为 ${_formatUploadPeriod(seconds)}';
        } else {
          _uploadMessage = '设置失败：${value.result ?? '设备未确认'}';
        }
      case Err(:final error):
        _uploadMessage = '设置失败：${_humanize(error)}';
    }
    _safeNotifyListeners();
  }

  void clearUploadMessage() {
    if (_uploadMessage == null) return;
    _uploadMessage = null;
    _safeNotifyListeners();
  }

  void _onAuthChanged() {
    // repository 状态变化（登录 / 退出 / token 失效）→ 转发通知给 UI。
    if (!_repo.isLoggedIn) {
      _uploadBusy = false;
      _uploadMessage = null;
    }
    _safeNotifyListeners();
  }

  /// 把异常转成用户能看懂的中文提示。
  String _humanize(Object error) {
    final text = error.toString();
    // Exception 默认 toString 形如 "Exception: 用户名或密码错误"，剥掉前缀更干净。
    if (text.startsWith('Exception: ')) {
      return text.substring('Exception: '.length);
    }
    return text;
  }

  void _safeNotifyListeners() {
    if (!_disposed) {
      notifyListeners();
    }
  }

  static bool _isAllowedUploadPeriod(int seconds) =>
      seconds == 2 || seconds == 10 || seconds == 30 || seconds == 60;

  static int _normalizeUploadPeriod(int? seconds) =>
      seconds != null && _isAllowedUploadPeriod(seconds) ? seconds : 10;

  static String _formatUploadPeriod(int seconds) =>
      seconds == 60 ? '1 分钟' : '$seconds 秒';

  @override
  void dispose() {
    _disposed = true;
    _repo.removeListener(_onAuthChanged);
    super.dispose();
  }
}
