import '../models/history_point.dart';
import '../models/measurement.dart';
import 'history_cache_service.dart';

/// 非 IO 平台的兜底实现：不落盘，只保持接口可用。
class NoopHistoryCacheService implements HistoryCacheService {
  @override
  Future<void> dispose() async {}

  @override
  Future<List<HistoryPoint>> readHistory({
    required HistoryField field,
    required DateTime from,
    required DateTime to,
  }) async {
    return const [];
  }

  @override
  Future<void> storeHistory(
    HistoryField field,
    List<HistoryPoint> points,
  ) async {}

  @override
  Future<void> storeMeasurement(Measurement measurement) async {}
}

HistoryCacheService createHistoryCacheService() => NoopHistoryCacheService();
