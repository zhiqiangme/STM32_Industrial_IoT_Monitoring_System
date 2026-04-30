import 'package:flutter/foundation.dart';

import '../../../data/models/command.dart';
import '../../../data/repositories/command_repository.dart';
import '../../../utils/result.dart';

/// 控制页 ViewModel。
///
/// 把"重启"这一命令式操作和它的"忙碌 / 结果"状态封在一起：
/// 命令在飞时按钮置灰、用户能立即看到成功 / 失败反馈。
///
/// 注：本页未挂路由，等后端补上 `/api/cmd/...` 后再启用。
class ControlViewModel extends ChangeNotifier {
  ControlViewModel({required CommandRepository repository})
      : _repo = repository;

  final CommandRepository _repo;

  bool _busy = false;
  Command? _lastCommand;
  String? _message;

  bool get busy => _busy;
  Command? get lastCommand => _lastCommand;

  /// 用于驱动 SnackBar 的一次性提示。读取后调用 [clearMessage]。
  String? get message => _message;

  /// 触发一次"重启设备"。已在飞时直接忽略，避免重复点击。
  Future<void> reboot() async {
    if (_busy) return;
    _busy = true;
    _message = null;
    notifyListeners();

    final res = await _repo.sendReboot();
    switch (res) {
      case Ok(:final value):
        _lastCommand = value;
        _message = value.status == CommandStatus.acked
            ? '设备已确认重启指令 (seq=${value.seq})'
            : '重启指令失败：${value.result ?? '未知'}';
      case Err(:final error):
        _message = '发送失败：$error';
    }
    _busy = false;
    notifyListeners();
  }

  /// SnackBar 显示完毕后调用，避免下次重建又弹一次。
  void clearMessage() {
    if (_message == null) return;
    _message = null;
    notifyListeners();
  }
}
