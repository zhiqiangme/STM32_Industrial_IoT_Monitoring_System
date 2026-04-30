import 'package:logger/logger.dart';

/// 全局共享的日志实例。
///
/// 全项目应使用 `appLog.d/i/w/e(...)` 而不是 `print`，
/// 以便统一格式、便于在生产环境替换为远端日志收集。
final Logger appLog = Logger(
  printer: PrettyPrinter(
    methodCount: 0, // 普通日志不打印调用栈
    errorMethodCount: 5, // 错误日志保留 5 行调用栈，便于定位
    lineLength: 100,
    colors: true,
    printEmojis: false,
    dateTimeFormat: DateTimeFormat.onlyTimeAndSinceStart,
  ),
);
