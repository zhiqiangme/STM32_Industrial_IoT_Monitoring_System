import '../models/history_point.dart';
import '../models/measurement.dart';

/// 历史缓存抽象。
///
/// - 实机（Android 等 IO 平台）使用本地数据库实现；
/// - 不支持的平台退化为 no-op，避免影响现有页面逻辑。
abstract interface class HistoryCacheService {
  /// 写入一帧实时测量，供历史页离线回看。
  Future<void> storeMeasurement(Measurement measurement);

  /// 写入一批历史点，通常来自服务端 `/api/history`。
  Future<void> storeHistory(
    HistoryField field,
    List<HistoryPoint> points,
  );

  /// 读取指定字段在时间区间内的本地历史点。
  Future<List<HistoryPoint>> readHistory({
    required HistoryField field,
    required DateTime from,
    required DateTime to,
  });

  /// 释放底层资源。
  Future<void> dispose();
}
