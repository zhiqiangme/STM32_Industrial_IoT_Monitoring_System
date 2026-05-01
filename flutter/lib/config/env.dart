/// Flowmeter 应用的运行期配置。
///
/// 所有配置值都通过 `--dart-define` 在构建时注入，
/// 这样同一份代码可以在 dev / prod 之间切换而不必改源码。
/// 默认值面向手机实机使用：默认连接真实后端，避免登录后仍显示假数据。
class Env {
  const Env._();

  /// 是否启用 Mock 服务而不是真实的 HTTP/WS。
  ///
  /// 只有离线演示或后端不可用时，才传 `--dart-define=USE_MOCK=true`。
  static const bool useMock = bool.fromEnvironment(
    'USE_MOCK',
    defaultValue: false,
  );

  /// REST 接口的基础地址。最终 URL 形如 `$apiBase/api/...`。
  /// STM32_Mill 项目独立部署在 mill-api.varka.cn，与 Flowmeter 的
  /// api.varka.cn 物理隔离（不同后端、不同 DB、不同 JWT 密钥）。
  static const String apiBase = String.fromEnvironment(
    'API_BASE',
    defaultValue: 'https://mill-api.varka.cn',
  );

  /// 实时通道（遥测 / ack）的 WebSocket 地址。
  static const String wsUrl = String.fromEnvironment(
    'WS_URL',
    defaultValue: 'wss://mill-api.varka.cn/ws/live',
  );

  /// 单设备部署下使用的固定设备 ID。
  static const String deviceId = String.fromEnvironment(
    'DEVICE_ID',
    defaultValue: 'FM002',
  );

  /// 下行命令的 ack 超时阈值。设备在该窗口内未回执则 UI 显示失败。
  static const Duration commandAckTimeout = Duration(seconds: 10);

  /// 遥测数据离线阈值。超过该时长没有新数据，
  /// UI 仍保留最后一次显示但把设备标记为离线。
  static const Duration telemetryOfflineTimeout = Duration(
    seconds: int.fromEnvironment('TELEMETRY_OFFLINE_SECONDS', defaultValue: 45),
  );

  /// WebSocket 重连退避的最大间隔。
  static const Duration wsReconnectMaxBackoff = Duration(seconds: 30);
}
