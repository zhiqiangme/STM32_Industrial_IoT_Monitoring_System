# Flowmeter Flutter 手机端

默认连接 `https://api.varka.cn` 和 `wss://api.varka.cn/ws/live`。
登录成功后，仪表盘、历史和告警页面会使用真实接口数据。

## 手机运行

```powershell
D:\Portable\Path\flutter\bin\flutter.bat run `
  --dart-define=API_BASE=https://api.varka.cn `
  --dart-define=WS_URL=wss://api.varka.cn/ws/live `
  --dart-define=DEVICE_ID=FM001
```

## 构建 APK

```powershell
D:\Portable\Path\flutter\bin\flutter.bat build apk --release `
  --dart-define=API_BASE=https://api.varka.cn `
  --dart-define=WS_URL=wss://api.varka.cn/ws/live `
  --dart-define=DEVICE_ID=FM001
```

## Mock 演示模式

后端不可用或只想看界面时，显式打开 Mock：

```powershell
D:\Portable\Path\flutter\bin\flutter.bat run --dart-define=USE_MOCK=true
```
