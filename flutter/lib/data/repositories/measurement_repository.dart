import 'dart:async';

import '../../utils/app_logger.dart';
import '../../utils/result.dart';
import '../../config/env.dart';
import '../models/alarm.dart';
import '../models/device_status.dart';
import '../models/history_point.dart';
import '../models/measurement.dart';
import '../services/api_service.dart';
import '../services/history_cache_service.dart';
import '../services/realtime_service.dart';

/// 测量数据 + 设备状态的仓库层。
///
/// - [liveStream]：实时遥测数据广播流（broadcast）。
/// - [statusStream]：设备在线 / 最近一次见到的时间。
///   在"设备通常处于离线"的部署里，这是一类一等公民状态，**不是错误**。
/// - 历史数据优先走普通 REST，同时合并本地缓存，支持离线回看。
class MeasurementRepository {
  MeasurementRepository({
    required ApiService api,
    required HistoryCacheService historyCache,
    required RealtimeService realtime,
  })  : _api = api,
        _historyCache = historyCache,
        _realtime = realtime {
    // 一进来就订阅实时事件流，把感兴趣的事件分发到本地状态。
    _sub = _realtime.events.listen(_onEvent);
  }

  final ApiService _api;
  final HistoryCacheService _historyCache;
  final RealtimeService _realtime;
  StreamSubscription<RealtimeEvent>? _sub;

  // 两个 broadcast controller：允许多个 ViewModel 同时监听。
  final _liveCtrl = StreamController<Measurement>.broadcast();
  final _statusCtrl = StreamController<DeviceStatus>.broadcast();

  // 缓存的最近一次状态，便于新订阅者快速拿到当前值。
  Measurement? _lastMeasurement;
  DeviceStatus _status = DeviceStatus.unknown();

  /// 最近一次收到遥测帧的"本地接收时间"（不是设备时间戳）。
  /// 用于计算超过 [Env.telemetryOfflineTimeout] 后是否应判定为离线。
  DateTime? _lastTelemetryReceivedAt;

  /// 离线判定的延时定时器。每次收到新帧后会重置。
  Timer? _offlineTimer;

  /// 设备状态变化时触发的回调，用于生成客户端告警。
  /// 由 [AlarmRepository] 注入。
  void Function(Alarm alarm)? onStatusAlarm;

  Stream<Measurement> get liveStream => _liveCtrl.stream;
  Stream<DeviceStatus> get statusStream => _statusCtrl.stream;

  Measurement? get lastMeasurement => _lastMeasurement;
  DeviceStatus get status => _status;

  /// 实时事件分发：本仓库只关心遥测帧和状态帧，
  /// 告警与 ack 由 [AlarmRepository]、[CommandRepository] 各自处理。
  void _onEvent(RealtimeEvent evt) {
    switch (evt) {
      case TelemetryEvent(:final measurement):
        _lastMeasurement = measurement;
        _liveCtrl.add(measurement);
        _markTelemetryOnline(measurement);
        unawaited(_persistMeasurementToCache(measurement));
      case StatusEvent(:final status):
        _setStatus(_normalizeStatus(status));
      case AlarmEvent():
      case AckEvent():
        // 不关心，由其他仓库处理。
        break;
    }
  }

  /// 主动拉取最近一帧测量数据（一般在页面打开时调用）。
  Future<Result<Measurement>> fetchLatest() async {
    try {
      final m = await _api.getLatest();
      _lastMeasurement = m;
      // 不在这里标记在线——存储的测量值不代表设备此刻在线，
      // 设备状态由 fetchStatus() 或实时遥测帧负责更新。
      // 但若当前 status 还没拿到 lastSeen（冷启动 + 设备离线），
      // 重新归一化一次，用 _lastMeasurement 作为 lastSeen 兜底，
      // 让横幅能显示"最后上报于 …"。
      if (_status.lastSeen == null) {
        _setStatus(_normalizeStatus(_status));
      }
      unawaited(_persistMeasurementToCache(m));
      return Ok(m);
    } catch (e, st) {
      appLog.w('fetchLatest failed: $e');
      return Err(e, st);
    }
  }

  /// 拉取历史曲线数据。
  ///
  /// [field] 指定要拉哪个字段的历史；[from]/[to] 是时间范围；
  /// [limit] 是后端单次最多返回的点数（默认 200）。
  Future<Result<List<HistoryPoint>>> fetchHistory({
    required HistoryField field,
    required DateTime from,
    required DateTime to,
    int limit = 200,
  }) async {
    var cached = const <HistoryPoint>[];
    try {
      cached = await _historyCache.readHistory(field: field, from: from, to: to);
    } catch (e) {
      appLog.w('read history cache failed: $e');
    }
    try {
      final pts = await _api.getHistory(
        field: field,
        from: from,
        to: to,
        limit: limit,
      );
      unawaited(_historyCache.storeHistory(field, pts));
      return Ok(_mergeHistoryPoints(cached, pts));
    } catch (e, st) {
      appLog.w('fetchHistory failed: $e');
      if (cached.isNotEmpty) {
        appLog.i('fetchHistory fallback to local cache count=${cached.length}');
        return Ok(cached);
      }
      return Err(e, st);
    }
  }

  /// 主动拉取一次链路状态。
  Future<Result<DeviceStatus>> fetchStatus() async {
    try {
      final s = await _api.getStatus();
      _setStatus(_normalizeStatus(s));
      return Ok(s);
    } catch (e, st) {
      return Err(e, st);
    }
  }

