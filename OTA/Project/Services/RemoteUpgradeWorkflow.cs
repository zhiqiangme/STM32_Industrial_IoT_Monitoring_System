// RemoteUpgradeWorkflow.cs
// 放远程升级业务编排，负责检查更新、选择固件、下载校验并调用本地刷写桥接。

using System.Text.RegularExpressions;

namespace Project;

/// <summary>
/// 远程升级工作流。
/// </summary>
internal sealed class RemoteUpgradeWorkflow
{
    private readonly RemoteUpgradeState _state;
    private readonly RemoteUpgradeApiClient _apiClient;
    private readonly RemotePackageStore _packageStore;
    private readonly IRemoteUpgradeDeviceBridge _deviceBridge;
    private readonly Action<string> _log;
    private Uri? _lastManifestUri;

    public RemoteUpgradeWorkflow(
        RemoteUpgradeState state,
        RemoteUpgradeApiClient apiClient,
        RemotePackageStore packageStore,
        IRemoteUpgradeDeviceBridge deviceBridge,
        Action<string> log)
    {
        _state = state ?? throw new ArgumentNullException(nameof(state));
        _apiClient = apiClient ?? throw new ArgumentNullException(nameof(apiClient));
        _packageStore = packageStore ?? throw new ArgumentNullException(nameof(packageStore));
        _deviceBridge = deviceBridge ?? throw new ArgumentNullException(nameof(deviceBridge));
        _log = log ?? throw new ArgumentNullException(nameof(log));

        if (string.IsNullOrWhiteSpace(_state.DownloadDirectory))
        {
            _state.DownloadDirectory = _packageStore.GetDefaultDirectory();
        }
    }

    /// <summary>
    /// 刷新设备串口和运行槽位信息。
    /// </summary>
    public async Task RefreshDeviceContextAsync(CancellationToken cancellationToken = default)
    {
        _state.IsBusy = true;
        _state.StatusText = "正在同步设备信息...";

        try
        {
            var context = await _deviceBridge.GetDeviceContextAsync(refreshRunningSlot: true, cancellationToken);
            ApplyDeviceContext(context);
            _state.StatusText = "设备信息已同步";
        }
        finally
        {
            _state.IsBusy = false;
        }
    }

    /// <summary>
    /// 读取远程清单并更新推荐固件包。
    /// </summary>
    public async Task CheckForUpdatesAsync(CancellationToken cancellationToken = default)
    {
        if (!Uri.TryCreate(_state.ManifestUrl.Trim(), UriKind.Absolute, out var manifestUri))
        {
            throw new InvalidOperationException("远程清单地址无效，请输入完整的 http 或 https 地址。");
        }

        _state.IsBusy = true;
        _state.StatusText = "正在检查远程版本...";
        _state.ProgressPercent = 0;
        _state.ResetPackages();
        _log($"读取远程升级清单：{manifestUri}");

        try
        {
            var deviceContext = await _deviceBridge.GetDeviceContextAsync(refreshRunningSlot: true, cancellationToken);
            ApplyDeviceContext(deviceContext);

            var manifest = await _apiClient.GetManifestAsync(manifestUri, cancellationToken);
            _lastManifestUri = manifestUri;

            var release = SelectLatestRelease(manifest.Releases);
            if (release.Packages.Count == 0)
            {
                throw new InvalidOperationException($"远程版本 {release.Version} 没有可用固件包。");
            }

            _state.SelectedRelease = release;
            _state.LatestVersionText = release.Version;
            _state.ReleaseNotes = string.IsNullOrWhiteSpace(release.Notes)
                ? "该版本没有发布说明。"
                : release.Notes.Trim();

            foreach (var package in release.Packages)
            {
                _state.AvailablePackages.Add(package);
            }

            _state.SelectedPackage = SelectRecommendedPackage(release.Packages, deviceContext.RunningSlot);
            _state.StatusText = "已获取远程版本信息";

            _log($"发现远程版本：{release.Version}");
            if (_state.SelectedPackage is not null)
            {
                _log($"默认选中固件包：{_state.SelectedPackage.DisplayText}");
            }
        }
        finally
        {
            _state.IsBusy = false;
        }
    }

    /// <summary>
    /// 下载当前选中的固件包。
    /// </summary>
    public async Task DownloadSelectedPackageAsync(CancellationToken cancellationToken = default)
    {
        _state.IsBusy = true;
        _state.StatusText = "正在下载远程固件...";
        _state.ProgressPercent = 0;

        try
        {
            var preparedPackage = await PreparePackageAsync(cancellationToken);
            _state.DownloadedPackagePath = preparedPackage.FilePath;
            _state.StatusText = "固件包已下载并校验";
            _log($"固件已保存到：{preparedPackage.FilePath}");
        }
        finally
        {
            _state.IsBusy = false;
        }
    }

