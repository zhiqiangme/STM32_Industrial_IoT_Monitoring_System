import 'dart:math' as math;

import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import 'package:provider/provider.dart';

import '../../../data/models/history_point.dart';
import '../view_model/history_view_model.dart';

const _rangePresets = <({int minutes, String label})>[
  (minutes: 5, label: '5分钟'),
  (minutes: 30, label: '30分钟'),
  (minutes: 60, label: '1小时'),
  (minutes: 240, label: '4小时'),
  (minutes: 720, label: '12小时'),
  (minutes: 1440, label: '24小时'),
  (minutes: 4320, label: '3天'),
  (minutes: 10080, label: '7天'),
];

/// 历史曲线页：顶部一行选项（字段 + 时间段）+ 下方填满剩余空间的折线图。
class HistoryPage extends StatelessWidget {
  const HistoryPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Consumer<HistoryViewModel>(
      builder: (context, vm, _) {
        return Column(
          children: [
            _Controls(vm: vm),
            Expanded(child: _Chart(vm: vm)),
          ],
        );
      },
    );
  }
}

/// 顶部控制条：字段下拉 + 时间区段单选。
class _Controls extends StatelessWidget {
  const _Controls({required this.vm});
  final HistoryViewModel vm;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
      child: LayoutBuilder(
        builder: (context, constraints) {
          // 窄屏（手机竖屏）下下拉框 + segmented 同行容易挤压，改两行布局。
          final narrow = constraints.maxWidth < 480;
          final dropdown = DropdownButtonFormField<HistoryField>(
            initialValue: vm.field,
            decoration: const InputDecoration(
              labelText: '字段',
              isDense: true,
              border: OutlineInputBorder(),
            ),
            items: HistoryField.values
                .map((f) => DropdownMenuItem(value: f, child: Text(f.label)))
                .toList(),
            onChanged: (f) {
              if (f != null) vm.setField(f);
            },
          );
          final segmented = SegmentedButton<int>(
            segments: [
              for (final preset in _rangePresets)
                ButtonSegment(
                  value: preset.minutes,
                  label: Text(preset.label),
                ),
            ],
            selected: {_rangeMinutes(vm)},
            showSelectedIcon: false,
            onSelectionChanged: (s) {
              final minutes = s.first;
              final to = DateTime.now();
              vm.setRange(to.subtract(Duration(minutes: minutes)), to);
            },
          );
          final scrollableSegmented = SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: segmented,
          );
          if (narrow) {
            return Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                dropdown,
                const SizedBox(height: 8),
                Align(
                  alignment: Alignment.centerLeft,
                  child: scrollableSegmented,
                ),
              ],
            );
          }
          return Row(
            children: [
              Expanded(child: dropdown),
              const SizedBox(width: 8),
              Flexible(child: scrollableSegmented),
            ],
          );
        },
      ),
    );
  }

  /// 把 [HistoryViewModel.from..to] 折算成最近的一档分钟区间。
  int _rangeMinutes(HistoryViewModel vm) {
    final target = vm.to.difference(vm.from).inMinutes;
    var best = _rangePresets.first.minutes;
    var bestDiff = (target - best).abs();
    for (final preset in _rangePresets.skip(1)) {
      final diff = (target - preset.minutes).abs();
      if (diff < bestDiff) {
        best = preset.minutes;
        bestDiff = diff;
      }
    }
    return best;
  }
}

/// 折线图主体（fl_chart）。
class _Chart extends StatefulWidget {
  const _Chart({required this.vm});
  final HistoryViewModel vm;

  @override
  State<_Chart> createState() => _ChartState();
}

class _ChartState extends State<_Chart> {
  String? _lastViewportSignature;
  double? _viewportMinX;
  double? _viewportMaxX;

