# Flutter 脚本说明

这些脚本位于 `D:\Project\Flowmeter\flutter`，面向 Windows PowerShell / CMD 使用。默认连接真实后端：

- REST：`https://api.varka.cn`
- WebSocket：`wss://api.varka.cn/ws/live`
- 设备：`FM001`

## 脚本列表

| 脚本 | 作用 |
| --- | --- |
| `Flutter_Env.bat` | 公共配置脚本，其他脚本会自动引用；一般不需要单独运行。 |
| `Flutter_PubGet.bat` | 执行 `flutter pub get`，用于拉取依赖。 |
| `Flutter_Analyze.bat` | 执行 `flutter analyze`，用于静态检查。 |
| `Flutter_Run_Phone.bat` | 连接手机直接运行 App，适合调试。 |
| `Flutter_Build_Debug.bat` | 构建调试 APK：`build\app\outputs\flutter-apk\app-debug.apk`。 |
| `Flutter_Create_Release_Keystore.bat` | 生成本地 release keystore；只生成本地文件，不提交到 Git。 |
| `Flutter_Build_Release.bat` | 构建正式签名发布 APK：`build\app\outputs\flutter-apk\app-release.apk`。 |
| `Flutter_Install_Release.bat` | 覆盖安装 release APK 到当前连接的 Android 手机；不卸载 App，因此会保留已登录 token。 |
| `Flutter_Reinstall_Clean.bat` | 清空重装 release APK，会先卸载 App；只用于重置测试，登录状态会丢失。 |
| `Flutter_Clean.bat` | 清理 Flutter 构建缓存；这是项目原有脚本。 |

## 常用顺序

首次构建：

```powershell
.\Flutter_PubGet.bat
.\Flutter_Create_Release_Keystore.bat
.\Flutter_Build_Release.bat
.\Flutter_Install_Release.bat
```

首次生成 keystore 后，把 `android\key.properties.example` 复制为 `android\key.properties`，填写真实密码。`android\.gitignore` 已忽略 `key.properties`、`*.jks`、`*.keystore`，不要把这些文件提交到 Git。

开发调试：

```powershell
.\Flutter_Run_Phone.bat
```

保留登录状态更新 App：

```powershell
.\Flutter_Build_Release.bat
.\Flutter_Install_Release.bat
```

需要清空本地数据时才使用：

```powershell
.\Flutter_Reinstall_Clean.bat
```

发布前检查：

```powershell
.\Flutter_Analyze.bat
.\Flutter_Build_Release.bat
```

## 环境要求

- Flutter 优先使用 `D:\Portable\Path\flutter\bin\flutter.bat`。
- 如果该路径不存在，脚本会尝试使用 PATH 里的 `flutter`。
- 脚本会优先使用 Android Studio 自带 JBR，避免系统 Java 8 导致 Gradle 构建失败。
- 安装脚本需要 `adb`，会依次查找 PATH、`ANDROID_HOME\platform-tools`、`%LOCALAPPDATA%\Android\Sdk\platform-tools`。

## 登录保留说明

App 的登录 token 存在 Android 应用私有安全存储里。使用 `Flutter_Install_Release.bat` 的覆盖安装会保留这部分数据；如果先卸载 App、点系统“清除数据”，或使用 `Flutter_Reinstall_Clean.bat`，Android 会删除本地 token，需要重新登录。
