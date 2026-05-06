import 'dart:async';

import 'package:path/path.dart' as p;
import 'package:sqflite/sqflite.dart';

import '../../config/env.dart';
import '../../utils/app_logger.dart';
import '../models/history_point.dart';
import '../models/measurement.dart';
import 'history_cache_service.dart';

class SqfliteHistoryCacheService implements HistoryCacheService {
  static const String _dbName = 'mill_history_cache.db';
  static const int _dbVersion = 1;
  static const String _table = 'history_points';
  static const Duration _retention = Duration(days: 10);

  Database? _db;
  Future<Database>? _opening;
  bool _cleanupQueued = false;

  @override
  Future<void> storeMeasurement(Measurement measurement) async {
    final rows = _rowsFromMeasurement(measurement);
    if (rows.isEmpty) return;
    await _insertRows(rows);
  }

  @override
  Future<void> storeHistory(
    HistoryField field,
    List<HistoryPoint> points,
  ) async {
    final rows = <Map<String, Object>>[];
    for (final point in points) {
      if (!point.value.isFinite) continue;
      rows.add(_buildRow(field.id, point.timestamp, point.value));
    }
    if (rows.isEmpty) return;
    await _insertRows(rows);
  }

  @override
  Future<List<HistoryPoint>> readHistory({
    required HistoryField field,
    required DateTime from,
    required DateTime to,
  }) async {
    final db = await _database();
    final rows = await db.query(
      _table,
      columns: const ['timestamp_ms', 'value'],
      where: 'device_id = ? AND field = ? AND timestamp_ms >= ? AND timestamp_ms <= ?',
      whereArgs: [
        Env.deviceId,
        field.id,
        from.millisecondsSinceEpoch,
        to.millisecondsSinceEpoch,
      ],
      orderBy: 'timestamp_ms ASC, id ASC',
    );
    return rows.map((row) {
      return HistoryPoint(
        timestamp: DateTime.fromMillisecondsSinceEpoch(
          row['timestamp_ms'] as int,
        ),
        value: (row['value'] as num).toDouble(),
      );
    }).toList(growable: false);
  }

  @override
  Future<void> dispose() async {
    final opening = _opening;
    final db = _db ?? (opening != null ? await opening : null);
    _opening = null;
    _db = null;
    if (db != null && db.isOpen) {
      await db.close();
    }
  }

  Future<void> _insertRows(List<Map<String, Object>> rows) async {
    final db = await _database();
    final batch = db.batch();
    for (final row in rows) {
      batch.insert(_table, row, conflictAlgorithm: ConflictAlgorithm.ignore);
    }
    await batch.commit(noResult: true);
    _scheduleCleanup(db);
  }

  Future<Database> _database() {
    final ready = _db;
    if (ready != null) {
      return Future.value(ready);
    }
    final opening = _opening;
    if (opening != null) {
      return opening;
    }
    final future = _openDatabase();
    _opening = future;
    return future;
  }

  Future<Database> _openDatabase() async {
    try {
      final dir = await getDatabasesPath();
      final path = p.join(dir, _dbName);
      final db = await openDatabase(
        path,
        version: _dbVersion,
        onCreate: (db, version) async {
          await db.execute('''
            CREATE TABLE $_table (
              id INTEGER PRIMARY KEY AUTOINCREMENT,
              device_id TEXT NOT NULL,
              field TEXT NOT NULL,
              timestamp_ms INTEGER NOT NULL,
              value REAL NOT NULL
            )
          ''');
          await db.execute('''
            CREATE INDEX idx_history_lookup
            ON $_table(device_id, field, timestamp_ms)
          ''');
          await db.execute('''
            CREATE UNIQUE INDEX idx_history_unique
            ON $_table(device_id, field, timestamp_ms, value)
          ''');
        },
      );
      _db = db;
      return db;
    } finally {
      _opening = null;
    }
  }

  void _scheduleCleanup(Database db) {
    if (_cleanupQueued) return;
    _cleanupQueued = true;
    unawaited(() async {
      try {
        final cutoff = DateTime.now()
            .subtract(_retention)
            .millisecondsSinceEpoch;
        await db.delete(
          _table,
          where: 'device_id = ? AND timestamp_ms < ?',
          whereArgs: [Env.deviceId, cutoff],
        );
      } catch (e) {
        appLog.w('history cache cleanup failed: $e');
      } finally {
        _cleanupQueued = false;
      }
    }());
  }

  List<Map<String, Object>> _rowsFromMeasurement(Measurement measurement) {
    final rows = <Map<String, Object>>[];
    final timestamp = measurement.timestamp;

    void add(String field, double? value) {
      if (value == null || !value.isFinite) return;
      rows.add(_buildRow(field, timestamp, value));
    }

    add(HistoryField.flow.id, measurement.flow);
    add(HistoryField.total.id, measurement.total);
    add(HistoryField.weight.id, measurement.weight);

    const tempFields = [
      HistoryField.t1,
      HistoryField.t2,
      HistoryField.t3,
      HistoryField.t4,
    ];
    for (var i = 0; i < measurement.temperatures.length && i < 4; i++) {
      add(tempFields[i].id, measurement.temperatures[i]);
    }
    return rows;
  }

  Map<String, Object> _buildRow(String field, DateTime timestamp, double value) {
    return {
      'device_id': Env.deviceId,
      'field': field,
      'timestamp_ms': timestamp.millisecondsSinceEpoch,
      'value': value,
    };
  }
}

HistoryCacheService createHistoryCacheService() => SqfliteHistoryCacheService();
