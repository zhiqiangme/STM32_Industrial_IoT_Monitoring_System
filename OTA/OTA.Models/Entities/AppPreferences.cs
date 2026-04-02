namespace OTA.Models;

/// <summary>
/// 应用级偏好设置。
/// 使用轻量 JSON 文件持久化本地升级页面偏好与主窗口状态。
/// </summary>
public sealed class AppPreferences
{
    public LocalUpgradePreferences LocalUpgrade { get; set; } = new();

    public WindowPreferences Window { get; set; } = new();
}

/// <summary>
/// 本地升级页面偏好。
/// </summary>
public sealed class LocalUpgradePreferences
{
    public string LastFirmwarePath { get; set; } = string.Empty;

    public string LastPortName { get; set; } = string.Empty;

    public string BaudRateText { get; set; } = string.Empty;
}

/// <summary>
/// 主窗口显示状态偏好。
/// </summary>
public sealed class WindowPreferences
{
    public double? Left { get; set; }

    public double? Top { get; set; }

    public double? Width { get; set; }

    public double? Height { get; set; }

    public bool IsMaximized { get; set; }
}