  @override
  Widget build(BuildContext context) {
    final vm = widget.vm;

    // 三种空 / 错状态分支：loading / error / 空数据。
    if (vm.loading && vm.points.isEmpty) {
      return const Center(child: CircularProgressIndicator());
    }
    if (vm.error != null) {
      return Center(child: Text('加载失败：${vm.error}'));
    }
    if (vm.isEmpty) {
      return const Center(child: Text('此时间段内无数据'));
    }

    // 把 HistoryPoint 转成 fl_chart 的 FlSpot：x 用毫秒时间戳。
    final rawSpots = vm.points
        .map(
          (p) => FlSpot(p.timestamp.millisecondsSinceEpoch.toDouble(), p.value),
        )
        .toList();
    final hasSinglePoint = rawSpots.length == 1;
    final spots = <FlSpot>[];
    for (var i = 0; i < rawSpots.length; i++) {
      spots.add(rawSpots[i]);
      if (i == rawSpots.length - 1) continue;
      final current = vm.points[i].timestamp;
      final next = vm.points[i + 1].timestamp;
      if (next.difference(current) > const Duration(seconds: 20)) {
        // 中间缺了一段历史点时插入断点，避免前后两段折线被强行连起来。
        spots.add(FlSpot.nullSpot);
      }
    }
    final xs = rawSpots.map((s) => s.x);
    final ys = rawSpots.map((s) => s.y);
    final rawMinX = xs.reduce((a, b) => a < b ? a : b);
    final rawMaxX = xs.reduce((a, b) => a > b ? a : b);
    final minY = ys.reduce((a, b) => a < b ? a : b);
    final maxY = ys.reduce((a, b) => a > b ? a : b);
    final ySpan = maxY - minY < 0.001 ? 1.0 : maxY - minY;

    return Padding(
      padding: const EdgeInsets.fromLTRB(8, 4, 16, 12),
      child: LayoutBuilder(
        builder: (context, constraints) {
          // 外框尺寸固定，图内时间窗通过拖动平移；这里只按视口宽度调节字号和留白。
          final narrow = constraints.maxWidth < 480;
          final xFontSize = narrow ? 9.0 : 10.0;
          final xReserved = narrow ? 28.0 : 32.0;
          final yFontSize = narrow ? 9.0 : 11.0;
          final yReserved = narrow ? 48.0 : 44.0;
          final xLabelFormat = _xAxisFormat(vm.from, vm.to);
          final xAxis = _buildXAxisSpec(
            from: vm.from,
            to: vm.to,
            narrow: narrow,
            rawMinX: rawMinX,
            rawMaxX: rawMaxX,
          );
          final viewportSpan = _viewportSpanForAxis(
            xAxis: xAxis,
            narrow: narrow,
          );
          _ensureViewport(
            signature:
                '${vm.from.millisecondsSinceEpoch}:${vm.to.millisecondsSinceEpoch}:${xAxis.interval.round()}',
            fullAxis: xAxis,
            viewportSpan: viewportSpan,
          );

          final visibleMinX = _viewportMinX ?? xAxis.minX;
          final visibleMaxX = _viewportMaxX ?? xAxis.maxX;

          return GestureDetector(
            behavior: HitTestBehavior.opaque,
            onHorizontalDragUpdate: (details) {
              final plotWidth = math.max(1.0, constraints.maxWidth - yReserved - 24.0);
              final visibleSpan = visibleMaxX - visibleMinX;
              final shift = -details.delta.dx * (visibleSpan / plotWidth);
              if (shift == 0) return;
              final fullSpan = xAxis.maxX - xAxis.minX;
              if (fullSpan <= visibleSpan) return;

              var nextMin = visibleMinX + shift;
              var nextMax = visibleMaxX + shift;
              if (nextMin < xAxis.minX) {
                nextMin = xAxis.minX;
                nextMax = nextMin + visibleSpan;
              }
              if (nextMax > xAxis.maxX) {
                nextMax = xAxis.maxX;
                nextMin = nextMax - visibleSpan;
              }

              if ((nextMin - visibleMinX).abs() < 1.0 &&
                  (nextMax - visibleMaxX).abs() < 1.0) {
                return;
              }

              setState(() {
                _viewportMinX = nextMin;
                _viewportMaxX = nextMax;
              });
            },
            child: Stack(
              children: [
                LineChart(
                  LineChartData(
                    minX: visibleMinX,
                    maxX: visibleMaxX,
                    // y 轴上下各留 10% 余量，避免曲线贴边。
                    minY: minY - ySpan * 0.1,
                    maxY: maxY + ySpan * 0.1,
                    titlesData: FlTitlesData(
                      // 屏蔽顶部 / 右侧坐标轴标题。
                      rightTitles: const AxisTitles(
                        sideTitles: SideTitles(showTitles: false),
                      ),
                      topTitles: const AxisTitles(
                        sideTitles: SideTitles(showTitles: false),
                      ),
                      // 外框固定不动，拖动时只改变图内可见时间窗。
                      bottomTitles: AxisTitles(
                        sideTitles: SideTitles(
                          showTitles: true,
                          reservedSize: xReserved,
                          interval: xAxis.interval,
                          getTitlesWidget: (value, meta) {
                            if (_isAxisEdgeLabel(value, meta)) {
                              return const SizedBox.shrink();
                            }
                            final d = DateTime.fromMillisecondsSinceEpoch(
                              value.toInt(),
                            );
                            return SideTitleWidget(
                              axisSide: meta.axisSide,
                              space: 4,
                              fitInside: SideTitleFitInsideData.fromTitleMeta(
                                meta,
                                distanceFromEdge: 0,
                              ),
                              child: Text(
                                DateFormat(xLabelFormat).format(d),
                                style: TextStyle(fontSize: xFontSize),
                                textAlign: TextAlign.center,
                              ),
                            );
                          },
                        ),
                      ),
                      // y 轴：把单位（如 L/min）作为坐标轴名展示，刻度字号随宽度调节。
                      leftTitles: AxisTitles(
                        axisNameWidget: Text(vm.field.unit),
                        sideTitles: SideTitles(
                          showTitles: true,
                          reservedSize: yReserved,
                          getTitlesWidget: (value, meta) {
                            if (_isAxisEdgeLabel(value, meta)) {
                              return const SizedBox.shrink();
                            }
                            return Padding(
                              padding: const EdgeInsets.only(right: 4),
                              child: Text(
                                meta.formattedValue,
                                style: TextStyle(fontSize: yFontSize),
                              ),
                            );
                          },
                        ),
                      ),
                    ),
                    gridData: const FlGridData(show: true),
                    borderData: FlBorderData(show: true),
                    // 触摸 tooltip：只显示数值，省略时间（x 轴本身已经标了）。
                    lineTouchData: LineTouchData(
                      touchTooltipData: LineTouchTooltipData(
                        getTooltipItems: (touchedSpots) {
                          return touchedSpots.map((spot) {
                            return LineTooltipItem(
                              spot.y.toStringAsFixed(1),
                              const TextStyle(
                                color: Colors.white,
                                fontWeight: FontWeight.w600,
                                fontSize: 14,
                              ),
                            );
                          }).toList();
                        },
                      ),
                    ),
                    lineBarsData: [
                      LineChartBarData(
                        spots: spots,
                        isCurved: true,
                        barWidth: 2,
                        color: Theme.of(context).colorScheme.primary,
                        // 只有一个点时显式画圆点，否则折线会看起来像空白图。
                        dotData: FlDotData(
                          show: hasSinglePoint,
                          getDotPainter: (spot, percent, bar, index) {
                            return FlDotCirclePainter(
                              radius: 4,
                              color: Theme.of(context).colorScheme.primary,
                              strokeWidth: 1.5,
                              strokeColor: Colors.white,
                            );
                          },
                        ),
                      ),
                    ],
                  ),
                  duration: Duration.zero,
                ),
                if (hasSinglePoint)
                  const Positioned(
                    top: 8,
                    right: 8,
                    child: DecoratedBox(
                      decoration: BoxDecoration(
                        color: Color(0xCCFFFFFF),
                        borderRadius: BorderRadius.all(Radius.circular(8)),
                      ),
                      child: Padding(
                        padding: EdgeInsets.symmetric(
                          horizontal: 8,
                          vertical: 4,
                        ),
                        child: Text(
                          '当前时间段仅 1 个点',
                          style: TextStyle(fontSize: 12),
                        ),
                      ),
                    ),
                  ),
              ],
            ),
          );
        },
      ),
    );
  }

