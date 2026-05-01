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
          padding: const EdgeInsets.all(16),
          children: [
            // 页面顶部只保留一行标题 + 位图，避免卡片里再重复一份。
            Row(
              children: [
                const Text(
                  '继电器控制',
                  style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600),
                ),
                const SizedBox(width: 16),
                Text(
                  '0x${vm.relayMask.toRadixString(16).padLeft(4, '0').toUpperCase()}',
                  style: Theme.of(context).textTheme.bodyMedium,
                ),
              ],
            ),
            const SizedBox(height: 16),
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
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
                            childAspectRatio: compact ? 2.2 : 2.8,
                            mainAxisSpacing: 8,
                            crossAxisSpacing: 8,
                          ),
                          itemBuilder: (context, index) {
                            final enabled = vm.relayEnabled(index);
                            return Card(
                              margin: EdgeInsets.zero,
                              child: Padding(
                                padding: const EdgeInsets.symmetric(horizontal: 12),
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
                                      onChanged: vm.busy
                                          ? null
                                          : (value) => vm.setRelay(index, value),
                                    ),
                                  ],
                                ),
                              ),
                            );
                          },
                        );
                      },
                    ),
                    if (vm.busy) ...[
                      const SizedBox(height: 12),
                      const LinearProgressIndicator(),
                    ],
                  ],
                ),
              ),
            ),
          ],
        );
      },
    );
  }
}
