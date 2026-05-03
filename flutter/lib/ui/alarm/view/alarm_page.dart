import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import 'package:provider/provider.dart';

import '../../../data/models/alarm.dart';
import '../view_model/alarm_view_model.dart';

/// 报警页：筛选栏 + 列表 + 下拉刷新 + 浮动"标记已读"按钮。
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
                : vm.items.isEmpty && vm.filter == null
                    // 拉完依然为空且无筛选：空态。
                    ? const _EmptyState()
                    : CustomScrollView(
                        slivers: [
                          SliverToBoxAdapter(
                            child: _FilterBar(
                              current: vm.filter,
                              onChanged: vm.setFilter,
                            ),
                          ),
                          if (vm.items.isEmpty)
                            const SliverFillRemaining(
                              hasScrollBody: false,
                              child: _EmptyState(),
                            )
                          else
                            SliverList.separated(
                              itemCount: vm.items.length,
                              separatorBuilder: (_, _) =>
                                  const Divider(height: 1),
                              itemBuilder: (context, i) {
                                final alarm = vm.items[i];
                                return _AlarmTile(
                                  alarm: alarm,
                                  expanded: vm.isExpanded(alarm.seq),
                                  onTap: () => vm.toggleExpanded(alarm.seq),
                                );
                              },
                            ),
                        ],
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

/// 严重等级筛选栏。
class _FilterBar extends StatelessWidget {
  const _FilterBar({required this.current, required this.onChanged});

  final AlarmSeverity? current;
  final ValueChanged<AlarmSeverity?> onChanged;

  @override
  Widget build(BuildContext context) {
    return SingleChildScrollView(
      scrollDirection: Axis.horizontal,
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
      child: Row(
        children: [
          _buildChip(context, label: '全部', selected: current == null,
              onTap: () => onChanged(null)),
          const SizedBox(width: 8),
          _buildChip(context,
              label: '提示',
              icon: Icons.info_outline,
              iconColor: Theme.of(context).colorScheme.primary,
              selected: current == AlarmSeverity.info,
              onTap: () => onChanged(AlarmSeverity.info)),
          const SizedBox(width: 8),
          _buildChip(context,
              label: '警告',
              icon: Icons.warning_amber_rounded,
              iconColor: Colors.orange,
              selected: current == AlarmSeverity.warn,
              onTap: () => onChanged(AlarmSeverity.warn)),
          const SizedBox(width: 8),
          _buildChip(context,
              label: '严重',
              icon: Icons.error_outline,
              iconColor: Theme.of(context).colorScheme.error,
              selected: current == AlarmSeverity.critical,
              onTap: () => onChanged(AlarmSeverity.critical)),
        ],
      ),
    );
  }

  Widget _buildChip(
    BuildContext context, {
    required String label,
    IconData? icon,
    Color? iconColor,
    required bool selected,
    required VoidCallback onTap,
  }) {
    return FilterChip(
      label: Text(label),
      avatar: icon != null
          ? Icon(icon, size: 18, color: iconColor)
          : null,
      selected: selected,
      onSelected: (_) => onTap(),
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

/// 单条告警条目：左侧色环 + 中间标题/时间 + 右侧已读勾，点击展开详情。
class _AlarmTile extends StatelessWidget {
  const _AlarmTile({
    required this.alarm,
    required this.expanded,
    required this.onTap,
  });

  final Alarm alarm;
  final bool expanded;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    // 颜色随严重等级映射：红 / 橙 / 蓝。
    final color = switch (alarm.severity) {
      AlarmSeverity.critical => cs.error,
      AlarmSeverity.warn => Colors.orange,
      AlarmSeverity.info => cs.primary,
    };
    return Column(
      children: [
        ListTile(
          leading: CircleAvatar(
            backgroundColor: color.withValues(alpha: 0.15),
            child: Icon(alarm.displayIcon, color: color),
          ),
          title: Text(
            alarm.displayTitle,
            style: const TextStyle(fontWeight: FontWeight.w600),
          ),
          subtitle: Text(
            DateFormat('yyyy-MM-dd HH:mm:ss').format(alarm.timestamp),
          ),
          // 已确认的告警末尾打个绿勾。
          trailing: alarm.acknowledged
              ? const Icon(Icons.check_circle, color: Colors.green, size: 20)
              : null,
          onTap: onTap,
        ),
        if (expanded) _buildDetail(context, color),
      ],
    );
  }

  Widget _buildDetail(BuildContext context, Color color) {
    final cs = Theme.of(context).colorScheme;
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.fromLTRB(72, 0, 16, 12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _detailRow('告警代码', alarm.code, cs),
          if (alarm.value != null)
            _detailRow('触发值', alarm.value!.toStringAsFixed(2), cs),
          _detailRow(
            '状态',
            alarm.acknowledged ? '已确认' : '未确认',
            cs,
          ),
          _detailRow(
            '等级',
            switch (alarm.severity) {
              AlarmSeverity.critical => '严重',
              AlarmSeverity.warn => '警告',
              AlarmSeverity.info => '提示',
            },
            cs,
          ),
        ],
      ),
    );
  }

  Widget _detailRow(String label, String value, ColorScheme cs) {
    return Padding(
      padding: const EdgeInsets.only(top: 4),
      child: Row(
        children: [
          Text(
            '$label：',
            style: TextStyle(fontSize: 12, color: cs.onSurfaceVariant),
          ),
          Text(
            value,
            style: TextStyle(
              fontSize: 12,
              color: cs.onSurface,
              fontWeight: FontWeight.w500,
            ),
          ),
        ],
      ),
    );
  }
}
