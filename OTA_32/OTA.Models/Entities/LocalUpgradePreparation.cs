namespace OTA.Models;

/// <summary>
/// 本地升级执行前的校验与展示数据。
/// </summary>
public readonly record struct LocalUpgradePreparation(
    LocalUpgradeOptions Options,
    FirmwareImageInfo ImageInfo,
    FirmwareSlot RunningSlot,
    IReadOnlyList<string> StartupMessages);
