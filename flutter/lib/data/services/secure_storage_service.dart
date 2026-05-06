import 'package:flutter_secure_storage/flutter_secure_storage.dart';

import '../../utils/app_logger.dart';

/// 对 [FlutterSecureStorage] 的薄封装。
///
/// 让上层只看到一个小而强类型的 API；实现可以替换
/// （例如单测里用一个内存版本的 fake 实现）。
class SecureStorageService {
  SecureStorageService({FlutterSecureStorage? storage})
      : _storage = storage ??
            const FlutterSecureStorage(
              // EncryptedSharedPreferences 比默认 RSA Keystore 实现更稳：
              // 调试期反复重装 / OS 升级时不易解密失败，避免 token 被误清。
              aOptions: AndroidOptions(encryptedSharedPreferences: true),
            );

  final FlutterSecureStorage _storage;

  /// 鉴权 token 的存储 key。集中放在常量便于以后改名。
  static const _kToken = 'auth_token';

  /// 写入鉴权 token。空字符串会被拒绝并抛 [ArgumentError]，
  /// 防止上游 BUG 写入空值后下游静默退化（例如 WS 以 "empty token" 跳过连接）。
  Future<void> writeToken(String token) async {
    if (token.isEmpty) {
      throw ArgumentError.value(token, 'token', 'must not be empty');
    }
    appLog.i(
      '[token-trace] writeToken len=${token.length}\n${StackTrace.current}',
    );
    try {
      await _storage.write(key: _kToken, value: token);
      appLog.i('[token-trace] writeToken OK');
    } catch (e) {
      appLog.w('[token-trace] writeToken FAILED: $e');
      rethrow;
    }
  }

  /// 读取已保存的 token，未登录时返回 null。
  Future<String?> readToken() async {
    try {
      final t = await _storage.read(key: _kToken);
      appLog.i(
        '[token-trace] readToken result='
        '${t == null ? 'null' : t.isEmpty ? 'empty' : 'len=${t.length}'}',
      );
      return t;
    } catch (e) {
      appLog.w(
        '[token-trace] readToken THREW: $e\n${StackTrace.current}',
      );
      rethrow;
    }
  }

  /// 退出登录或失败时清除 token。
  Future<void> clearToken() async {
    appLog.w('[token-trace] clearToken called\n${StackTrace.current}');
    try {
      await _storage.delete(key: _kToken);
      appLog.w('[token-trace] clearToken OK');
    } catch (e) {
      appLog.w('[token-trace] clearToken FAILED: $e');
      rethrow;
    }
  }
}
