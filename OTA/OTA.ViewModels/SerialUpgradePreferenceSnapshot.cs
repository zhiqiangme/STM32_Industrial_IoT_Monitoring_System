namespace OTA.ViewModels;

/// <summary>
/// 共享升级页面读取到的持久化偏好快照。
/// </summary>
public readonly record struct SerialUpgradePreferenceSnapshot(
    string LastFirmwarePath,
    string LastPortName,
    string BaudRateText);
