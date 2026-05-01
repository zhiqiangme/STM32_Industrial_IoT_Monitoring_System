import 'package:dio/dio.dart';

import '../../config/env.dart';
import '../models/alarm.dart';
import '../models/command.dart';
import '../models/device_status.dart';
import '../models/history_point.dart';
import '../models/measurement.dart';
import 'api_service.dart';

/// 真实环境下的 [ApiService]，底层使用 Dio。
///
/// 与 `D:\Project\HTML\server\services\pipe-monitor-api` 中
/// `pipe-monitor-api` 服务的接口字段对齐：
/// - `POST /api/auth/login`     → `{token, expiresIn, user}`
/// - `/api/latest?dev=…`        → `{device, topic, payload, receivedAt}`
/// - `/api/history?dev=…`       → `{data: [row, …]}`
/// - `/api/alarms?dev=…`        → `{data: [row, …]}`
/// - `/api/status`              → `{mqttConnected, wsClients, lastMessageAt, …}`
///
/// 除 `/api/auth/login` 外的接口都需要 `Authorization: Bearer <token>`。
/// [setAuthToken] 由 `AuthRepository` 在登录 / 恢复会话 / 退出时调用，
/// 拦截器据此把 token 注入到每一次请求里。
class HttpApiService implements ApiService {
  HttpApiService({Dio? dio})
      : _dio = dio ?? Dio(BaseOptions(baseUrl: Env.apiBase)) {
    _dio.interceptors.add(
      InterceptorsWrapper(
        onRequest: (options, handler) {
          // 请求阶段统一注入 token；登录接口本身不依赖 token，多带也无妨。
          final token = _token;
          if (token != null && token.isNotEmpty) {
            options.headers['Authorization'] = 'Bearer $token';
          }
          handler.next(options);
        },
      ),
    );
  }

  final Dio _dio;

  /// 当前持有的 token。`null` 表示未登录或会话已失效。
  String? _token;

  /// 由 [AuthRepository] 在登录 / 恢复会话 / 退出时调用。
  /// 传 `null` 表示清除 token，后续请求将不再携带 Authorization。
  @override
  void setAuthToken(String? token) {
    _token = token;
  }

  @override
  Future<String> login({
    required String username,
    required String password,
  }) async {
    try {
      final res = await _dio.post<Map<String, dynamic>>(
        '/api/auth/login',
        data: {'username': username, 'password': password},
      );
      final body = res.data;
      final token = body == null ? null : body['token'];
      if (token is! String || token.isEmpty) {
        throw StateError('登录响应缺少 token 字段');
      }
      return token;
    } on DioException catch (e) {
      // 401：凭证错误；其他状态：保留原信息便于排障。
      if (e.response?.statusCode == 401) {
        throw Exception('用户名或密码错误');
      }
      if (e.response?.statusCode == 503) {
        throw Exception('服务端数据库未就绪，暂时无法登录');
      }
      rethrow;
    }
  }

  @override
  Future<Measurement> getLatest() async {
    final res = await _dio.get<Map<String, dynamic>>(
      '/api/latest',
      queryParameters: {'dev': Env.deviceId},
    );
    final body = res.data!;
    // 服务端用 `{device, topic, payload, receivedAt}` 结构封装；
    // 真正的设备字段在 payload 里。
    final payload = body['payload'] as Map<String, dynamic>?;
    if (payload == null) {
      throw StateError('Unexpected /api/latest shape: ${body.keys}');
    }
    return Measurement.fromJson(payload, receivedAt: body['receivedAt']);
  }

  @override
  Future<List<HistoryPoint>> getHistory({
    required HistoryField field,
    required DateTime from,
    required DateTime to,
    int limit = 200,
  }) async {
    final res = await _dio.get<Map<String, dynamic>>(
      '/api/history',
      queryParameters: {
        'dev': Env.deviceId,
        'limit': limit,
        // 后端要求 Unix 秒，不是毫秒。
        'from': _unixSec(from),
        'to': _unixSec(to),
      },
    );
    final rows = (res.data!['data'] as List).cast<Map<String, dynamic>>();
    // 服务端返回的是"最新优先"，但绘图需要时间正序。
    // 同时把 NaN 点过滤掉，避免折线断裂处出现伪标签。
    final points = rows
        .map((j) => HistoryPoint.fromJson(j, field))
        .where((p) => !p.value.isNaN)
        .toList()
      ..sort((a, b) => a.timestamp.compareTo(b.timestamp));
    return points;
  }

  @override
  Future<List<Alarm>> getAlarms({
    required DateTime from,
    required DateTime to,
    int limit = 200,
  }) async {
    final res = await _dio.get<Map<String, dynamic>>(
      '/api/alarms',
      queryParameters: {
        'dev': Env.deviceId,
        'limit': limit,
        'from': _unixSec(from),
        'to': _unixSec(to),
      },
    );
    final rows = (res.data!['data'] as List).cast<Map<String, dynamic>>();
    return rows.map(Alarm.fromJson).toList(growable: false);
  }

  /// 把本地 [DateTime] 转成 UTC Unix 秒。
  static int _unixSec(DateTime t) =>
      t.toUtc().millisecondsSinceEpoch ~/ 1000;

  @override
  Future<DeviceStatus> getStatus() async {
    final res = await _dio.get<Map<String, dynamic>>('/api/status');
    return DeviceStatus.fromStatusJson(res.data!);
  }

  @override
  Future<Command> sendRelaySet({required int mask}) async {
    final res = await _dio.post<Map<String, dynamic>>(
      '/api/commands/relay-set',
      data: {
        'dev': Env.deviceId,
        'mask': mask,
      },
    );
    return Command.fromJson(res.data!);
  }
}
