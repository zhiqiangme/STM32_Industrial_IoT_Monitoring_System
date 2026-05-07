import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../view_model/control_view_model.dart';

/// 控制页：展示当前继电器输出，并允许逐路开关。
class ControlPage extends StatelessWidget {
  const ControlPage({super.key});

  /// 弹出二次确认对话框，确认后执行全部开启或全部关闭。
  Future<void> _confirmSetAll(
      BuildContext context, ControlViewModel vm, bool enabled) async {
    final action = enabled ? '开启' : '关闭';
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text('确认全部$action'),
        content: Text('确定要将所有 16 路继电器全部$action吗？'),
        actions: [
          FilledButton(
            onPressed: () => Navigator.of(ctx).pop(false),
            child: const Text('取消'),
          ),
          TextButton(
            onPressed: () => Navigator.of(ctx).pop(true),
            child: Text('确定'),
          ),
        ],
      ),
    );
    if (confirmed == true) {
      vm.setAllRelays(enabled);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Consumer<ControlViewModel>(
      builder: (context, vm, _) {
        // 只在控制页面处于活跃状态时显示 SnackBar，
        // 避免切换到其他页面后仍弹出通知。
        final isActive = context.watch<int>() == 2;
        final msg = vm.message;
        if (msg != null && isActive) {
          WidgetsBinding.instance.addPostFrameCallback((_) {
            if (!context.mounted) return;
            ScaffoldMessenger.of(context).clearSnackBars();
            ScaffoldMessenger.of(context)
                .showSnackBar(SnackBar(content: Text(msg)));
            vm.clearMessage();
          });
        } else if (msg != null && !isActive) {
          // 页面不活跃时静默清除消息，防止切换回来后重复弹出。
          WidgetsBinding.instance.addPostFrameCallback((_) {
            vm.clearMessage();
          });
        }
        if (!vm.isLoggedIn) {
          return const SizedBox.expand();
        }

        return ListView(
          // 手机竖屏优先保证 16 个按钮一页完整显示，适当收紧四周留白。
          padding: const EdgeInsets.fromLTRB(12, 8, 12, 8),
          children: [
            // 全部开启 / 全部关闭 按钮
            Padding(
              padding: const EdgeInsets.only(bottom: 8),
              child: Row(
                children: [
                  Expanded(
                    child: OutlinedButton.icon(
                      onPressed: () => _confirmSetAll(context, vm, true),
                      icon: const Icon(Icons.power_settings_new),
                      label: const Text('全部开启'),
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: OutlinedButton.icon(
                      onPressed: () => _confirmSetAll(context, vm, false),
                      icon: const Icon(Icons.power_off),
                      label: const Text('全部关闭'),
                    ),
                  ),
                ],
              ),
            ),
            LayoutBuilder(
              builder: (context, constraints) {
                final compact = constraints.maxWidth < 600;
                final crossAxisCount = compact ? 2 : 4;
                return GridView.builder(
                  shrinkWrap: true,
                  physics: const NeverScrollableScrollPhysics(),
                  itemCount: 16,
                  gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                    crossAxisCount: crossAxisCount,
                    // 竖屏时把卡片压扁一些，避免 CH15/CH16 被挤到屏幕外。
                    childAspectRatio: compact ? 2.9 : 3.2,
                    mainAxisSpacing: 6,
                    crossAxisSpacing: 6,
                  ),
                  itemBuilder: (context, index) {
                    final enabled = vm.relayEnabled(index);
                    return Card(
                      margin: EdgeInsets.zero,
                      child: Padding(
                        padding: const EdgeInsets.symmetric(horizontal: 10),
                        child: Row(
                          children: [
                            Expanded(
                              child: Text(
                                'CH${index + 1}',
                                style: const TextStyle(fontWeight: FontWeight.w600),
                              ),
                            ),
                            Switch(
                              value: enabled,
                              onChanged: (value) => vm.setRelay(index, value),
                            ),
                          ],
                        ),
                      ),
                    );
                  },
                );
              },
            ),
            if (vm.hasPending) ...[
              const SizedBox(height: 12),
              const LinearProgressIndicator(),
            ],
          ],
        );
      },
    );
  }
}
