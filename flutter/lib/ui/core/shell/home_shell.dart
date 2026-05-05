import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';
import 'package:provider/provider.dart';

import '../../../data/repositories/alarm_repository.dart';

/// 顶层 Scaffold：承载五个 Tab（实时 / 控制 / 历史 / 警报 / 用户）。
///
/// 使用 go_router 的 [StatefulNavigationShell]，
/// 切换 Tab 时各自的页面状态（滚动位置、表单值等）会被保留。
///
/// 未登录时三个数据 Tab 仍然可见，但实时页主体留白，
/// "用户"Tab 内部根据登录态切换登录表单 / 设置面板。
class HomeShell extends StatelessWidget {
  const HomeShell({super.key, required this.navigationShell});

  /// 由 go_router 注入的 shell 实例，
  /// 通过它驱动 Tab 切换并维护各 Branch 的导航栈。
  final StatefulNavigationShell navigationShell;

  @override
  Widget build(BuildContext context) {
    // 监听未读告警数，用 Provider.watch 触发本 widget 在徽标变化时重建。
    final alarmRepo = context.watch<AlarmRepository>();
    final location = GoRouterState.of(context).uri.path;
    final hideShellChrome = location.startsWith('/user/settings');
    return Scaffold(
      appBar: hideShellChrome
          ? null
          : AppBar(
              // 手机竖屏空间紧，标题栏只保留识别当前页面所需高度。
              toolbarHeight: 48,
              title: Text(_titleFor(navigationShell.currentIndex)),
            ),
      body: Provider<int>.value(
        value: navigationShell.currentIndex,
        child: navigationShell,
      ),
      bottomNavigationBar: hideShellChrome
          ? null
          : StreamBuilder<int>(
              stream: alarmRepo.unreadStream,
              // initialData 让首帧不闪：直接用仓库当前的未读数。
              initialData: alarmRepo.unreadCount,
              builder: (context, snap) {
                final unread = snap.data ?? 0;
                return NavigationBar(
                  // 减少底部导航固定占高，给实时数据卡片留出完整显示空间。
                  height: 68,
                  selectedIndex: navigationShell.currentIndex,
                  onDestinationSelected: (i) {
                    // 切换 Tab 时清除 SnackBar，避免控制页通知残留在其他页面。
                    ScaffoldMessenger.of(context).clearSnackBars();
                    navigationShell.goBranch(
                      i,
                      // 再次点击当前 Tab 时回到该 Branch 的 initial location，
                      // 实现"再点回到首页"的常见交互。
                      initialLocation: i == navigationShell.currentIndex,
                    );
                  },
                  destinations: [
                    const NavigationDestination(
                      icon: Icon(Icons.dashboard_outlined),
                      selectedIcon: Icon(Icons.dashboard),
                      label: '实时',
                    ),
                    const NavigationDestination(
                      icon: Icon(Icons.show_chart),
                      selectedIcon: Icon(Icons.show_chart),
                      label: '历史',
                    ),
                    const NavigationDestination(
                      icon: Icon(Icons.tune_outlined),
                      selectedIcon: Icon(Icons.tune),
                      label: '控制',
                    ),
                    // 警报 Tab：未读数大于 0 时显示数字徽标。
                    NavigationDestination(
                      icon: Badge(
                        isLabelVisible: unread > 0,
                        label: Text('$unread'),
                        child: const Icon(Icons.notifications_none),
                      ),
                      selectedIcon: Badge(
                        isLabelVisible: unread > 0,
                        label: Text('$unread'),
                        child: const Icon(Icons.notifications),
                      ),
                      label: '警报',
                    ),
                    // 用户 Tab：登录入口与账号设置。
                    const NavigationDestination(
                      icon: Icon(Icons.person_outline),
                      selectedIcon: Icon(Icons.person),
                      label: '用户',
                    ),
                  ],
                );
              },
            ),
    );
  }

  /// 根据当前 Tab 下标决定 AppBar 标题。
  String _titleFor(int index) => switch (index) {
    0 => '实时数据',
    1 => '历史曲线',
    2 => '继电器控制',
    3 => '警报',
    4 => '用户',
    _ => '磨坊系统',
  };
}
