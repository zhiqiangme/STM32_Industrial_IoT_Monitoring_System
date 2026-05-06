import 'dart:async';

import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';
import 'package:provider/provider.dart';

import 'config/theme.dart';
import 'data/repositories/auth_repository.dart';
import 'ui/alarm/view/alarm_page.dart';
import 'ui/alarm/view_model/alarm_view_model.dart';
import 'ui/control/view/control_page.dart';
import 'ui/control/view_model/control_view_model.dart';
import 'ui/core/shell/home_shell.dart';
import 'ui/dashboard/view/dashboard_page.dart';
import 'ui/dashboard/view_model/dashboard_view_model.dart';
import 'ui/history/view/history_page.dart';
import 'ui/history/view_model/history_view_model.dart';
import 'ui/settings/theme_mode_controller.dart';
import 'ui/settings/view/settings_page.dart';
import 'ui/user/view/user_page.dart';
import 'ui/user/view_model/user_view_model.dart';

/// 应用根 Widget。只构造一次 [GoRouter]，并通过嵌套的
/// [ChangeNotifierProvider] 给每页注入对应的 ViewModel。
///
/// 路由共有 5 个 Tab：
/// - `/`        实时数据
/// - `/history` 历史曲线
/// - `/control` 继电器控制
/// - `/alarm`   告警列表
/// - `/user`    用户（含登录表单与已登录设置面板）
///
/// 未登录时三个数据 Tab 仍可见，仅展示空壳。
/// 登录态切换通过 `refreshListenable: AuthRepository` 让 GoRouter 重新评估。
class FlowmeterApp extends StatefulWidget {
  const FlowmeterApp({super.key});

  @override
  State<FlowmeterApp> createState() => _FlowmeterAppState();
}

class _FlowmeterAppState extends State<FlowmeterApp>
    with WidgetsBindingObserver {
  // GoRouter 只在 initState 构造一次，避免热重载时重复创建导致路由状态丢失。
  late final GoRouter _router;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    // 取出 AuthRepository，用作 GoRouter 的刷新监听器：
    // 登录 / 退出时它会 notifyListeners，Router 收到后会重建当前路由。
    final auth = context.read<AuthRepository>();
    _router = _buildRouter(auth);
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.resumed) {
      unawaited(context.read<AuthRepository>().refreshRealtimeSession());
    }
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Consumer<ThemeModeController>(
      builder: (context, themeController, _) {
        return MaterialApp.router(
          title: '磨坊系统',
          // 亮色 / 暗色主题固定，具体跟随哪套由用户偏好决定。
          theme: AppTheme.light(),
          darkTheme: AppTheme.dark(),
          themeMode: themeController.themeMode,
          routerConfig: _router,
          debugShowCheckedModeBanner: false,
        );
      },
    );
  }

  /// 构造底部 Tab 导航。每个 Tab 都是一个独立 [StatefulShellBranch]，
  /// 这样切换 Tab 时各自的页面状态（滚动位置、表单等）不会被销毁。
  GoRouter _buildRouter(AuthRepository auth) {
    return GoRouter(
      initialLocation: '/',
      // 监听登录态变化：登录 / 退出后让 Router 重新评估，
      // 让用户 Tab 在两态之间切换 UI 时立即生效。
      refreshListenable: auth,
      routes: [
        StatefulShellRoute.indexedStack(
          // HomeShell 负责绘制底部导航栏并切换当前 Branch。
          builder: (context, state, navShell) => HomeShell(navigationShell: navShell),
          branches: [
            // 实时数据页（首页）。
            StatefulShellBranch(routes: [
              GoRoute(
                path: '/',
                builder: (context, state) => ChangeNotifierProvider(
                  create: (c) => DashboardViewModel(
                    repository: c.read(),
                    auth: c.read<AuthRepository>(),
                  ),
                  child: const DashboardPage(),
                ),
              ),
            ]),
            // 历史曲线页。
            StatefulShellBranch(routes: [
              GoRoute(
                path: '/history',
                builder: (context, state) => ChangeNotifierProvider(
                  create: (c) => HistoryViewModel(
                    repository: c.read(),
                    auth: c.read<AuthRepository>(),
                  ),
                  child: const HistoryPage(),
                ),
              ),
            ]),
            // 继电器控制页。
            StatefulShellBranch(routes: [
              GoRoute(
                path: '/control',
                builder: (context, state) => ChangeNotifierProvider(
                  create: (c) => ControlViewModel(
                    repository: c.read(),
                    measurements: c.read(),
                    auth: c.read<AuthRepository>(),
                  ),
                  child: const ControlPage(),
                ),
              ),
            ]),
            // 告警列表页。
            StatefulShellBranch(routes: [
              GoRoute(
                path: '/alarm',
                builder: (context, state) => ChangeNotifierProvider(
                  create: (c) => AlarmViewModel(
                    repository: c.read(),
                    auth: c.read<AuthRepository>(),
                  ),
                  child: const AlarmPage(),
                ),
              ),
            ]),
            // 用户页：未登录时显示登录表单，登录后显示设置 + 登出。
            StatefulShellBranch(routes: [
              GoRoute(
                path: '/user',
                builder: (context, state) => ChangeNotifierProvider(
                  create: (c) => UserViewModel(
                    repository: c.read<AuthRepository>(),
                    commands: c.read(),
                    preferences: c.read(),
                  ),
                  child: const UserPage(),
                ),
                routes: [
                  GoRoute(
                    path: 'settings',
                    builder: (context, state) => const SettingsPage(),
                  ),
                ],
              ),
            ]),
          ],
        ),
      ],
    );
  }
}
