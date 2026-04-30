import 'package:flutter/material.dart';

/// 仪表盘上的"大数字"卡片。
///
/// 显示一个标签 + 已格式化的数值 + 单位，并在数据"陈旧"或缺测时
/// 用一个警告角标提示用户。
class ValueTile extends StatelessWidget {
  const ValueTile({
    super.key,
    required this.label,
    required this.value,
    required this.unit,
    this.valid = true,
  });

  /// 字段标签，例如"瞬时流量"。
  final String label;

  /// 已格式化的数值文本（包含小数位等）。
  final String value;

  /// 单位字符串，例如"L/min"。
  final String unit;

  /// 数值是否仍然有效。无效时数字变灰并出现告警图标。
  final bool valid;

  @override
  Widget build(BuildContext context) {
    final cs = Theme.of(context).colorScheme;
    return Card(
      elevation: 0,
      color: cs.surfaceContainer,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      margin: EdgeInsets.zero,
      // 用 LayoutBuilder 让卡片在窄屏 / 横向密集排布下也有合理字号。
      child: LayoutBuilder(
        builder: (context, constraints) {
          // 手机首页需要一屏展示全部指标，按卡片高度分两档压缩留白。
          final dense = constraints.maxHeight < 130;
          final veryDense = constraints.maxHeight < 102;
          final padding = veryDense ? 10.0 : (dense ? 12.0 : 16.0);
          final valueFontSize = veryDense ? 24.0 : (dense ? 28.0 : 32.0);
          final unitFontSize = veryDense ? 12.0 : 13.0;
          final labelFontSize = veryDense ? 12.0 : 13.0;

          return Padding(
            padding: EdgeInsets.all(padding),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                // 第一行：标签。
                Text(
                  label,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    fontSize: labelFontSize,
                    color: cs.onSurfaceVariant,
                  ),
                ),
                const Spacer(),
                // 第二行：大字号数值 + 单位 + 可选警告图标。
                // 用 FittedBox 在数字过长时整体等比缩小，避免溢出。
                FittedBox(
                  fit: BoxFit.scaleDown,
                  alignment: Alignment.bottomLeft,
                  child: Row(
                    mainAxisSize: MainAxisSize.min,
                    crossAxisAlignment: CrossAxisAlignment.baseline,
                    textBaseline: TextBaseline.alphabetic,
                    children: [
                      Text(
                        value,
                        style: TextStyle(
                          fontSize: valueFontSize,
                          fontWeight: FontWeight.w600,
                          // 失效时把数字变灰，告诉用户"这个数不可信"。
                          color: valid ? cs.onSurface : cs.outline,
                        ),
                      ),
                      const SizedBox(width: 4),
                      Text(
                        unit,
                        style: TextStyle(
                          fontSize: unitFontSize,
                          color: cs.onSurfaceVariant,
                        ),
                      ),
                      if (!valid) ...[
                        const SizedBox(width: 8),
                        Icon(
                          Icons.warning_amber_rounded,
                          size: 16,
                          color: cs.error,
                        ),
                      ],
                    ],
                  ),
                ),
              ],
            ),
          );
        },
      ),
    );
  }
}
