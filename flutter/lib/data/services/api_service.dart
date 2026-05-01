import '../models/alarm.dart';
import '../models/command.dart';
import '../models/device_status.dart';
import '../models/history_point.dart';
import '../models/measurement.dart';

/// 所有 REST 调用的契约接口。
///
/// 仓库层只依赖这个抽象，不依赖 Dio 之类的具体实现，
/// 这样可以在前期开发或测试时无缝替换为 Mock 实现。
abstract interface class ApiService {
  /// `POST /api/auth/login` —— 真实实现走服务端鉴权返回 JWT；
  /// Mock 实现接受任意非空账号密码并返回伪 token，便于离线演示。
  Future<String> login({required String username, required String password});

  /// 设置 / 清除请求头里的 Bearer token。
  /// `AuthRepository` 在登录、恢复会话、退出时调用；
  /// Mock 实现可以直接当作 no-op。
  void setAuthToken(String? token);

  /// `GET /api/latest?dev=…` —— 拉取最近一帧遥测样本。
  Future<Measurement> getLatest();

  /// `GET /api/history?dev=&limit=&from=&to=` —— 历史曲线数据。
  Future<List<HistoryPoint>> getHistory({
    required HistoryField field,
    required DateTime from,
    required DateTime to,
    int limit,
  });

  /// `GET /api/alarms?dev=&limit=&from=&to=` —— 历史告警列表。
  Future<List<Alarm>> getAlarms({
    required DateTime from,
    required DateTime to,
    int limit,
  });

  /// `GET /api/status` —— 链路 / 服务健康状态。
  Future<DeviceStatus> getStatus();

  /// `POST /api/commands/relay-set` —— 下发继电器目标位图。
  Future<Command> sendRelaySet({required int mask});
}
