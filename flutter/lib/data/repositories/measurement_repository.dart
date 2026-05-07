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
  static const Duration _statusPollInterval = Duration(seconds: 15);

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
  /// WS 实时帧或 HTTP 兜底轮询确认到新数据时都会刷新它，
  /// 用于计算超过 [Env.telemetryOfflineTimeout] 后是否应判定为离线。
  DateTime? _lastTelemetryReceivedAt;

  /// 离线判定的延时定时器。每次收到新帧后会重置。
  Timer? _offlineTimer;

  /// 登录态下的 HTTP 轮询兜底：用于覆盖移动端长连被系统/NAT 静默掐断的情况。
  Timer? _statusPollTimer;
  bool _statusPollEnabled = false;
  bool _statusPollInFlight = false;
  bool _suspendedForBackground = false;

  /// 设备状态变化时触发的回调，用于生成客户端告警。
  /// 由 [AlarmRepository] 注入。
  void Function(Alarm alarm)? onStatusAlarm;

  /// 会话冷启动 / 恢复阶段先抑制在线状态告警，
  /// 避免把"首次拿到当前在线状态"误判成"离线后恢复在线"。
  bool _statusAlarmArmed = false;

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

  /// 登录后开启状态轮询兜底；重复调用幂等。
  Future<void> startSessionSync() async {
    if (_statusPollEnabled) return;
    _statusPollEnabled = true;
    await refreshSessionSnapshot();
    _statusPollTimer?.cancel();
    _statusPollTimer = Timer.periodic(_statusPollInterval, (_) {
      unawaited(_refreshServerSnapshot());
    });
  }

  /// 退出登录时停止轮询，避免未登录状态持续打接口。
  Future<void> stopSessionSync() async {
    _statusPollEnabled = false;
    _statusPollTimer?.cancel();
    _statusPollTimer = null;
    _statusPollInFlight = false;
  }

  /// 会话建立期间先关闭状态边沿告警，等待首条状态作为基线。
  void beginSessionBootstrap() {
    _statusAlarmArmed = false;
  }

  /// App 进入后台时暂停本地离线定时器与 HTTP 兜底轮询。
  ///
  /// 手机息屏后 Dart Timer 和 WebSocket 都可能被系统挂起，不能把这段时间
  /// 当作设备真实离线时间；恢复前台后以服务端状态重新建立基线。
  /// 同时取消轮询定时器，避免后台仍持续打 `/api/status`、`/api/latest`。
  void suspendForAppBackground() {
    _suspendedForBackground = true;
    _offlineTimer?.cancel();
    _offlineTimer = null;
    _statusPollTimer?.cancel();
    _statusPollTimer = null;
    beginSessionBootstrap();
  }

  /// App 回到前台前重新打开离线检测，并让下一条状态只作为基线。
  /// 若挂起期间取消了轮询定时器，这里把它重新拉起。
  void prepareForAppForeground() {
    _suspendedForBackground = false;
    beginSessionBootstrap();
    if (_statusPollEnabled && _statusPollTimer == null) {
      _statusPollTimer = Timer.periodic(_statusPollInterval, (_) {
        unawaited(_refreshServerSnapshot());
      });
    }
  }

  /// 在 App 回到前台时主动同步一次，避免依赖下一轮定时器才恢复状态。
  Future<void> refreshSessionSnapshot() async {
    if (!_statusPollEnabled) return;
    await _refreshServerSnapshot();
  }

  /// 释放资源：取消定时器、取消订阅、关闭 controller。
  Future<void> dispose() async {
    await stopSessionSync();
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

  /// 会话级兜底：定期用 HTTP 确认服务端最近一帧与在线状态。
  ///
  /// 如果发现服务端的 lastSeen 继续前进，但本地 WS 一直没把最新遥测推过来，
  /// 则补拉一次 `/api/latest`，避免 UI 因长连接静默失活而卡死在旧数据上。
  Future<void> _refreshServerSnapshot() async {
    if (!_statusPollEnabled || _statusPollInFlight || _suspendedForBackground) {
      return;
    }
    _statusPollInFlight = true;
    try {
      final status = await _api.getStatus();
      if (!_statusPollEnabled) return;

      final normalized = _normalizeStatus(status);
      if (normalized.online) {
        _lastTelemetryReceivedAt = DateTime.now();
      }
      _setStatus(normalized);

      final serverLastSeen = normalized.lastSeen;
      if (serverLastSeen != null && _shouldPullLatestSnapshot(serverLastSeen)) {
        final latest = await _api.getLatest();
        if (!_statusPollEnabled) return;
        _lastMeasurement = latest;
        _lastTelemetryReceivedAt = DateTime.now();
        _liveCtrl.add(latest);
        unawaited(_persistMeasurementToCache(latest));
      }
    } catch (e) {
      appLog.w('session sync failed: $e');
    } finally {
      _statusPollInFlight = false;
    }
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
    if (online && lastSeen != null) {
      // 还在线：根据离线阈值剩余时间安排下次检查。
      // online 为 true 隐含 lastSeen 非 null（_isFresh 对 null 返回 false），
      // 这里多写一遍空检查只为去掉强解包，避免后续修改 _isFresh 时引入 NPE。
      final age = DateTime.now().difference(lastSeen);
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

  /// 仅当本地实时流已经沉默一小段时间，且服务端 lastSeen 明显更近时，
  /// 才回源补拉最新值，避免在 WS 正常时重复请求 `/api/latest`。
  bool _shouldPullLatestSnapshot(DateTime serverLastSeen) {
    final localMeasurementTime = _lastMeasurement?.timestamp;
    final wsAppearsStale = _lastTelemetryReceivedAt == null ||
        DateTime.now().difference(_lastTelemetryReceivedAt!) >=
            _statusPollInterval;
    if (!wsAppearsStale) return false;
    if (localMeasurementTime == null) return true;
    return serverLastSeen.isAfter(localMeasurementTime);
  }

  /// 重新安排离线检测定时器。
  void _scheduleOfflineCheck(Duration delay) {
    if (_suspendedForBackground) return;
    _offlineTimer?.cancel();
    _offlineTimer = Timer(delay, _markOfflineIfTelemetryStale);
  }

  /// 定时器到点后的回调：
  /// 如果在等待期间又来了新帧，则推迟重新计时；否则把状态设为离线。
  void _markOfflineIfTelemetryStale() {
    if (_suspendedForBackground) return;
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
    if (!_statusAlarmArmed) {
      _statusAlarmArmed = true;
      return;
    }
    if (_suspendedForBackground) return;
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
    final seen = <int>{};

    // 仅以毫秒级时间戳作为去重 key：同时刻保留先入项（cached 优先，
    // 这样后续 remote 不会用浮点重建出的近似值覆盖原始缓存）。
    void appendAll(List<HistoryPoint> points) {
      for (final point in points) {
        if (!point.value.isFinite) continue;
        final key = point.timestamp.millisecondsSinceEpoch;
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
