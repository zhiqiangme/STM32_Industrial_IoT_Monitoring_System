import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../view_model/control_view_model.dart';

/// 控制页：远程操作（重启设备 + 未来的参数下发）。
///
/// 当前未挂路由，作为占位 UI 保留。
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
        return Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              // 卡片 1：远程操作（当前只有重启）。
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text('远程操作',
                          style: TextStyle(fontSize: 16, fontWeight: FontWeight.w600)),
                      const SizedBox(height: 8),
                      const Text(
                        '重启设备将使所有运行中的采集/上报任务暂停约 30 秒。',
                        style: TextStyle(fontSize: 13),
                      ),
                      const SizedBox(height: 16),
                      // busy 时按钮置灰并显示一个小转圈。
                      FilledButton.icon(
                        onPressed: vm.busy ? null : () => _confirmReboot(context, vm),
                        icon: vm.busy
                            ? const SizedBox(
                                width: 16, height: 16,
                                child: CircularProgressIndicator(strokeWidth: 2),
                              )
                            : const Icon(Icons.restart_alt),
                        label: Text(vm.busy ? '等待设备确认...' : '重启设备'),
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 16),
              // 卡片 2：占位，后续接"参数下发"。
              const Card(
                child: ListTile(
                  leading: Icon(Icons.tune),
                  title: Text('参数下发'),
                  subtitle: Text('K 系数、零点标定、报警阈值（后续版本支持）'),
                  enabled: false,
                ),
              ),
            ],
          ),
        );
      },
    );
  }

  /// 重启二次确认对话框。避免误触。
  Future<void> _confirmReboot(BuildContext context, ControlViewModel vm) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('确认重启'),
        content: const Text('设备将立即重启，是否继续？'),
        actions: [
          TextButton(onPressed: () => Navigator.pop(ctx, false), child: const Text('取消')),
          FilledButton(onPressed: () => Navigator.pop(ctx, true), child: const Text('重启')),
        ],
      ),
    );
    if (ok == true) vm.reboot();
  }
}
