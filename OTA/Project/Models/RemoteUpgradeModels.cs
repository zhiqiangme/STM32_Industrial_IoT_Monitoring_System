// RemoteUpgradeModels.cs
// 放远程升级的清单模型、包模型、设备上下文和主窗口桥接接口。

using System.Text.Json.Serialization;

namespace Project;

/// <summary>
/// 远程升级清单。
/// 一般由服务端返回，包含多个发布版本及其固件包列表。
/// </summary>
internal sealed class RemoteManifest
{
    public string? Product { get; set; }

    public string? Description { get; set; }

    public List<RemoteReleaseInfo> Releases { get; set; } = [];
}

/// <summary>
/// 一个可发布的远程版本。
/// </summary>
internal sealed class RemoteReleaseInfo
{
    public string Version { get; set; } = string.Empty;

    public string? Notes { get; set; }

    public DateTimeOffset? PublishedAt { get; set; }

    public List<RemotePackageInfo> Packages { get; set; } = [];

    public override string ToString()
    {
        return string.IsNullOrWhiteSpace(Version) ? "未命名版本" : Version;
    }
}

/// <summary>
/// 一个可下载的远程固件包。
/// </summary>
internal sealed class RemotePackageInfo
{
    public string FileName { get; set; } = "firmware.bin";

    [JsonPropertyName("url")]
    public string DownloadUrl { get; set; } = string.Empty;

    public string? Sha256 { get; set; }

    public long? SizeBytes { get; set; }

    [JsonPropertyName("slot")]
    public string? TargetSlot { get; set; }

    public bool Recommended { get; set; }

    public string DisplayText
    {
        get
        {
            var parts = new List<string> { FileName };
            var slot = this.ParseTargetSlot();
            if (slot != FirmwareSlot.Unknown)
            {
                parts.Add($"目标槽 {slot.ToDisplayText()}");
            }

            if (SizeBytes is > 0)
            {
                parts.Add($"{SizeBytes.Value / 1024d:F1} KB");
            }

            if (Recommended)
            {
                parts.Add("推荐");
            }

            return string.Join(" | ", parts);
        }
    }

    public override string ToString()
    {
        return DisplayText;
    }
}

/// <summary>
/// 远程升级前从主窗口读取到的设备上下文。
/// </summary>
internal readonly record struct RemoteDeviceContext(
    bool IsAvailable,
    string PortName,
    int BaudRate,
    double TimeoutSeconds,
    FirmwareSlot RunningSlot,
    string? ErrorMessage);

/// <summary>
/// 已下载并校验完成的远程固件包。
/// </summary>
internal readonly record struct RemotePreparedPackage(
    string FilePath,
    RemoteReleaseInfo Release,
    RemotePackageInfo Package);

/// <summary>
/// 主窗口为远程升级流程提供的设备桥接接口。
/// </summary>
internal interface IRemoteUpgradeDeviceBridge
{
    Task<RemoteDeviceContext> GetDeviceContextAsync(bool refreshRunningSlot, CancellationToken cancellationToken = default);

    Task RunUpgradeAsync(RemotePreparedPackage package, Action<string> log, CancellationToken cancellationToken = default);
}

/// <summary>
/// 远程升级模型辅助方法。
/// </summary>
internal static class RemoteUpgradeModelExtensions
{
    /// <summary>
    /// 从远程包的槽位文本中解析出内部槽位枚举。
    /// </summary>
    public static FirmwareSlot ParseTargetSlot(this RemotePackageInfo package)
    {
        ArgumentNullException.ThrowIfNull(package);

        if (string.IsNullOrWhiteSpace(package.TargetSlot))
        {
            return FirmwareSlot.Unknown;
        }

        return package.TargetSlot.Trim().ToUpperInvariant() switch
        {
            "A" or "SLOT_A" or "APP_A" => FirmwareSlot.A,
            "B" or "SLOT_B" or "APP_B" => FirmwareSlot.B,
            _ => FirmwareSlot.Unknown
        };
    }
}
