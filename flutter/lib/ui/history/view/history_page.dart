import 'dart:math' as math;

import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import 'package:provider/provider.dart';

import '../../../data/models/history_point.dart';
import '../view_model/history_view_model.dart';

/// X 轴分度值预设：一个大格代表多少分钟，以及切到该档时一屏显示几个大格。
/// 可见格数随分度值单调递增；24h 档目标 8 d 会被钳到 fullSpan，铺满整段 7 天。
const _intervalPresets = <({int minutes, int visibleIntervals, String label})>[
  (minutes: 5,    visibleIntervals: 6,  label: '5min'),
  (minutes: 10,   visibleIntervals: 6,  label: '10min'),
  (minutes: 30,   visibleIntervals: 8,  label: '30min'),
  (minutes: 60,   visibleIntervals: 8,  label: '1h'),
  (minutes: 360,  visibleIntervals: 8,  label: '6h'),
  (minutes: 1440, visibleIntervals: 8,  label: '24h'),
];

int _visibleIntervalsFor(int intervalMinutes) {
  for (final preset in _intervalPresets) {
    if (preset.minutes == intervalMinutes) return preset.visibleIntervals;
  }
  return 6;
}

const double _chartDragSensitivity = 2.5;

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

/// 顶部控制条：字段下拉 + X 轴分度值单选。
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
          final dropdown = DropdownButtonFormField<HistoryView>(
            initialValue: vm.view,
            decoration: const InputDecoration(
              labelText: '字段',
              isDense: true,
              border: OutlineInputBorder(),
            ),
            items: HistoryView.values
                .map((v) => DropdownMenuItem(value: v, child: Text(v.label)))
                .toList(),
            onChanged: (v) {
              if (v != null) vm.setView(v);
            },
          );
          final segmented = SegmentedButton<int>(
            segments: [
              for (final preset in _intervalPresets)
                ButtonSegment(
                  value: preset.minutes,
                  label: Text(preset.label),
                ),
            ],
            selected: {vm.intervalMinutes},
            showSelectedIcon: false,
            style: SegmentedButton.styleFrom(
              padding: const EdgeInsets.symmetric(horizontal: 7),
              visualDensity: const VisualDensity(
                horizontal: -3,
                vertical: -2,
              ),
              shape: const RoundedRectangleBorder(),
            ),
            onSelectionChanged: (s) {
              vm.setIntervalMinutes(s.first);
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
}

/// 折线图主体（fl_chart）。
///
/// 单字段视图：渲染一张 [LineChart]，沿用 fl_chart 内置的拖拽逻辑。
/// 温度组（T1~T4）：上下叠 4 张共用时间轴、各自独立 Y 轴的小图，
/// Y 轴分度值在 4 张之间统一。
class _Chart extends StatefulWidget {
  const _Chart({required this.vm});
  final HistoryViewModel vm;

  @override
  State<_Chart> createState() => _ChartState();
}

class _ChartState extends State<_Chart> {
  String? _lastViewportSignature;
  String? _lastRangeSignature;
  double? _viewportMinX;
  double? _viewportMaxX;

  @override
  Widget build(BuildContext context) {
    final vm = widget.vm;

    // 三种空 / 错状态分支：loading / error / 空数据。
    if (vm.loading && vm.isEmpty) {
      return const Center(child: CircularProgressIndicator());
    }
    if (vm.error != null) {
      return Center(child: Text('加载失败：${vm.error}'));
    }
    if (vm.isEmpty) {
      return const Center(child: Text('此时间段内无数据'));
    }

    final fields = vm.fields;
    final series = <_FieldSeries>[];
    for (final f in fields) {
      final s = _FieldSeries.from(f, vm.pointsOf(f));
      if (s != null) series.add(s);
    }
    if (series.isEmpty) {
      return const Center(child: Text('此时间段内无数据'));
    }

    final rawMinX = series
        .map((s) => s.rawMinX)
        .reduce((a, b) => a < b ? a : b);
    final rawMaxX = series
        .map((s) => s.rawMaxX)
        .reduce((a, b) => a > b ? a : b);

    return Padding(
      padding: const EdgeInsets.fromLTRB(0, 4, 4, 8),
      child: LayoutBuilder(
        builder: (context, constraints) {
          // 外框尺寸固定，图内时间窗通过拖动平移；这里只按视口宽度调节字号和留白。
          final narrow = constraints.maxWidth < 480;
          final xFontSize = narrow ? 9.0 : 10.0;
          final xReserved = narrow ? 28.0 : 32.0;
          final yFontSize = narrow ? 9.0 : 11.0;
          final xLabelFormat = _xAxisFormat(vm.from, vm.to);

          final xAxis = _buildXAxisSpec(
            from: vm.from,
            to: vm.to,
            intervalMinutes: vm.intervalMinutes,
          );
          final viewportSpan = _viewportSpanForAxis(
            xAxis: xAxis,
            visibleIntervals: _visibleIntervalsFor(vm.intervalMinutes),
          );
          // rangeSignature 仅含时间范围；fullSignature 还包含分度值与字段集合。
          // 切换分度值时以视口中心为锚点缩放；时间范围变化时重置到最右端。
          final rangeSignature =
              '${vm.from.millisecondsSinceEpoch}:${vm.to.millisecondsSinceEpoch}';
          _ensureViewport(
            rangeSignature: rangeSignature,
            fullSignature:
                '$rangeSignature:${vm.intervalMinutes}:${vm.view.name}',
            fullAxis: xAxis,
            viewportSpan: viewportSpan,
          );
          final visibleMinX = _viewportMinX ?? xAxis.minX;
          final visibleMaxX = _viewportMaxX ?? xAxis.maxX;

          // 多字段（温度组）下，Y 分度值统一为各通道需求的最大值。
          double? unifiedYInterval;
          if (vm.view.isMulti) {
            for (final s in series) {
              final candidate = _yAxisInterval(
                field: s.field,
                minY: s.chartMinY,
                maxY: s.chartMaxY,
              );
              if (candidate == null) continue;
              if (unifiedYInterval == null || candidate > unifiedYInterval) {
                unifiedYInterval = candidate;
              }
            }
          }

          final yReserved = _yReservedSize(series.first.field, narrow);

          return GestureDetector(
            behavior: HitTestBehavior.opaque,
            onHorizontalDragUpdate: (details) {
              final plotWidth =
                  math.max(1.0, constraints.maxWidth - yReserved - 24.0);
              final visibleSpan = visibleMaxX - visibleMinX;
              final shift = -details.delta.dx *
                  (visibleSpan / plotWidth) *
                  _chartDragSensitivity;
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
            child: vm.view.isMulti
                ? _buildMultiChannel(
                    context: context,
                    series: series,
                    xAxis: xAxis,
                    visibleMinX: visibleMinX,
                    visibleMaxX: visibleMaxX,
                    rawMinX: rawMinX,
                    rawMaxX: rawMaxX,
                    yInterval: unifiedYInterval,
                    narrow: narrow,
                    xFontSize: xFontSize,
                    xReserved: xReserved,
                    yFontSize: yFontSize,
                    xLabelFormat: xLabelFormat,
                  )
                : _buildSingle(
                    context: context,
                    s: series.first,
                    xAxis: xAxis,
                    visibleMinX: visibleMinX,
                    visibleMaxX: visibleMaxX,
                    rawMinX: rawMinX,
                    rawMaxX: rawMaxX,
                    narrow: narrow,
                    xFontSize: xFontSize,
                    xReserved: xReserved,
                    yFontSize: yFontSize,
                    xLabelFormat: xLabelFormat,
                  ),
          );
        },
      ),
    );
  }

  Widget _buildSingle({
    required BuildContext context,
    required _FieldSeries s,
    required _XAxisSpec xAxis,
    required double visibleMinX,
    required double visibleMaxX,
    required double rawMinX,
    required double rawMaxX,
    required bool narrow,
    required double xFontSize,
    required double xReserved,
    required double yFontSize,
    required String xLabelFormat,
  }) {
    final yInterval = _yAxisInterval(
      field: s.field,
      minY: s.chartMinY,
      maxY: s.chartMaxY,
    );
    return Stack(
      children: [
        _LineChartSection(
          series: s,
          xAxis: xAxis,
          visibleMinX: visibleMinX,
          visibleMaxX: visibleMaxX,
          yInterval: yInterval,
          showBottomTitles: true,
          narrow: narrow,
          xFontSize: xFontSize,
          xReserved: xReserved,
          yFontSize: yFontSize,
          xLabelFormat: xLabelFormat,
        ),
        Positioned(
          left: 2,
          top: 4,
          child: Text(
            s.field.unit,
            style: TextStyle(
              fontSize: narrow ? 11.0 : 12.0,
              fontWeight: FontWeight.w500,
            ),
          ),
        ),
        if (s.hasSinglePoint)
          const Positioned(
            top: 8,
            right: 8,
            child: _SinglePointBadge(),
          ),
      ],
    );
  }

  Widget _buildMultiChannel({
    required BuildContext context,
    required List<_FieldSeries> series,
    required _XAxisSpec xAxis,
    required double visibleMinX,
    required double visibleMaxX,
    required double rawMinX,
    required double rawMaxX,
    required double? yInterval,
    required bool narrow,
    required double xFontSize,
    required double xReserved,
    required double yFontSize,
    required String xLabelFormat,
  }) {
    return Column(
      children: [
        for (var i = 0; i < series.length; i++) ...[
          Expanded(
            child: Stack(
              children: [
                _LineChartSection(
                  series: series[i],
                  xAxis: xAxis,
                  visibleMinX: visibleMinX,
                  visibleMaxX: visibleMaxX,
                  yInterval: yInterval,
                  // 仅最后一张小图显示 X 轴时间标签，避免上下重复占用空间。
                  showBottomTitles: i == series.length - 1,
                  narrow: narrow,
                  xFontSize: xFontSize,
                  xReserved: xReserved,
                  yFontSize: yFontSize,
                  xLabelFormat: xLabelFormat,
                ),
                Positioned(
                  left: 2,
                  top: 2,
                  child: Text(
                    _channelTag(series[i].field),
                    style: TextStyle(
                      fontSize: narrow ? 10.0 : 11.0,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ),
                if (i == 0)
                  Positioned(
                    right: 2,
                    top: 2,
                    child: Text(
                      series[i].field.unit,
                      style: TextStyle(
                        fontSize: narrow ? 10.0 : 11.0,
                        fontWeight: FontWeight.w500,
                      ),
                    ),
                  ),
                if (series[i].hasSinglePoint)
                  const Positioned(
                    top: 4,
                    right: 24,
                    child: _SinglePointBadge(),
                  ),
              ],
            ),
          ),
          if (i != series.length - 1) const SizedBox(height: 4),
        ],
      ],
    );
  }

  void _ensureViewport({
    required String rangeSignature,
    required String fullSignature,
    required _XAxisSpec fullAxis,
    required double viewportSpan,
  }) {
    // 完全未变（含拖动后的状态）→ 保持当前视口。
    if (_lastViewportSignature == fullSignature &&
        _viewportMinX != null &&
        _viewportMaxX != null) {
      return;
    }
    _lastViewportSignature = fullSignature;

    final rangeChanged = _lastRangeSignature != rangeSignature;
    _lastRangeSignature = rangeSignature;

    if (rangeChanged || _viewportMinX == null || _viewportMaxX == null) {
      // 时间范围变化或首次加载 → 重置到最右端。
      _viewportMaxX = fullAxis.maxX;
      _viewportMinX = math.max(fullAxis.minX, fullAxis.maxX - viewportSpan);
    } else {
      // 仅分度值变化 → 以当前视口中心为锚点缩放。
      final center = (_viewportMinX! + _viewportMaxX!) / 2;
      final halfSpan = viewportSpan / 2;
      _viewportMinX =
          (center - halfSpan).clamp(fullAxis.minX, fullAxis.maxX - viewportSpan);
      _viewportMaxX = _viewportMinX! + viewportSpan;
    }
  }
}

/// 单张折线图渲染单元，被单字段视图与温度组小图共用。
class _LineChartSection extends StatelessWidget {
  const _LineChartSection({
    required this.series,
    required this.xAxis,
    required this.visibleMinX,
    required this.visibleMaxX,
    required this.yInterval,
    required this.showBottomTitles,
    required this.narrow,
    required this.xFontSize,
    required this.xReserved,
    required this.yFontSize,
    required this.xLabelFormat,
  });

  final _FieldSeries series;
  final _XAxisSpec xAxis;
  final double visibleMinX;
  final double visibleMaxX;
  final double? yInterval;
  final bool showBottomTitles;
  final bool narrow;
  final double xFontSize;
  final double xReserved;
  final double yFontSize;
  final String xLabelFormat;

  @override
  Widget build(BuildContext context) {
    final yReserved = _yReservedSize(series.field, narrow);
    return LineChart(
      LineChartData(
        minX: visibleMinX,
        maxX: visibleMaxX,
        clipData: const FlClipData.all(),
        // y 轴上下各留 10% 余量，避免曲线贴边。
        minY: series.chartMinY,
        maxY: series.chartMaxY,
        titlesData: FlTitlesData(
          rightTitles: const AxisTitles(
            sideTitles: SideTitles(showTitles: false),
          ),
          topTitles: const AxisTitles(
            sideTitles: SideTitles(showTitles: false),
          ),
          bottomTitles: AxisTitles(
            sideTitles: showBottomTitles
                ? SideTitles(
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
                  )
                : const SideTitles(showTitles: false),
          ),
          leftTitles: AxisTitles(
            sideTitles: SideTitles(
              showTitles: true,
              reservedSize: yReserved,
              interval: yInterval,
              getTitlesWidget: (value, meta) {
                if (_isAxisEdgeLabel(value, meta)) {
                  return const SizedBox.shrink();
                }
                return Padding(
                  padding: const EdgeInsets.only(right: 2),
                  child: Text(
                    _formatYAxisValue(series.field, value, meta),
                    style: TextStyle(fontSize: yFontSize),
                  ),
                );
              },
            ),
          ),
        ),
        gridData: FlGridData(
          show: true,
          drawHorizontalLine: true,
          drawVerticalLine: true,
          verticalInterval: xAxis.interval,
          horizontalInterval: yInterval,
        ),
        borderData: FlBorderData(show: true),
        // 历史页手势只保留左右拖动，不再点按弹出当前时刻数据提示。
        lineTouchData: const LineTouchData(enabled: false),
        lineBarsData: [
          LineChartBarData(
            spots: series.spots,
            isCurved: true,
            barWidth: 2,
            color: Theme.of(context).colorScheme.primary,
            // 只有一个点时显式画圆点，否则折线会看起来像空白图。
            dotData: FlDotData(
              show: series.hasSinglePoint,
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
    );
  }
}

class _SinglePointBadge extends StatelessWidget {
  const _SinglePointBadge();

  @override
  Widget build(BuildContext context) {
    return const DecoratedBox(
      decoration: BoxDecoration(
        color: Color(0xCCFFFFFF),
        borderRadius: BorderRadius.all(Radius.circular(8)),
      ),
      child: Padding(
        padding: EdgeInsets.symmetric(horizontal: 8, vertical: 4),
        child: Text(
          '当前时间段仅 1 个点',
          style: TextStyle(fontSize: 12),
        ),
      ),
    );
  }
}

/// 单个字段已转好的曲线数据 + Y 范围。
class _FieldSeries {
  _FieldSeries._({
    required this.field,
    required this.spots,
    required this.hasSinglePoint,
    required this.rawMinX,
    required this.rawMaxX,
    required this.chartMinY,
    required this.chartMaxY,
  });

  final HistoryField field;
  final List<FlSpot> spots;
  final bool hasSinglePoint;
  final double rawMinX;
  final double rawMaxX;
  final double chartMinY;
  final double chartMaxY;

  static _FieldSeries? from(HistoryField field, List<HistoryPoint> points) {
    if (points.isEmpty) return null;

    // 把 HistoryPoint 转成 fl_chart 的 FlSpot：x 用毫秒时间戳。
    final rawSpots = points
        .map(
          (p) => FlSpot(p.timestamp.millisecondsSinceEpoch.toDouble(), p.value),
        )
        .toList();
    final hasSinglePoint = rawSpots.length == 1;
    final spots = <FlSpot>[];
    for (var i = 0; i < rawSpots.length; i++) {
      spots.add(rawSpots[i]);
      if (i == rawSpots.length - 1) continue;
      final current = points[i].timestamp;
      final next = points[i + 1].timestamp;
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

    return _FieldSeries._(
      field: field,
      spots: spots,
      hasSinglePoint: hasSinglePoint,
      rawMinX: rawMinX,
      rawMaxX: rawMaxX,
      chartMinY: minY - ySpan * 0.1,
      chartMaxY: maxY + ySpan * 0.1,
    );
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
  required int intervalMinutes,
}) {
  final intervalMs = Duration(minutes: intervalMinutes).inMilliseconds.toDouble();
  final alignedFrom = _floorToMinuteBoundary(from, intervalMinutes);
  final alignedTo = _ceilToMinuteBoundary(to, intervalMinutes);
  final safeTo = alignedTo.isAfter(alignedFrom)
      ? alignedTo
      : alignedFrom.add(Duration(minutes: intervalMinutes));

  return _XAxisSpec(
    minX: alignedFrom.millisecondsSinceEpoch.toDouble(),
    maxX: safeTo.millisecondsSinceEpoch.toDouble(),
    interval: intervalMs,
  );
}

double _viewportSpanForAxis({
  required _XAxisSpec xAxis,
  required int visibleIntervals,
}) {
  final fullSpan = xAxis.maxX - xAxis.minX;
  if (fullSpan <= 0) return xAxis.interval;

  final desired = xAxis.interval * visibleIntervals;
  // 目标跨度 ≥ 全部数据 → 铺满，整段一屏可见，拖动自然失效。
  if (desired >= fullSpan) return fullSpan;
  // 至少留 1.5 个分度，避免极端情况下视口塌缩到看不清。
  final minSpan = math.min(fullSpan, xAxis.interval * 1.5);
  return desired.clamp(minSpan, fullSpan).toDouble();
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

String _formatYAxisValue(HistoryField field, double value, TitleMeta meta) {
  switch (field) {
    case HistoryField.t1:
    case HistoryField.t2:
    case HistoryField.t3:
    case HistoryField.t4:
      return value.toStringAsFixed(1);
    case HistoryField.relayDo:
    case HistoryField.relayDi:
      return '0x${value.toInt().toRadixString(16).padLeft(4, '0').toUpperCase()}';
    case HistoryField.flow:
    case HistoryField.total:
    case HistoryField.weight:
      return meta.formattedValue;
  }
}

double _yReservedSize(HistoryField field, bool narrow) {
  if (_isTemperatureField(field)) {
    return narrow ? 26.0 : 24.0;
  }
  // relayDo/relayDi 十六进制显示（如 0x0003）需要更多宽度。
  if (field == HistoryField.relayDo || field == HistoryField.relayDi) {
    return narrow ? 40.0 : 38.0;
  }
  return narrow ? 34.0 : 32.0;
}

double? _yAxisInterval({
  required HistoryField field,
  required double minY,
  required double maxY,
}) {
  final span = (maxY - minY).abs();
  if (span <= 0) return null;

  if (_isTemperatureField(field)) {
    const candidates = <double>[0.1, 0.2, 0.5, 1.0, 2.0, 5.0];
    final target = span / 6.0;
    for (final candidate in candidates) {
      if (candidate >= target) return candidate;
    }
    return candidates.last;
  }

  return null;
}

bool _isTemperatureField(HistoryField field) {
  switch (field) {
    case HistoryField.t1:
    case HistoryField.t2:
    case HistoryField.t3:
    case HistoryField.t4:
      return true;
    case HistoryField.flow:
    case HistoryField.total:
    case HistoryField.weight:
    case HistoryField.relayDo:
    case HistoryField.relayDi:
      return false;
  }
}

String _channelTag(HistoryField field) {
  switch (field) {
    case HistoryField.t1:
      return 'T1';
    case HistoryField.t2:
      return 'T2';
    case HistoryField.t3:
      return 'T3';
    case HistoryField.t4:
      return 'T4';
    case HistoryField.flow:
    case HistoryField.total:
    case HistoryField.weight:
    case HistoryField.relayDo:
    case HistoryField.relayDi:
      return field.label;
  }
}

/// `fl_chart 0.68` 没有 `minIncluded/maxIncluded`，这里手动隐藏边界标签，避免和内部刻度重叠。
bool _isAxisEdgeLabel(double value, TitleMeta meta) {
  const epsilon = 0.000001;
  return (value - meta.min).abs() < epsilon ||
      (value - meta.max).abs() < epsilon;
}
