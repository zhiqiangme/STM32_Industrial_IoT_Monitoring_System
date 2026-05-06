import 'package:flutter/foundation.dart';

import '../../config/env.dart';
import '../../utils/app_logger.dart';
import '../../utils/result.dart';
import 'measurement_repository.dart';
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
    required MeasurementRepository measurements,
    required SecureStorageService storage,
  })  : _api = api,
        _realtime = realtime,
        _measurements = measurements,
        _storage = storage {
    _api.setUnauthorizedHandler(_handleUnauthorized);
    _realtime.setUnauthorizedHandler(_handleUnauthorized);
  }

  final ApiService _api;
  final RealtimeService _realtime;
  final MeasurementRepository _measurements;
  final SecureStorageService _storage;

  bool _isLoggedIn = false;
  String? _username;
  bool _handlingUnauthorized = false;

  /// 当前是否处于已登录态。
  bool get isLoggedIn => _isLoggedIn;

  /// 已登录用户的用户名；未登录时为 null。
  String? get currentUsername => _username;

  /// App 启动时调用：若安全存储里有 token，就直接进入登录态并打开实时通道。
  /// 不主动调用接口验证 token；首次拉取数据若 401，会触发统一会话清理。
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
      _username = null; // 暂未持久化用户名，留空；下次登录会填上。
      _isLoggedIn = true;
      _api.setAuthToken(token);
      await _connectRealtimeBestEffort(
        token: token,
        logContext: 'restore session realtime connect failed',
      );
      await _measurements.startSessionSync();
      notifyListeners();
      return true;
    } catch (e) {
      appLog.w('restore session failed: $e');
      // 恢复失败：清掉脏 token，避免下一次启动继续踩同一个错误。
      await _realtime.disconnect();
      await _measurements.stopSessionSync();
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
      // 先确保 token 能持久化，再切到已登录态，避免写存储失败时留下半登录状态。
      await _storage.writeToken(token);
      _api.setAuthToken(token);
      _username = username;
      _isLoggedIn = true;
      await _connectRealtimeBestEffort(
        token: token,
        logContext: 'login realtime connect failed',
      );
      await _measurements.startSessionSync();
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
    await _measurements.stopSessionSync();
    await _storage.clearToken();
    _api.setAuthToken(null);
    _isLoggedIn = false;
    _username = null;
    notifyListeners();
  }

  /// 任意数据接口返回 401 时统一清理会话，避免 UI 停留在“假登录态”。
  Future<void> _handleUnauthorized() async {
    if (_handlingUnauthorized || !_isLoggedIn) return;
    _handlingUnauthorized = true;
    try {
      appLog.w('session expired, logout automatically');
      await _realtime.disconnect();
      await _measurements.stopSessionSync();
      await _storage.clearToken();
      _api.setAuthToken(null);
      _isLoggedIn = false;
      _username = null;
      notifyListeners();
    } finally {
      _handlingUnauthorized = false;
    }
  }

  /// WS 仅作为实时能力补充；连接失败时保留当前登录态，等待内部自动重连。
  Future<void> _connectRealtimeBestEffort({
    required String token,
    required String logContext,
  }) async {
    try {
      await _realtime.connect(token: token);
    } catch (e) {
      appLog.w('$logContext: $e');
    }
  }

  @override
  void dispose() {
    _api.setUnauthorizedHandler(null);
    _realtime.setUnauthorizedHandler(null);
    super.dispose();
  }
}
