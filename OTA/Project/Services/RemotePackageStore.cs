// RemotePackageStore.cs
// 放远程升级包的缓存目录和文件路径管理逻辑。

using System.IO;

namespace Project;

/// <summary>
/// 远程升级包本地存储辅助类。
/// </summary>
internal sealed class RemotePackageStore
{
    /// <summary>
    /// 获取默认下载目录。
    /// </summary>
    public string GetDefaultDirectory()
    {
        return Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "STM32_Mill",
            "OTA",
            "Downloads");
    }

    /// <summary>
    /// 规范化并确保下载目录存在。
    /// </summary>
    public string EnsureDirectory(string? configuredDirectory)
    {
        var directory = string.IsNullOrWhiteSpace(configuredDirectory)
            ? GetDefaultDirectory()
            : configuredDirectory.Trim();

        Directory.CreateDirectory(directory);
        return directory;
    }

    /// <summary>
    /// 为远程固件包生成稳定的缓存文件路径。
    /// </summary>
    public string BuildPackageFilePath(string downloadDirectory, RemoteReleaseInfo release, RemotePackageInfo package)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(downloadDirectory);
        ArgumentNullException.ThrowIfNull(release);
        ArgumentNullException.ThrowIfNull(package);

        var safeVersion = SanitizeFileNamePart(string.IsNullOrWhiteSpace(release.Version) ? "unknown" : release.Version);
        var safeFileName = string.IsNullOrWhiteSpace(package.FileName) ? "firmware.bin" : Path.GetFileName(package.FileName);
        return Path.Combine(downloadDirectory, $"{safeVersion}_{safeFileName}");
    }

    private static string SanitizeFileNamePart(string value)
    {
        var invalidChars = Path.GetInvalidFileNameChars();
        return new string(value.Select(ch => invalidChars.Contains(ch) ? '_' : ch).ToArray());
    }
}
