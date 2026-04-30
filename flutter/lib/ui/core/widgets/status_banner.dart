import 'package:flutter/material.dart';
import 'package:intl/intl.dart';

import '../../../data/models/device_status.dart';

/// 顶部一条横幅：展示设备在线 / 离线 + 最近一次上报时间。
///
/// 在"设备通常处于离线"的部署里，离线 + 时间戳陈旧是常态，
/// 因此样式按提示信息而非错误来呈现，避免给用户造成"出错了"错觉。
class StatusBanner extends StatelessWidget {
  const StatusBanner({super.key, required this.status});

  final DeviceStatus status;

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    final isOnline = status.online;
    // 在线用主色容器；离线退化成普通中性色调。
    final bg = isOnline ? cs.primaryContainer : cs.surfaceContainerHighest;
    final fg = isOnline ? cs.onPrimaryContainer : cs.onSurfaceVariant;

    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 6),
      color: bg,
      child: Row(
        children: [
          Icon(
            isOnline ? Icons.cloud_done : Icons.cloud_off,
            size: 18,
            color: fg,
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              _message(),
              style: TextStyle(color: fg, fontSize: 13),
            ),
          ),
        ],
      ),
    );
  }

  /// 根据当前状态拼出展示文案。
  /// 离线时附带"最后上报于 …"，并按时间差选择合适粒度。
  String _message() {
    if (status.online) return '设备在线';
    final last = status.lastSeen;
    if (last == null) return '设备离线';
    final now = DateTime.now();
    final diff = now.difference(last);
    if (diff.inMinutes < 1) return '设备离线 · 最后上报于刚刚';
    if (diff.inMinutes < 60) return '设备离线 · 最后上报于 ${diff.inMinutes} 分钟前';
    if (diff.inHours < 24) return '设备离线 · 最后上报于 ${diff.inHours} 小时前';
    // 时间差超过一天后用绝对日期，避免出现"× 小时前"读起来很大的数字。
    return '设备离线 · 最后上报于 ${DateFormat('MM-dd HH:mm').format(last)}';
  }
}
