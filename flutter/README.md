# STM32_Mill Flutter 手机端

默认连接 `https://mill-api.varka.cn` 和 `wss://mill-api.varka.cn/ws/live`。
登录成功后，仪表盘、历史和告警页面会使用真实接口数据。

## 手机运行

```powershell
D:\Portable\Path\flutter\bin\flutter.bat run `
  --dart-define=API_BASE=https://mill-api.varka.cn `
  --dart-define=WS_URL=wss://mill-api.varka.cn/ws/live `
  --dart-define=DEVICE_ID=FM002
```

## 构建 APK

```powershell
D:\Portable\Path\flutter\bin\flutter.bat build apk --release `
  --dart-define=API_BASE=https://mill-api.varka.cn `
  --dart-define=WS_URL=wss://mill-api.varka.cn/ws/live `
  --dart-define=DEVICE_ID=FM002
```

## 登录保持说明

- 登录 token 保存在 `flutter_secure_storage` 的 `auth_token` 中。
- 只有“同一 `applicationId` + 同一签名”的覆盖安装才会保留登录态。
- 同签名 `debug` 覆盖 `debug`、同签名 `release` 覆盖 `release`，登录通常会保留。
- 如果 `flutter run` 安装的是 debug 包，而手机上原来是 release 包，或反过来混装不同签名包，Android 可能先卸载旧包再安装，新包会丢失原来的登录数据。
- 需要验证登录保持时，不要混用 `flutter run` 和正式 APK 覆盖安装；调试包只覆盖调试包，正式包只覆盖同签名正式包。

## Mock 演示模式

后端不可用或只想看界面时，显式打开 Mock：

```powershell
D:\Portable\Path\flutter\bin\flutter.bat run --dart-define=USE_MOCK=true
```
