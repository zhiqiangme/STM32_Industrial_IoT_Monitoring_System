import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../../data/models/measurement.dart';
import '../../core/widgets/status_banner.dart';
import '../../core/widgets/value_tile.dart';
import '../view_model/dashboard_view_model.dart';

/// 实时数据页：顶部状态横幅 + 所有指标统一大小卡片，按顺序排列。
class DashboardPage extends StatelessWidget {
  /// 卡片间距。
  static const double _gridSpacing = 6;

  /// 宽屏下卡片的最大宽度。
  static const double _tileWideMaxExtent = 230;

  const DashboardPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Consumer<DashboardViewModel>(
      builder: (context, vm, _) {
        if (!vm.isLoggedIn) {
          // 未登录时实时页保持留白，不提前暴露空壳文案。
          return const SizedBox.expand();
        }

        final m = vm.measurement;
        return RefreshIndicator(
          onRefresh: vm.refresh,
          child: CustomScrollView(
            slivers: [
              // 顶部一条状态横幅，长期占位。
              SliverToBoxAdapter(child: StatusBanner(status: vm.status)),
              SliverPadding(
                padding: const EdgeInsets.fromLTRB(10, 8, 10, 8),
                sliver: SliverToBoxAdapter(child: _tiles(context, m)),
              ),
            ],
          ),
        );
      },
    );
  }

  /// 把测量数据拆分成所有卡片，按顺序排列。
  Widget _tiles(BuildContext context, Measurement? m) {
    // 缺测时显示 "--"，不显示 "NaN"。
    String fmt(double? v, [int digits = 2]) =>
        v?.toStringAsFixed(digits) ?? '--';
    String fmtMask(int? mask) =>
        mask == null ? '--' : '0x${mask.toRadixString(16).padLeft(4, '0').toUpperCase()}';

    // 所有卡片按顺序排列：流量 → 重量 → 温度 → 状态
    final allTiles = [
      // 流量计1（当前接入）
      ValueTile(
        label: '流量1瞬时',
        value: fmt(m?.flow),
        unit: 'L/min',
        valid: m?.flowValid ?? true,
      ),
      ValueTile(
        label: '流量1累积',
        value: fmt(m?.total),
        unit: 'L',
        valid: m?.totalValid ?? true,
      ),
      // 流量计2（未接入）
      const ValueTile(
        label: '流量2瞬时',
        value: '--',
        unit: 'L/min',
        valid: false,
      ),
      const ValueTile(
        label: '流量2累积',
        value: '--',
        unit: 'L',
        valid: false,
      ),
      // 流量计3（未接入）
      const ValueTile(
        label: '流量3瞬时',
        value: '--',
        unit: 'L/min',
        valid: false,
      ),
      const ValueTile(
        label: '流量3累积',
        value: '--',
        unit: 'L',
        valid: false,
      ),
      // 重量通道（预留4个，当前只接CH3）
      ValueTile(
        label: '重量CH1',
        value: '--',
        unit: 'g',
        valid: false,
      ),
      ValueTile(
        label: '重量CH2',
        value: '--',
        unit: 'g',
        valid: false,
      ),
      ValueTile(
        label: '重量CH3',
        value: fmt(m?.weight, 0),
        unit: 'g',
        valid: m?.weightValid ?? true,
      ),
      ValueTile(
        label: '重量CH4',
        value: '--',
        unit: 'g',
        valid: false,
      ),
      // 4 路 PT100 温度槽位（T1-T4）。
      ...List.generate(4, (i) {
        return ValueTile(
          label: '温度T${i + 1}',
          value: fmt(m == null ? null : m.temperatures[i], 1),
          unit: '°C',
          valid: m?.temperatureValid(i) ?? true,
        );
      }),
      // 状态
      ValueTile(
        label: '控制模式',
        value: m == null ? '--' : (m.autoMode ? '自动' : '手动'),
        unit: '',
        valid: true,
      ),
      ValueTile(
        label: '继电器输出',
        value: fmtMask(m?.relayDo),
        unit: '',
        valid: true,
      ),
      ValueTile(
        label: '继电器输入',
        value: fmtMask(m?.relayDi),
        unit: '',
        valid: true,
      ),
      ValueTile(
        label: '心跳计数',
        value: m?.heartCount?.toString() ?? '--',
        unit: '',
        valid: true,
      ),
    ];

    return LayoutBuilder(
      builder: (context, constraints) {
        final width = constraints.maxWidth;
        // 600 是 Material 设计常用的 sm/md 断点：手机以下叫紧凑模式。
        final compactWidth = width < 600;

        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // 所有卡片统一大小，紧凑模式下强制 3 列；宽屏下按最大宽度自适应。
            GridView.builder(
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              itemCount: allTiles.length,
              gridDelegate: SliverGridDelegateWithMaxCrossAxisExtent(
                maxCrossAxisExtent: compactWidth
                    ? _maxExtentForColumns(width, 3)
                    : _tileWideMaxExtent,
                // 统一卡片高度
                mainAxisExtent: compactWidth ? 94 : 128,
                mainAxisSpacing: _gridSpacing,
                crossAxisSpacing: _gridSpacing,
              ),
              itemBuilder: (context, index) => allTiles[index],
            ),
          ],
        );
      },
    );
  }

  /// 紧凑模式下根据"想要的列数"反算每列最大宽度。
  static double _maxExtentForColumns(double width, int columns) {
    final totalSpacing = _gridSpacing * (columns - 1);
    return (width - totalSpacing) / columns;
  }
}
