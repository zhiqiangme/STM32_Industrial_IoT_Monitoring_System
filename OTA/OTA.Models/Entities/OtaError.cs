namespace OTA.Models;

/// <summary>
/// OTA 业务错误对象。
/// </summary>
public sealed record OtaError(OtaErrorCode Code, string Message);
