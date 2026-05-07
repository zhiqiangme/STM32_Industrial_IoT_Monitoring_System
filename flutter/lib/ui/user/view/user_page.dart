import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';
import 'package:provider/provider.dart';

import '../view_model/user_view_model.dart';

/// 用户 Tab 页面。
///
/// 两态 UI：
/// - 未登录：账号 / 密码登录表单；
/// - 已登录：用户名 + 设置占位 + 退出登录按钮。
///
/// 登录态切换由 [UserViewModel] 监听 [AuthRepository] 后通知，
/// 页面据此自动重建。
class UserPage extends StatelessWidget {
  const UserPage({super.key});

  @override
  Widget build(BuildContext context) {
    return Consumer<UserViewModel>(
      builder: (context, vm, _) {
        // 已登录：展示账号信息和登出入口；未登录：展示登录表单。
        return SafeArea(
          child: vm.isLoggedIn
              ? _LoggedInView(vm: vm)
              : _LoginFormView(vm: vm),
        );
      },
    );
  }
}

/// 未登录态：账号 / 密码表单。
class _LoginFormView extends StatefulWidget {
  const _LoginFormView({required this.vm});

  final UserViewModel vm;

  @override
  State<_LoginFormView> createState() => _LoginFormViewState();
}

class _LoginFormViewState extends State<_LoginFormView> {
  // Form key + 两个文本控制器。State 自己持有，dispose 时释放。
  final _form = GlobalKey<FormState>();
  final _userCtrl = TextEditingController();
  final _pwdCtrl = TextEditingController();

  @override
  void dispose() {
    _userCtrl.dispose();
    _pwdCtrl.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final vm = widget.vm;
    return Center(
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(24),
        child: Form(
          key: _form,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const FlutterLogo(size: 72),
              const SizedBox(height: 16),
              Text('磨坊系统', style: Theme.of(context).textTheme.headlineSmall),
              const SizedBox(height: 8),
              Text(
                '请使用后台预置账号登录',
                style: Theme.of(context).textTheme.bodySmall,
              ),
              const SizedBox(height: 32),
              // 用户名输入框。
              TextFormField(
                controller: _userCtrl,
                textInputAction: TextInputAction.next,
                decoration: const InputDecoration(
                  labelText: '用户名',
                  border: OutlineInputBorder(),
                ),
                validator: (v) =>
                    (v == null || v.trim().isEmpty) ? '请输入用户名' : null,
              ),
              const SizedBox(height: 12),
              // 密码输入框（脱敏）。
              TextFormField(
                controller: _pwdCtrl,
                obscureText: true,
                textInputAction: TextInputAction.done,
                onFieldSubmitted: (_) => _submit(vm),
                decoration: const InputDecoration(
                  labelText: '密码',
                  border: OutlineInputBorder(),
                ),
                validator: (v) =>
                    (v == null || v.isEmpty) ? '请输入密码' : null,
              ),
              // 错误信息：仅当 ViewModel 报错时才出现。
              if (vm.error != null) ...[
                const SizedBox(height: 12),
                Text(
                  vm.error!,
                  style: TextStyle(color: Theme.of(context).colorScheme.error),
                ),
              ],
              const SizedBox(height: 24),
              // 登录按钮：busy 时置灰并展示小转圈。
              SizedBox(
                width: double.infinity,
                child: FilledButton(
                  onPressed: vm.busy ? null : () => _submit(vm),
                  child: vm.busy
                      ? const SizedBox(
                          width: 18,
                          height: 18,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Text('登录'),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  /// 提交：先做表单校验，再走 ViewModel；登录态切换后页面会自动重建到已登录视图。
  Future<void> _submit(UserViewModel vm) async {
    if (!_form.currentState!.validate()) return;
    await vm.submit(
      username: _userCtrl.text.trim(),
      password: _pwdCtrl.text,
    );
    // 不需要手动跳转：UserPage 顶层会随登录态切换刷新。
  }
}

/// 已登录态：账号信息 + 功能入口 + 设置。
class _LoggedInView extends StatelessWidget {
  const _LoggedInView({required this.vm});

  final UserViewModel vm;

  @override
  Widget build(BuildContext context) {
    final username = vm.currentUsername ?? '已登录';
    final message = vm.uploadMessage;
    if (message != null) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (!context.mounted) return;
        ScaffoldMessenger.of(context).clearSnackBars();
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text(message)));
        vm.clearUploadMessage();
      });
    }

    return ListView(
      padding: const EdgeInsets.symmetric(vertical: 16),
      children: [
        // 顶部头像 + 用户名。
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 16),
          child: Column(
            children: [
              const CircleAvatar(
                radius: 36,
                child: Icon(Icons.person, size: 40),
              ),
              const SizedBox(height: 12),
              Text(
                username,
                style: Theme.of(context).textTheme.titleLarge,
              ),
            ],
          ),
        ),
        const Divider(),
        _UploadPeriodTile(vm: vm),
        const Divider(height: 1),
        ListTile(
          leading: const Icon(Icons.settings_outlined),
          title: const Text('设置'),
          trailing: const Icon(Icons.chevron_right),
          onTap: () => context.push('/user/settings'),
        ),
      ],
    );
  }
}

class _UploadPeriodTile extends StatelessWidget {
  const _UploadPeriodTile({required this.vm});

  static const _periods = [2, 10, 30, 60];

  final UserViewModel vm;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.fromLTRB(16, 8, 16, 12),
      child: ListTile(
        contentPadding: EdgeInsets.zero,
        enabled: !vm.uploadBusy,
        leading: const Icon(Icons.schedule_outlined),
        title: const Text('网关上报频率'),
        trailing: vm.uploadBusy
            ? const SizedBox(
                width: 22,
                height: 22,
                child: CircularProgressIndicator(strokeWidth: 2),
              )
            : Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(_formatPeriod(vm.uploadPeriodSeconds)),
                  const SizedBox(width: 4),
                  const Icon(Icons.expand_more),
                ],
              ),
        onTap: vm.uploadBusy ? null : () => _changeUploadPeriod(context),
      ),
    );
  }

  Future<void> _changeUploadPeriod(BuildContext context) async {
    final selected = await showModalBottomSheet<int>(
      context: context,
      builder: (sheetContext) {
        final current = vm.uploadPeriodSeconds;
        return SafeArea(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const ListTile(
                title: Text('选择网关上报频率'),
              ),
              for (final seconds in _periods)
                ListTile(
                  title: Text(_formatPeriod(seconds)),
                  selected: seconds == current,
                  trailing: seconds == current ? const Icon(Icons.check) : null,
                  onTap: () => Navigator.of(sheetContext).pop(seconds),
                ),
            ],
          ),
        );
      },
    );
    if (!context.mounted ||
        selected == null ||
        selected == vm.uploadPeriodSeconds) {
      return;
    }

    // 选择后再二次确认，确认前不下发设备命令。
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (dialogContext) => AlertDialog(
        title: const Text('确认修改上报频率'),
        content: Text('确定要把网关上报频率改为 ${_formatPeriod(selected)} 吗？'),
        actions: [
          FilledButton(
            onPressed: () => Navigator.of(dialogContext).pop(false),
            child: const Text('取消'),
          ),
          TextButton(
            onPressed: () => Navigator.of(dialogContext).pop(true),
            child: const Text('确定'),
          ),
        ],
      ),
    );
    if (confirmed == true && context.mounted) {
      await vm.setUploadPeriod(selected);
    }
  }

  static String _formatPeriod(int seconds) =>
      seconds == 60 ? '1 分钟' : '$seconds 秒';
}
