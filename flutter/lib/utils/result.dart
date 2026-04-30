/// 数据层广泛使用的函数式 [Result] 类型。
///
/// Repository 一律返回 `Future<Result<T>>`，让 ViewModel 可以
/// 用模式匹配区分成功 / 失败，不必到处写 try/catch。
///
/// ```dart
/// final res = await repo.getLatest();
/// switch (res) {
///   case Ok(:final value): // 使用 value
///   case Err(:final error): // 显示错误
/// }
/// ```
sealed class Result<T> {
  const Result();

  /// 是否为成功结果。
  bool get isOk => this is Ok<T>;

  /// 取值；若是 [Err] 则返回 `null`，便于链式表达。
  T? get valueOrNull => switch (this) {
        Ok<T>(:final value) => value,
        Err<T>() => null,
      };
}

/// 成功分支：携带类型为 [T] 的实际值。
final class Ok<T> extends Result<T> {
  final T value;
  const Ok(this.value);
}

/// 失败分支：携带异常对象与可选的调用栈，便于上层日志定位。
final class Err<T> extends Result<T> {
  final Object error;
  final StackTrace? stackTrace;
  const Err(this.error, [this.stackTrace]);

  @override
  String toString() => 'Err($error)';
}
