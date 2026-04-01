// RemotePackageVerifier.cs
// 放远程固件包校验逻辑，负责大小和 SHA256 验证。

using System.IO;
using System.Security.Cryptography;

namespace Project;

/// <summary>
/// 远程升级包校验器。
/// </summary>
internal static class RemotePackageVerifier
{
    /// <summary>
    /// 校验下载后的固件包。
    /// </summary>
    public static void Verify(string filePath, RemotePackageInfo package)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(filePath);
        ArgumentNullException.ThrowIfNull(package);

        if (!File.Exists(filePath))
        {
            throw new FileNotFoundException("下载后的固件包不存在。", filePath);
        }

        var fileInfo = new FileInfo(filePath);
        if (package.SizeBytes is > 0 && fileInfo.Length != package.SizeBytes.Value)
        {
            throw new InvalidOperationException(
                $"固件包大小校验失败，期望 {package.SizeBytes.Value} 字节，实际 {fileInfo.Length} 字节。");
        }

        if (!string.IsNullOrWhiteSpace(package.Sha256))
        {
            using var stream = File.OpenRead(filePath);
            var hash = Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
            if (!string.Equals(hash, package.Sha256.Trim(), StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidOperationException("固件包 SHA256 校验失败。");
            }
        }
    }
}
