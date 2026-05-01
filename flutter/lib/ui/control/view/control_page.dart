import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../view_model/control_view_model.dart';

/// 控制页：展示当前继电器输出，并允许逐路开关。
class ControlPage extends StatelessWidget {
  const ControlPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Consumer<ControlViewModel>(
      builder: (context, vm, _) {
        // ViewModel 里有一次性消息时弹 SnackBar；
        // 用 post-frame callback 避免在 build 阶段直接调度。
        final msg = vm.message;
        if (msg != null) {
          WidgetsBinding.instance.addPostFrameCallback((_) {
            ScaffoldMessenger.of(context).clearSnackBars();
            ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(msg)));
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
