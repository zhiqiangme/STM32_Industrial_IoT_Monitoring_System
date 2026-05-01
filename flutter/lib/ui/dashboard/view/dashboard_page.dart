import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../../../data/models/measurement.dart';
import '../../core/widgets/status_banner.dart';
import '../../core/widgets/value_tile.dart';
import '../view_model/dashboard_view_model.dart';

/// 实时数据页：顶部状态横幅 + 主指标卡 + 温度/继电器状态卡。
class DashboardPage extends StatelessWidget {
  /// 卡片间距。
  static const double _gridSpacing = 6;

  /// 宽屏下主指标卡片的最大宽度。
  static const double _mainTileWideMaxExtent = 320;

  /// 宽屏下温度卡片的最大宽度。
  static const double _temperatureTileWideMaxExtent = 230;

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

  /// 把测量数据拆分成主指标 + 温度/状态两组卡片。
  Widget _tiles(BuildContext context, Measurement? m) {
    // 缺测时显示 "--"，不显示 "NaN"。
    String fmt(double? v, [int digits = 2]) =>
        v?.toStringAsFixed(digits) ?? '--';
    String fmtMask(int? mask) =>
        mask == null ? '--' : '0x${mask.toRadixString(16).padLeft(4, '0').toUpperCase()}';

    // 4 张主指标。
    final metricTiles = [
      ValueTile(
        label: '瞬时流量',
        value: fmt(m?.flow),
        unit: 'L/min',
        valid: m?.flowValid ?? true,
      ),
      ValueTile(
        label: '累计量',
        value: fmt(m?.total),
        unit: 'L',
        valid: m?.totalValid ?? true,
      ),
      ValueTile(
        label: '重量',
        value: fmt(m?.weight, 0),
        unit: 'g',
        valid: m?.weightValid ?? true,
      ),
      ValueTile(
        label: '控制模式',
        value: m == null ? '--' : (m.autoMode ? '自动' : '手动'),
        unit: '',
        valid: true,
      ),
    ];
    // 4 路 PT100 温度槽位（T1-T4）。
    final temperatureTiles = List.generate(4, (i) {
      return ValueTile(
        label: 'T${i + 1}',
        value: fmt(m == null ? null : m.temperatures[i], 1),
        unit: '°C',
        valid: m?.temperatureValid(i) ?? true,
      );
    });
    final heartTile = ValueTile(
      label: '心跳计数',
      value: m?.heartCount?.toString() ?? '--',
      unit: '',
      valid: true,
    );
    final relayOutTile = ValueTile(
      label: '继电器输出',
      value: fmtMask(m?.relayDo),
      unit: '',
      valid: true,
    );
    final relayInTile = ValueTile(
      label: '继电器输入',
      value: fmtMask(m?.relayDi),
      unit: '',
      valid: true,
    );
    final temperatureAndHeartTiles = [
      ...temperatureTiles,
      relayOutTile,
      relayInTile,
      heartTile,
    ];

    return LayoutBuilder(
      builder: (context, constraints) {
        final width = constraints.maxWidth;
        // 600 是 Material 设计常用的 sm/md 断点：手机以下叫紧凑模式。
        final compactWidth = width < 600;

        return Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // 主指标网格：紧凑模式下强制 2 列；宽屏下按最大宽度自适应列数。
            GridView.builder(
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              itemCount: metricTiles.length,
              gridDelegate: SliverGridDelegateWithMaxCrossAxisExtent(
                maxCrossAxisExtent: compactWidth
                    ? _maxExtentForColumns(width, 2)
                    : _mainTileWideMaxExtent,
                // 主指标只保留必要留白，避免首页必须滚动两屏才能看完。
                mainAxisExtent: compactWidth ? 112 : 156,
                mainAxisSpacing: _gridSpacing,
                crossAxisSpacing: _gridSpacing,
              ),
              itemBuilder: (context, index) => metricTiles[index],
            ),
            const SizedBox(height: 10),
            // 温度和继电器状态分区标题。
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 2),
              child: Text(
                '温度 / 状态',
                style: Theme.of(
                  context,
                ).textTheme.titleMedium?.copyWith(fontWeight: FontWeight.w600),
              ),
            ),
            const SizedBox(height: 4),
            // 温度网格：紧凑模式下强制 3 列；宽屏下按最大宽度自适应。
            GridView.builder(
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              itemCount: temperatureAndHeartTiles.length,
              gridDelegate: SliverGridDelegateWithMaxCrossAxisExtent(
                maxCrossAxisExtent: compactWidth
                    ? _maxExtentForColumns(width, 3)
                    : _temperatureTileWideMaxExtent,
                // 手机竖屏下压缩卡片高度，保证 4 路温度尽量同屏展示。
                mainAxisExtent: compactWidth ? 94 : 128,
                mainAxisSpacing: _gridSpacing,
                crossAxisSpacing: _gridSpacing,
              ),
              itemBuilder: (context, index) => temperatureAndHeartTiles[index],
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