  /// 释放资源：取消定时器、取消订阅、关闭 controller。
  Future<void> dispose() async {
    _offlineTimer?.cancel();
    await _sub?.cancel();
    await _historyCache.dispose();
    await _liveCtrl.close();
    await _statusCtrl.close();
  }

  Future<void> _persistMeasurementToCache(Measurement measurement) async {
    try {
      await _historyCache.storeMeasurement(measurement);
    } catch (e) {
      appLog.w('store live history cache failed: $e');
    }
  }

  /// 收到遥测帧后：刷新本地接收时间、把状态拉为"在线"，
  /// 并安排下一次离线检测。
  ///
  /// 只有当数据时间戳足够新（在离线阈值内）时才标记为在线，
  /// 避免 hello 帧中的旧数据导致误判。
  void _markTelemetryOnline(Measurement measurement) {
    _lastTelemetryReceivedAt = DateTime.now();
    // 检查数据时间戳是否足够新，避免旧数据导致误判。
    if (!_isFresh(measurement.timestamp)) {
      // 数据太旧，不更新在线状态，只刷新本地接收时间。
      return;
    }
    _setStatus(
      DeviceStatus(
        online: true,
        lastSeen: measurement.timestamp,
        wsClients: _status.wsClients,
      ),
    );
    _scheduleOfflineCheck(Env.telemetryOfflineTimeout);
  }

  /// 把后端给的状态结合本地最近一次帧时间做"消歧"：
  /// 即使 `mqttConnected=true`，如果最近一次设备数据距今太久，
  /// 也认为设备离线，避免出现"链路在线但数据陈旧"的迷惑提示。
  ///
  /// 当后端 `/api/status` 未带 `lastMessageAt` 时，回落到本地缓存的最近一帧
  /// 时间戳，保证离线横幅能显示"最后上报于 …"。
  DeviceStatus _normalizeStatus(DeviceStatus status) {
    final lastSeen = status.lastSeen ?? _lastMeasurement?.timestamp;
    final online = status.online && _isFresh(lastSeen);
    final normalized = DeviceStatus(
      online: online,
      lastSeen: lastSeen,
      wsClients: status.wsClients,
    );
    if (online) {
      // 还在线：根据离线阈值剩余时间安排下次检查。
      final age = DateTime.now().difference(lastSeen!);
      final remaining = Env.telemetryOfflineTimeout - age;
      _scheduleOfflineCheck(remaining.isNegative ? Duration.zero : remaining);
    } else {
      _offlineTimer?.cancel();
    }
    return normalized;
  }

  /// 数据是否"够新"。
  bool _isFresh(DateTime? lastSeen) {
    if (lastSeen == null) return false;
    return lastSeen.isAfter(
      DateTime.now().subtract(Env.telemetryOfflineTimeout),
    );
  }

  /// 重新安排离线检测定时器。
  void _scheduleOfflineCheck(Duration delay) {
    _offlineTimer?.cancel();
    _offlineTimer = Timer(delay, _markOfflineIfTelemetryStale);
  }

  /// 定时器到点后的回调：
  /// 如果在等待期间又来了新帧，则推迟重新计时；否则把状态设为离线。
  void _markOfflineIfTelemetryStale() {
    final receivedAt = _lastTelemetryReceivedAt;
    if (receivedAt != null &&
        DateTime.now().difference(receivedAt) < Env.telemetryOfflineTimeout) {
      // 等待期间收到了新帧，重新排定。
      _scheduleOfflineCheck(
        Env.telemetryOfflineTimeout - DateTime.now().difference(receivedAt),
      );
      return;
    }
    if (!_status.online) return;
    _setStatus(
      DeviceStatus(
        online: false,
        lastSeen: _status.lastSeen,
        wsClients: _status.wsClients,
      ),
    );
  }

  /// 更新内部状态并广播。
  void _setStatus(DeviceStatus status) {
    final wasOnline = _status.online;
    _status = status;
    _statusCtrl.add(status);
    // 检测在线/离线状态变化，生成客户端告警。
    if (wasOnline && !status.online) {
      onStatusAlarm?.call(Alarm(
        seq: DateTime.now().millisecondsSinceEpoch,
        timestamp: DateTime.now(),
        code: 'DEVICE_OFFLINE',
        severity: AlarmSeverity.warn,
      ));
    } else if (!wasOnline && status.online) {
      onStatusAlarm?.call(Alarm(
        seq: DateTime.now().millisecondsSinceEpoch,
        timestamp: DateTime.now(),
        code: 'DEVICE_ONLINE',
        severity: AlarmSeverity.info,
      ));
    }
  }

  List<HistoryPoint> _mergeHistoryPoints(
    List<HistoryPoint> cached,
    List<HistoryPoint> remote,
  ) {
    if (cached.isEmpty) return remote;
    if (remote.isEmpty) return cached;

    final merged = <HistoryPoint>[];
    final seen = <String>{};

    void appendAll(List<HistoryPoint> points) {
      for (final point in points) {
        if (!point.value.isFinite) continue;
        final key =
            '${point.timestamp.millisecondsSinceEpoch}:${point.value.toStringAsPrecision(12)}';
        if (seen.add(key)) {
          merged.add(point);
        }
      }
    }

    appendAll(cached);
    appendAll(remote);
    merged.sort((a, b) => a.timestamp.compareTo(b.timestamp));
    return merged;
  }
}