    /// <summary>
    /// 下载选中固件包并直接进入本地刷写流程。
    /// </summary>
    public async Task DownloadAndUpgradeAsync(CancellationToken cancellationToken = default)
    {
        _state.IsBusy = true;
        _state.StatusText = "正在准备远程升级...";
        _state.ProgressPercent = 0;

        try
        {
            var preparedPackage = await PreparePackageAsync(cancellationToken);
            _state.DownloadedPackagePath = preparedPackage.FilePath;
            _state.StatusText = "正在调用本地刷写流程...";
            _log($"开始远程升级，目标文件：{preparedPackage.FilePath}");
            await _deviceBridge.RunUpgradeAsync(preparedPackage, _log, cancellationToken);
            _state.StatusText = "远程升级完成";
        }
        finally
        {
            _state.IsBusy = false;
        }
    }

    private async Task<RemotePreparedPackage> PreparePackageAsync(CancellationToken cancellationToken)
    {
        var release = _state.SelectedRelease ?? throw new InvalidOperationException("请先检查远程版本。");
        var package = _state.SelectedPackage ?? throw new InvalidOperationException("请先选择一个远程固件包。");
        if (_lastManifestUri is null)
        {
            throw new InvalidOperationException("远程清单尚未加载，请先检查更新。");
        }

        var downloadDirectory = _packageStore.EnsureDirectory(_state.DownloadDirectory);
        _state.DownloadDirectory = downloadDirectory;

        var destinationFilePath = _packageStore.BuildPackageFilePath(downloadDirectory, release, package);
        var packageUri = BuildPackageUri(_lastManifestUri, package.DownloadUrl);

        _log($"开始下载固件包：{package.DisplayText}");
        var progress = new Progress<double>(value =>
        {
            _state.ProgressPercent = value;
            _state.StatusText = value >= 100
                ? "正在校验固件包..."
                : $"下载进度 {value:0}%";
        });

        await _apiClient.DownloadPackageAsync(packageUri, destinationFilePath, progress, cancellationToken);
        RemotePackageVerifier.Verify(destinationFilePath, package);
        _log("远程固件包校验通过。");
        _state.ProgressPercent = 100;

        return new RemotePreparedPackage(destinationFilePath, release, package);
    }

    private void ApplyDeviceContext(RemoteDeviceContext context)
    {
        if (!context.IsAvailable)
        {
            _state.DevicePortText = "未配置";
            _state.RunningSlotText = "未读取";
            if (!string.IsNullOrWhiteSpace(context.ErrorMessage))
            {
                _log($"设备信息读取失败：{context.ErrorMessage}");
            }

            return;
        }

        _state.DevicePortText = $"{context.PortName} @ {context.BaudRate}, 超时 {context.TimeoutSeconds:0.###}s";
        _state.RunningSlotText = context.RunningSlot == FirmwareSlot.Unknown
            ? "未知"
            : context.RunningSlot.ToDisplayText();
    }

    private static RemoteReleaseInfo SelectLatestRelease(IReadOnlyList<RemoteReleaseInfo> releases)
    {
        return releases
            .OrderByDescending(release => release.PublishedAt ?? DateTimeOffset.MinValue)
            .ThenByDescending(release => ParseVersionSortKey(release.Version))
            .First();
    }

    private static RemotePackageInfo? SelectRecommendedPackage(IReadOnlyList<RemotePackageInfo> packages, FirmwareSlot runningSlot)
    {
        var targetSlot = runningSlot.GetOpposite();

        return packages.FirstOrDefault(package => package.ParseTargetSlot() == targetSlot) ??
               packages.FirstOrDefault(package => package.Recommended) ??
               packages.FirstOrDefault();
    }

    private static Uri BuildPackageUri(Uri manifestUri, string downloadUrl)
    {
        if (string.IsNullOrWhiteSpace(downloadUrl))
        {
            throw new InvalidOperationException("远程固件包缺少下载地址。");
        }

        return Uri.TryCreate(downloadUrl, UriKind.Absolute, out var absoluteUri)
            ? absoluteUri
            : new Uri(manifestUri, downloadUrl);
    }

    private static Version ParseVersionSortKey(string versionText)
    {
        if (string.IsNullOrWhiteSpace(versionText))
        {
            return new Version(0, 0);
        }

        var match = Regex.Match(versionText, @"\d+(\.\d+){0,3}");
        return match.Success && Version.TryParse(match.Value, out var version)
            ? version
            : new Version(0, 0);
    }
}
