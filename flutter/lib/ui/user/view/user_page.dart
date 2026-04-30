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
              Text('管道监控系统', style: Theme.of(context).textTheme.headlineSmall),
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

/// 已登录态：账号信息 + 设置占位 + 退出登录。
class _LoggedInView extends StatelessWidget {
  const _LoggedInView({required this.vm});

  final UserViewModel vm;

  @override
  Widget build(BuildContext context) {
    final username = vm.currentUsername ?? '已登录';
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
        ListTile(
          leading: const Icon(Icons.settings_outlined),
          title: const Text('设置'),
          trailing: const Icon(Icons.chevron_right),
          onTap: () => context.push('/user/settings'),
        ),
        const Divider(),
        // 退出登录：点击后回到未登录态，三个数据 Tab 同时清空。
        Padding(
          padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 16),
          child: FilledButton.tonalIcon(
            onPressed: () => _confirmLogout(context),
            icon: const Icon(Icons.logout),
            label: const Text('退出登录'),
          ),
        ),
      ],
    );
  }

  /// 退出前给一个确认弹窗，避免误触。
  Future<void> _confirmLogout(BuildContext context) async {
    final confirmed = await showDialog<bool>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('退出登录'),
        content: const Text('确定要退出当前账号吗？'),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(ctx).pop(false),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () => Navigator.of(ctx).pop(true),
            child: const Text('退出'),
          ),
        ],
      ),
    );
    if (confirmed == true) {
      await vm.logout();
    }
  }
}
