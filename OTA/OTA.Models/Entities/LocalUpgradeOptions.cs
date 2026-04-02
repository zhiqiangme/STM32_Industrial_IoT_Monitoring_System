namespace OTA.Models;

/// <summary>
/// 本地升级所需的全部参数。
/// </summary>
public readonly record struct LocalUpgradeOptions(
    string PortName,
    int BaudRate,
    double TimeoutSeconds,
    string ImagePath,
    uint TargetFirmwareVersion = 0);
