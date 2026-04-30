import 'package:flutter_secure_storage/flutter_secure_storage.dart';

/// 对 [FlutterSecureStorage] 的薄封装。
///
/// 让上层只看到一个小而强类型的 API；实现可以替换
/// （例如单测里用一个内存版本的 fake 实现）。
class SecureStorageService {
  SecureStorageService({FlutterSecureStorage? storage})
      : _storage = storage ?? const FlutterSecureStorage();

  final FlutterSecureStorage _storage;

  /// 鉴权 token 的存储 key。集中放在常量便于以后改名。
  static const _kToken = 'auth_token';

  /// 写入鉴权 token。
  Future<void> writeToken(String token) =>
      _storage.write(key: _kToken, value: token);

  /// 读取已保存的 token，未登录时返回 null。
  Future<String?> readToken() => _storage.read(key: _kToken);

  /// 退出登录或失败时清除 token。
  Future<void> clearToken() => _storage.delete(key: _kToken);
}
