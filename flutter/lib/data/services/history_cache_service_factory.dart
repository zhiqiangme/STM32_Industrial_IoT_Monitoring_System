import 'history_cache_service.dart';
import 'history_cache_service_stub.dart'
    if (dart.library.io) 'history_cache_service_io.dart' as impl;

/// 按平台挑选历史缓存实现。
HistoryCacheService createHistoryCacheService() =>
    impl.createHistoryCacheService();
