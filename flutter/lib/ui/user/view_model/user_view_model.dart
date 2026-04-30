import 'package:flutter/foundation.dart';

import '../../../data/repositories/auth_repository.dart';
import '../../../utils/result.dart';

/// 用户 Tab 的 ViewModel。
///
/// 复用 [AuthRepository]：
/// - 登录态由 repository 直接暴露（[isLoggedIn] / [currentUsername]）；
/// - 登录 / 退出动作转发给 repository，由它写 token、连 / 断 WS、并通知监听者。
///
/// 监听 repository 的变化把它转成本 ViewModel 的 notifyListeners，
/// 让用户页在登录态切换时立刻刷新两态 UI。
class UserViewModel extends ChangeNotifier {
  UserViewModel({required AuthRepository repository}) : _repo = repository {
    _repo.addListener(_onAuthChanged);
  }

  final AuthRepository _repo;

  bool _busy = false;
  String? _error;

  /// 是否处于已登录态——决定页面渲染登录表单还是设置面板。
  bool get isLoggedIn => _repo.isLoggedIn;

  /// 已登录用户的用户名；未登录时为 null。
  String? get currentUsername => _repo.currentUsername;

  /// 登录请求是否进行中（用于禁用按钮 + 显示菊花）。
  bool get busy => _busy;

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
    notifyListeners();

    final res = await _repo.login(username: username, password: password);
    _busy = false;
    switch (res) {
      case Ok():
        // 这里不再单独 notify：repository 已经 notify 过了，
        // _onAuthChanged 会把变化透传。
        notifyListeners();
        return true;
      case Err(:final error):
        _error = _humanize(error);
        notifyListeners();
        return false;
    }
  }

  /// 退出登录。委托给 repository，UI 通过 [_onAuthChanged] 自动刷新。
  Future<void> logout() => _repo.logout();

  void _onAuthChanged() {
    // repository 状态变化（登录 / 退出 / token 失效）→ 转发通知给 UI。
    notifyListeners();
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

  @override
  void dispose() {
    _repo.removeListener(_onAuthChanged);
    super.dispose();
  }
}
