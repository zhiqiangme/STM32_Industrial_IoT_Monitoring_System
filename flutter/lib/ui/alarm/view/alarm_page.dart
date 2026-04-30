import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import 'package:provider/provider.dart';

import '../../../data/models/alarm.dart';
import '../view_model/alarm_view_model.dart';

/// 报警页：列表 + 下拉刷新 + 浮动"标记已读"按钮。
class AlarmPage extends StatelessWidget {
  const AlarmPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Consumer<AlarmViewModel>(
      builder: (context, vm, _) {
        return Scaffold(
          body: RefreshIndicator(
            onRefresh: vm.load,
            child: vm.loading && vm.items.isEmpty
                // 首次拉取且无数据：居中 loading。
                ? const Center(child: CircularProgressIndicator())
                : vm.items.isEmpty
                    // 拉完依然为空：空态。
                    ? const _EmptyState()
                    : ListView.separated(
                        itemCount: vm.items.length,
                        separatorBuilder: (_, _) => const Divider(height: 1),
                        itemBuilder: (context, i) => _AlarmTile(alarm: vm.items[i]),
                      ),
          ),
          // 有未读时悬浮按钮：一键全部已读。
          floatingActionButton: vm.unread > 0
              ? FloatingActionButton.extended(
                  onPressed: vm.markAllRead,
                  icon: const Icon(Icons.done_all),
                  label: Text('标记已读 (${vm.unread})'),
                )
              : null,
        );
      },
    );
  }
}

/// 空态视图。
class _EmptyState extends StatelessWidget {
  const _EmptyState();

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(Icons.notifications_none,
              size: 48, color: Theme.of(context).colorScheme.outline),
          const SizedBox(height: 8),
          const Text('暂无报警'),
        ],
      ),
    );
  }
}

/// 单条告警条目：左侧色环 + 中间标题/时间 + 右侧已读勾。
class _AlarmTile extends StatelessWidget {
  const _AlarmTile({required this.alarm});
  final Alarm alarm;

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    // 颜色随严重等级映射：红 / 橙 / 蓝。
    final color = switch (alarm.severity) {
      AlarmSeverity.critical => cs.error,
      AlarmSeverity.warn => Colors.orange,
      AlarmSeverity.info => cs.primary,
    };
    return ListTile(
      leading: CircleAvatar(
        backgroundColor: color.withValues(alpha: 0.15),
        child: Icon(Icons.warning_amber_rounded, color: color),
      ),
      title: Text(
        alarm.displayTitle,
        style: const TextStyle(fontWeight: FontWeight.w600),
      ),
      subtitle: Text(
        '${DateFormat('yyyy-MM-dd HH:mm:ss').format(alarm.timestamp)}'
        // 有触发值时附带显示，便于判断阈值差距。
        '${alarm.value != null ? '  ·  值 ${alarm.value}' : ''}',
      ),
      // 已确认的告警末尾打个绿勾。
      trailing: alarm.acknowledged
          ? const Icon(Icons.check_circle, color: Colors.green, size: 20)
          : null,
    );
  }
}
