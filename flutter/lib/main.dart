import 'dart:async';

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'app.dart';
import 'config/env.dart';
import 'data/repositories/alarm_repository.dart';
import 'data/repositories/auth_repository.dart';
import 'data/repositories/command_repository.dart';
import 'data/repositories/measurement_repository.dart';
import 'data/services/api_service.dart';
import 'data/services/history_cache_service_factory.dart';
import 'data/services/http_api_service.dart';
import 'data/services/mock_api_service.dart';
import 'data/services/mock_realtime_service.dart';
import 'data/services/realtime_service.dart';
import 'data/services/secure_storage_service.dart';
import 'data/services/ws_realtime_service.dart';
import 'ui/settings/theme_mode_controller.dart';

/// 应用入口。负责装配整个依赖注入树：
///
/// ```
///   SecureStorageService（安全存储）
///        │
///   RealtimeService（WebSocket 实时通道） ──┐
///        │                                  │
///   ApiService（REST 接口）                  │   两者共同注入到各 Repository
///        │                                  │
///   ┌────┴──────────────────────────────────┴────┐
///   Auth / Measurement / Alarm / Command Repositories（业务仓库）
///        │
///   每页一个 ChangeNotifierProvider —— 详见 app.dart
/// ```
///
/// [Env.useMock]（默认 `false`）控制是否切换到 [MockApiService] +
/// [MockRealtimeService]；正常手机实机运行默认直连真实后端。
///
/// 启动流程：先尝试用本地保存的 token 恢复会话；恢复失败则保持未登录态，
/// 三个数据 Tab 全部展示空壳，用户在"用户" Tab 完成登录后再填充真实数据。
Future<void> main() async {
  // 必须在使用任何 plugin/异步 binding 之前调用，确保 Flutter 引擎初始化完成。
  WidgetsFlutterBinding.ensureInitialized();

  // 安全存储：保存 token、用户偏好等需要持久化的小数据。
  final storage = SecureStorageService();
  final preferences = await SharedPreferences.getInstance();
  final themeController = ThemeModeController(preferences: preferences)..load();

  // 实时通道与 REST 服务。Mock 与真实实现切换由编译期常量 USE_MOCK 控制。
  final RealtimeService realtime;
  final ApiService api;
  if (Env.useMock) {
    // Mock 模式：本地构造一对 Mock 服务，互相注入实现"假装在线"的演示数据。
    final mockRt = MockRealtimeService();
    realtime = mockRt;
    api = MockApiService(realtime: mockRt);
  } else {
    // 真实模式：HTTP + WebSocket 直连后端。
    realtime = WsRealtimeService();
    api = HttpApiService();
  }

  // 业务仓库层：负责把 ApiService + RealtimeService 的原始数据
  // 转换成上层 ViewModel 关心的领域模型。
  final authRepo = AuthRepository(
    api: api,
    realtime: realtime,
    storage: storage,
  );
  final historyCache = createHistoryCacheService();
  final measurementRepo = MeasurementRepository(
    api: api,
    historyCache: historyCache,
    realtime: realtime,
  );
  final alarmRepo = AlarmRepository(
    api: api,
    realtime: realtime,
    preferences: preferences,
    measurementRepo: measurementRepo,
  );
  final commandRepo = CommandRepository(api: api, realtime: realtime);

  // 启动时尝试用持久化的 token 直接恢复会话；
  // 没 token 或失败时静默回到未登录态，App 仍能正常进入空壳界面。
  await authRepo.tryRestoreSession();

  // 通过 MultiProvider 把全部依赖注入到 widget 树。
  // 用 Provider.value / Provider 区分：
  //   - .value：已经构造好的对象，由调用方负责生命周期；
  //   - 普通 create：交给 Provider 在销毁时调用 dispose。
  runApp(
    MultiProvider(
      providers: [
        Provider<SecureStorageService>.value(value: storage),
        ChangeNotifierProvider<ThemeModeController>.value(value: themeController),
        Provider<ApiService>.value(value: api),
        Provider<RealtimeService>(
          create: (_) => realtime,
          // dispose 用 unawaited 避免阻塞 widget 销毁。
          dispose: (_, service) => unawaited(service.disconnect()),
        ),
        // AuthRepository 是 ChangeNotifier，UI 与 GoRouter 都要监听它。
        ChangeNotifierProvider<AuthRepository>.value(value: authRepo),
        Provider<MeasurementRepository>(
          create: (_) => measurementRepo,
          dispose: (_, repo) => unawaited(repo.dispose()),
        ),
        Provider<AlarmRepository>(
          create: (_) => alarmRepo,
          dispose: (_, repo) => unawaited(repo.dispose()),
        ),
        Provider<CommandRepository>(
          create: (_) => commandRepo,
          dispose: (_, repo) => unawaited(repo.dispose()),
        ),
      ],
      child: const FlowmeterApp(),
    ),
  );
}