  void _ensureViewport({
    required String signature,
    required _XAxisSpec fullAxis,
    required double viewportSpan,
  }) {
    if (_lastViewportSignature == signature &&
        _viewportMinX != null &&
        _viewportMaxX != null) {
      return;
    }
    _lastViewportSignature = signature;
    _viewportMaxX = fullAxis.maxX;
    _viewportMinX = math.max(fullAxis.minX, fullAxis.maxX - viewportSpan);
  }
}

class _XAxisSpec {
  final double minX;
  final double maxX;
  final double interval;

  const _XAxisSpec({
    required this.minX,
    required this.maxX,
    required this.interval,
  });
}

/// 短区间只显示时分；跨天后再带日期，避免手机窄屏下横轴标签互相压住。
String _xAxisFormat(DateTime from, DateTime to) {
  final span = to.difference(from);
  if (span <= const Duration(hours: 24)) {
    return 'HH:mm';
  }
  return 'MM-dd\nHH:mm';
}

_XAxisSpec _buildXAxisSpec({
  required DateTime from,
  required DateTime to,
  required bool narrow,
  required double rawMinX,
  required double rawMaxX,
}) {
  final span = to.difference(from).abs();
  final tickCount = narrow ? 3 : 5;

  // 5 分钟档保持当前自由刻度，避免过密时整点对齐把标签挤没。
  if (span < const Duration(hours: 1)) {
    // 30 分钟档固定 5 分钟刻度，其它小于 1 小时的范围保留原本的自由分布。
    if (span >= const Duration(minutes: 30)) {
      final intervalMinutes = 5;
      final alignedFrom = _floorToMinuteBoundary(from, intervalMinutes);
      final alignedTo = _ceilToMinuteBoundary(to, intervalMinutes);
      final safeTo = alignedTo.isAfter(alignedFrom)
          ? alignedTo
          : alignedFrom.add(const Duration(minutes: 5));
      return _XAxisSpec(
        minX: alignedFrom.millisecondsSinceEpoch.toDouble(),
        maxX: safeTo.millisecondsSinceEpoch.toDouble(),
        interval: const Duration(minutes: 5).inMilliseconds.toDouble(),
      );
    }
    final minX = rawMinX;
    final maxX = rawMaxX;
    final xSpan = maxX - minX < 1.0 ? 60000.0 : maxX - minX;
    return _XAxisSpec(
      minX: minX,
      maxX: maxX,
      interval: xSpan / tickCount,
    );
  }

  final intervalMinutes = _preferredMinuteInterval(span);
  final alignedFrom = _floorToMinuteBoundary(from, intervalMinutes);
  final alignedTo = _ceilToMinuteBoundary(to, intervalMinutes);
  final safeTo = alignedTo.isAfter(alignedFrom)
      ? alignedTo
      : alignedFrom.add(Duration(minutes: intervalMinutes));

  return _XAxisSpec(
    minX: alignedFrom.millisecondsSinceEpoch.toDouble(),
    maxX: safeTo.millisecondsSinceEpoch.toDouble(),
    interval: Duration(minutes: intervalMinutes).inMilliseconds.toDouble(),
  );
}

