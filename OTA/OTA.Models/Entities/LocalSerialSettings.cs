namespace OTA.Models;

/// <summary>
/// 串口连接参数。
/// </summary>
public readonly record struct LocalSerialSettings(
    string PortName,
    int BaudRate,
    double TimeoutSeconds);
