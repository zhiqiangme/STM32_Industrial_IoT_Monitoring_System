import 'dart:async';

import 'package:flutter/foundation.dart';

import '../../../data/models/command.dart';
import '../../../data/models/measurement.dart';
import '../../../data/repositories/auth_repository.dart';
import '../../../data/repositories/command_repository.dart';
import '../../../data/repositories/measurement_repository.dart';
import '../../../utils/result.dart';

/// 控制页 ViewModel。
///
/// 负责展示当前继电器输出状态，并把"设定位图"命令封装成可等待 ack 的一次性操作。
class ControlViewModel extends ChangeNotifier {
  ControlViewModel({
    required CommandRepository repository,
    required MeasurementRepository measurements,
    required AuthRepository auth,
  })  : _repo = repository,
        _measurements = measurements,
        _auth = auth {
    _measurement = measurements.lastMeasurement;
    _liveSub = measurements.liveStream.listen(_onMeasurement);
    _auth.addListener(_onAuthChanged);
  }

  final CommandRepository _repo;
  final MeasurementRepository _measurements;
  final AuthRepository _auth;
  StreamSubscription<Measurement>? _liveSub;

  bool _busy = false;
  Command? _lastCommand;
  String? _message;
  Measurement? _measurement;

  bool get busy => _busy;
  Command? get lastCommand => _lastCommand;
  String? get message => _message;
  bool get isLoggedIn => _auth.isLoggedIn;
  int get relayMask => _measurement?.relayDo ?? 0;

  /// 统一用 16 路 bitmask 表示继电器状态。
  bool relayEnabled(int index) => (relayMask & (1 << index)) != 0;

  Future<void> setRelay(int index, bool enabled) async {
    if (_busy || index < 0 || index >= 16 || !_auth.isLoggedIn) return;

    final currentMask = relayMask;
    final targetMask = enabled
        ? (currentMask | (1 << index))
        : (currentMask & ~(1 << index));

    _busy = true;
    _message = null;
    notifyListeners();

    final res = await _repo.sendRelaySet(targetMask);
    switch (res) {
      case Ok(:final value):
        _lastCommand = value;
        _message = value.status == CommandStatus.acked
            ? '继电器状态已确认 (seq=${value.seq})'
            : '继电器控制失败：${value.result ?? '未知'}';
      case Err(:final error):
        _message = '发送失败：$error';
    }

    _busy = false;
    notifyListeners();
  }

  void clearMessage() {
    if (_message == null) return;
    _message = null;
    notifyListeners();
  }

  void _onMeasurement(Measurement measurement) {
    _measurement = measurement;
    notifyListeners();
  }

  void _onAuthChanged() {
    if (_auth.isLoggedIn) {
      _measurement = _measurements.lastMeasurement;
    } else {
      _measurement = null;
      _busy = false;
      _lastCommand = null;
      _message = null;
    }
    notifyListeners();
  }

  @override
  void dispose() {
    _auth.removeListener(_onAuthChanged);
    _liveSub?.cancel();
    super.dispose();
  }
}
