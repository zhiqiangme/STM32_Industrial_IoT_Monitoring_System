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
  String? _activeToken;
  bool _handlingUnauthorized = false;

  /// 当前是否处于已登录态。
  bool get isLoggedIn => _isLoggedIn;

  /// 已登录用户的用户名；未登录时为 null。
  String? get currentUsername => _username;

  /// App 启动时调用：若安全存储里有 token，就直接进入登录态并打开实时通道。
  /// 不主动调用接口验证 token；首次拉取数据若 401，会触发统一会话清理。
  /// 返回值表示本次是否成功恢复会话。
  Future<bool> tryRestoreSession() async {
    _measurements.beginSessionBootstrap();

    // 第一步：读 token。仅当存储介质本身坏了（PlatformException 等）才清掉，
    // 避免把网络/后端抖动当成"token 失效"误清。
    String? token;
    try {
      token = await _storage.readToken();
    } catch (e) {
      appLog.w('read token failed, clearing storage: $e');
      try {
        await _storage.clearToken();
      } catch (_) {
        // 清也清不掉就算了，下一次启动还会再试。
      }
      return false;
    }
    if (token == null || token.isEmpty) {
      return false;
    }
    if (!Env.useMock && token.startsWith('mock-jwt-')) {
      // 从旧 Mock 版本升级到真实后端时，丢弃本地假 token，强制重新登录。
      await _storage.clearToken();
      return false;
    }

    // 第二步：进入登录态并尽力建链。
    // 网络/后端瞬时不可用时保留 token，由 401 拦截器或下次 refresh 处理真正失效。
    _username = null; // 暂未持久化用户名，留空；下次登录会填上。
    _activeToken = token;
    _isLoggedIn = true;
    _api.setAuthToken(token);
    await _connectRealtimeBestEffort(
      token: token,
      logContext: 'restore session realtime connect failed',
    );
    try {
      await _measurements.startSessionSync();
    } catch (e) {
      // 启动期 getStatus / getLatest 抖动不应触发登出，只记日志。
      appLog.w('startSessionSync failed (keeping session): $e');
    }
    notifyListeners();
    return true;
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
      _activeToken = token;
      _username = username;
      _isLoggedIn = true;
      _measurements.beginSessionBootstrap();
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
    _activeToken = null;
    _isLoggedIn = false;
    _username = null;
    notifyListeners();
  }

  /// App 回到前台时主动重建实时链路并立即同步一次状态。
  Future<void> refreshRealtimeSession() async {
    if (!_isLoggedIn) return;
    final token = _activeToken;
    if (token == null || token.isEmpty) return;
    await _realtime.disconnect();
    await _connectRealtimeBestEffort(
      token: token,
      logContext: 'resume realtime reconnect failed',
    );
    await _measurements.refreshSessionSnapshot();
  }

  /// 任意数据接口返回 401 时统一清理会话，避免 UI 停留在“假登录态”。
  Future<void> _handleUnauthorized() async {
    if (_handlingUnauthorized || !_isLoggedIn) return;
    _handlingUnauthorized = true;
    try {
      appLog.w(
        '[token-trace] _handleUnauthorized fired\n${StackTrace.current}',
      );
      appLog.w('session expired, logout automatically');
      await _realtime.disconnect();
      await _measurements.stopSessionSync();
      await _storage.clearToken();
      _api.setAuthToken(null);
      _activeToken = null;
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
