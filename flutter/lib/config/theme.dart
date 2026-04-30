import 'package:flutter/material.dart';

/// 全局主题配置。
///
/// 用单一种子色驱动 Material 3 调色板，亮色 / 暗色两套主题
/// 都从同一个 seed 派生，保证整体品牌色一致。
class AppTheme {
  const AppTheme._();

  /// 工业蓝。Flowmeter 的主品牌色。
  static const Color _seed = Color(0xFF1976D2);

  /// 亮色主题。
  static ThemeData light() {
    return ThemeData(
      colorScheme: ColorScheme.fromSeed(
        seedColor: _seed,
        brightness: Brightness.light,
      ),
      useMaterial3: true,
      // AppBar 标题靠左，符合工业类应用阅读习惯。
      appBarTheme: const AppBarTheme(centerTitle: false),
    );
  }

  /// 暗色主题（系统跟随时使用）。
  static ThemeData dark() {
    return ThemeData(
      colorScheme: ColorScheme.fromSeed(
        seedColor: _seed,
        brightness: Brightness.dark,
      ),
      useMaterial3: true,
      appBarTheme: const AppBarTheme(centerTitle: false),
    );
  }
}