double _viewportSpanForAxis({
  required _XAxisSpec xAxis,
  required bool narrow,
}) {
  final fullSpan = xAxis.maxX - xAxis.minX;
  if (fullSpan <= 0) return xAxis.interval;

  // 大部分档位默认只看右侧最新的 4~5 个刻度跨度，剩余内容通过拖动查看。
  final visibleIntervals = narrow ? 4 : 5;
  var viewportSpan = xAxis.interval * visibleIntervals;

  // 5 分钟档本身时间窗很短，缩成 80% 左右，保证也能左右拖动。
  if (viewportSpan >= fullSpan) {
    viewportSpan = fullSpan * 0.8;
  }

  final minViewportSpan = math.min(fullSpan, xAxis.interval * 1.5);
  return viewportSpan.clamp(minViewportSpan, fullSpan).toDouble();
}

int _preferredMinuteInterval(Duration span) {
  if (span <= const Duration(hours: 1)) return 10;
  if (span <= const Duration(hours: 4)) return 30;
  if (span <= const Duration(hours: 12)) return 60;
  if (span <= const Duration(hours: 24)) return 120;
  if (span <= const Duration(days: 3)) return 360;
  return 720;
}

DateTime _floorToMinuteBoundary(DateTime value, int intervalMinutes) {
  final minuteOfDay = value.hour * 60 + value.minute;
  final flooredMinuteOfDay = (minuteOfDay ~/ intervalMinutes) * intervalMinutes;
  return DateTime(
    value.year,
    value.month,
    value.day,
  ).add(Duration(minutes: flooredMinuteOfDay));
}

DateTime _ceilToMinuteBoundary(DateTime value, int intervalMinutes) {
  final floored = _floorToMinuteBoundary(value, intervalMinutes);
  if (floored.isAtSameMomentAs(value)) {
    return value;
  }
  return floored.add(Duration(minutes: intervalMinutes));
}

/// `fl_chart 0.68` 没有 `minIncluded/maxIncluded`，这里手动隐藏边界标签，避免和内部刻度重叠。
bool _isAxisEdgeLabel(double value, TitleMeta meta) {
  const epsilon = 0.000001;
  return (value - meta.min).abs() < epsilon ||
      (value - meta.max).abs() < epsilon;
}
