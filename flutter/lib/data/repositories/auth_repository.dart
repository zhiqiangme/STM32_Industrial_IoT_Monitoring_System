import 'package:flutter/foundation.dart';

import '../../config/env.dart';
import '../../utils/app_logger.dart';
import '../../utils/result.dart';
import '../services/api_service.dart';
import '../services/realtime_service.dart';
import '../services/secure_storage_service.dart';

/// 会话生命周期。
///
/// 三态：未登录（初始）→ 已登录（持有 token、连上 WS、当前用户名可用）→ 退出回到未登录。
/// 通过 [ChangeNotifier] 暴露状态变化，让 GoRouter（refreshListenable）与
/// 用户 Tab 视图都能在登录态切换时自动刷新。
class AuthRepository extends ChangeNotifier {
  AuthRepository({
    required ApiService api,
    required RealtimeService realtime,
    required SecureStorageService storage,
  })  : _api = api,
        _realtime = realtime,
        _storage = storage;

  final ApiService _api;
  final RealtimeService _realtime;
  final SecureStorageService _storage;

  bool _isLoggedIn = false;
  String? _username;

  /// 当前是否处于已登录态。
  bool get isLoggedIn => _isLoggedIn;

  /// 已登录用户的用户名；未登录时为 null。
  String? get currentUsername => _username;

  /// App 启动时调用：若安全存储里有 token，就直接进入登录态并打开实时通道。
  /// 不主动调用接口验证 token；首次拉取数据若 401，由上层调用 [logout] 清理。
  /// 返回值表示本次是否成功恢复会话。
  Future<bool> tryRestoreSession() async {
    try {
      final token = await _storage.readToken();
      if (token == null || token.isEmpty) {
        return false;
      }
      if (!Env.useMock && token.startsWith('mock-jwt-')) {
        // 从旧 Mock 版本升级到真实后端时，丢弃本地假 token，强制重新登录。
        await _storage.clearToken();
        return false;
      }
      _api.setAuthToken(token);
      // 实时通道当前不强制鉴权，token 仅为接口对齐保留。
      await _realtime.connect(token: token);
      _username = null; // 暂未持久化用户名，留空；下次登录会填上。
      _isLoggedIn = true;
      notifyListeners();
      return true;
    } catch (e) {
      appLog.w('restore session failed: $e');
      // 恢复失败：清掉脏 token，避免下一次启动继续踩同一个错误。
      await _storage.clearToken();
      _api.setAuthToken(null);
      _isLoggedIn = false;
      _username = null;
      // 这里不通知监听者，因为 App 还没渲染过登录态，状态本来就是未登录。
      return false;
    }
  }

  /// 用户名 / 密码登录。成功后写 token、连 WS、并通知监听者。
  Future<Result<void>> login({
    required String username,
    required String password,
  }) async {
    try {
      final token = await _api.login(username: username, password: password);
      // 先告诉 ApiService 后续请求要带上这个 token，再持久化与连 WS。
      _api.setAuthToken(token);
      await _storage.writeToken(token);
      await _realtime.connect(token: token);
      _username = username;
      _isLoggedIn = true;
      notifyListeners();
      return const Ok(null);
    } catch (e, st) {
      appLog.w('login failed: $e');
      // 登录失败保持未登录态；不写 token、不连 WS。
      return Err(e, st);
    }
  }

  /// 退出登录：断开 WS、清除 token、回到未登录态并通知监听者。
  Future<void> logout() async {
    await _realtime.disconnect();
    await _storage.clearToken();
    _api.setAuthToken(null);
    _isLoggedIn = false;
    _username = null;
    notifyListeners();
  }
}
