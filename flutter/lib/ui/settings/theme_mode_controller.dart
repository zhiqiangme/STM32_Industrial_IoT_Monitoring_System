import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// 应用主题偏好。
///
/// 只保存用户的外观选择，不与后端同步。
enum AppThemePreference {
  system('system', '跟随系统'),
  light('light', '日间模式'),
  dark('dark', '夜间模式');

  final String storageValue;
  final String label;

  const AppThemePreference(this.storageValue, this.label);

  ThemeMode get themeMode => switch (this) {
    AppThemePreference.system => ThemeMode.system,
    AppThemePreference.light => ThemeMode.light,
    AppThemePreference.dark => ThemeMode.dark,
  };

  static AppThemePreference fromStorage(String? value) {
    for (final item in values) {
      if (item.storageValue == value) return item;
    }
    return AppThemePreference.system;
  }
}

/// 全局主题模式控制器。
///
/// MaterialApp 读取这里的 [themeMode]，设置页修改后立即生效。
class ThemeModeController extends ChangeNotifier {
  ThemeModeController({required SharedPreferences preferences})
      : _preferences = preferences;

  static const _kThemePreference = 'theme_preference';

  final SharedPreferences _preferences;
  AppThemePreference _preference = AppThemePreference.system;

  AppThemePreference get preference => _preference;
  ThemeMode get themeMode => _preference.themeMode;

  /// 启动时从本地恢复用户上次选择，默认跟随系统。
  void load() {
    _preference = AppThemePreference.fromStorage(
      _preferences.getString(_kThemePreference),
    );
  }

  /// 更新主题偏好并落盘。
  Future<void> setPreference(AppThemePreference preference) async {
    if (_preference == preference) return;
    _preference = preference;
    await _preferences.setString(_kThemePreference, preference.storageValue);
    notifyListeners();
  }
}
