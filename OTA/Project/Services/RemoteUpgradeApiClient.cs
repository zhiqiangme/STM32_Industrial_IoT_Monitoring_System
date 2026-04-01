// RemoteUpgradeApiClient.cs
// 放远程升级 HTTP 通信逻辑，负责读取清单和下载固件包文件。

using System.IO;
using System.Net.Http;
using System.Text.Json;

namespace Project;

/// <summary>
/// 远程升级接口客户端。
/// </summary>
internal sealed class RemoteUpgradeApiClient
{
    private static readonly HttpClient HttpClient = new();

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        AllowTrailingCommas = true,
        ReadCommentHandling = JsonCommentHandling.Skip
    };

    /// <summary>
    /// 获取远程升级清单。
    /// </summary>
    public async Task<RemoteManifest> GetManifestAsync(Uri manifestUri, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(manifestUri);

        using var response = await HttpClient.GetAsync(manifestUri, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
        response.EnsureSuccessStatusCode();

        await using var stream = await response.Content.ReadAsStreamAsync(cancellationToken);
        var manifest = await JsonSerializer.DeserializeAsync<RemoteManifest>(stream, JsonOptions, cancellationToken);
        if (manifest is null)
        {
            throw new InvalidOperationException("远程升级清单为空或格式无法识别。");
        }

        if (manifest.Releases.Count == 0)
        {
            throw new InvalidOperationException("远程升级清单中没有可用版本。");
        }

        return manifest;
    }

    /// <summary>
    /// 下载远程固件包。
    /// </summary>
    public async Task DownloadPackageAsync(
        Uri packageUri,
        string destinationFilePath,
        IProgress<double>? progress = null,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(packageUri);
        ArgumentException.ThrowIfNullOrWhiteSpace(destinationFilePath);

        var destinationDirectory = Path.GetDirectoryName(destinationFilePath);
        if (string.IsNullOrWhiteSpace(destinationDirectory))
        {
            throw new InvalidOperationException("下载目录无效。");
        }

        Directory.CreateDirectory(destinationDirectory);

        using var response = await HttpClient.GetAsync(packageUri, HttpCompletionOption.ResponseHeadersRead, cancellationToken);
        response.EnsureSuccessStatusCode();

        var totalBytes = response.Content.Headers.ContentLength;

        await using var input = await response.Content.ReadAsStreamAsync(cancellationToken);
        await using var output = File.Create(destinationFilePath);

        var buffer = new byte[81920];
        long receivedBytes = 0;
        while (true)
        {
            var read = await input.ReadAsync(buffer, cancellationToken);
            if (read <= 0)
            {
                break;
            }

            await output.WriteAsync(buffer.AsMemory(0, read), cancellationToken);
            receivedBytes += read;

            if (totalBytes is > 0)
            {
                progress?.Report(receivedBytes * 100d / totalBytes.Value);
            }
        }

        progress?.Report(100);
    }
}
