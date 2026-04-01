// RemoteUpgradeState.cs
// 放远程升级页面状态，集中管理界面绑定字段、进度、日志和按钮可用性。

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Project;

/// <summary>
/// 远程升级界面状态。
/// 用于和 UserControl 做简单数据绑定。
/// </summary>
internal sealed class RemoteUpgradeState : INotifyPropertyChanged
{
    private string _manifestUrl = string.Empty;
    private string _downloadDirectory = string.Empty;
    private string _devicePortText = "未配置";
    private string _runningSlotText = "未读取";
    private string _latestVersionText = "未检查";
    private string _releaseNotes = "等待检查更新。";
    private string _statusText = "待机";
    private string _downloadedPackagePath = string.Empty;
    private string _logText = string.Empty;
    private double _progressPercent;
    private bool _isBusy;
    private RemotePackageInfo? _selectedPackage;
    private RemoteReleaseInfo? _selectedRelease;

    public event PropertyChangedEventHandler? PropertyChanged;

    public ObservableCollection<RemotePackageInfo> AvailablePackages { get; } = [];

    public string ManifestUrl
    {
        get => _manifestUrl;
        set
        {
            if (SetField(ref _manifestUrl, value))
            {
                NotifyCommandStateChanged();
            }
        }
    }

    public string DownloadDirectory
    {
        get => _downloadDirectory;
        set => SetField(ref _downloadDirectory, value);
    }

    public string DevicePortText
    {
        get => _devicePortText;
        set => SetField(ref _devicePortText, value);
    }

    public string RunningSlotText
    {
        get => _runningSlotText;
        set => SetField(ref _runningSlotText, value);
    }

    public string LatestVersionText
    {
        get => _latestVersionText;
        set => SetField(ref _latestVersionText, value);
    }

    public string ReleaseNotes
    {
        get => _releaseNotes;
        set => SetField(ref _releaseNotes, value);
    }

    public string StatusText
    {
        get => _statusText;
        set => SetField(ref _statusText, value);
    }

    public string DownloadedPackagePath
    {
        get => _downloadedPackagePath;
        set => SetField(ref _downloadedPackagePath, value);
    }

    public string LogText
    {
        get => _logText;
        private set => SetField(ref _logText, value);
    }

    public double ProgressPercent
    {
        get => _progressPercent;
        set => SetField(ref _progressPercent, value);
    }

    public bool IsBusy
    {
        get => _isBusy;
        set
        {
            if (SetField(ref _isBusy, value))
            {
                NotifyCommandStateChanged();
            }
        }
    }

    public RemotePackageInfo? SelectedPackage
    {
        get => _selectedPackage;
        set
        {
            if (SetField(ref _selectedPackage, value))
            {
                NotifyCommandStateChanged();
            }
        }
    }

    public RemoteReleaseInfo? SelectedRelease
    {
        get => _selectedRelease;
        set => SetField(ref _selectedRelease, value);
    }

    public bool CanCheckUpdates => !IsBusy && !string.IsNullOrWhiteSpace(ManifestUrl);

    public bool CanRefreshDevice => !IsBusy;

    public bool CanDownloadSelectedPackage => !IsBusy && SelectedPackage is not null;

    public bool CanStartUpgrade => !IsBusy && SelectedPackage is not null;

    public void AppendLog(string message)
    {
        var renderedLine = $"[{DateTime.Now:HH:mm:ss}] {(message ?? string.Empty).TrimEnd('\r', '\n')}{Environment.NewLine}";
        LogText += renderedLine;
    }

    public void ClearLog()
    {
        LogText = string.Empty;
    }

    public void ResetPackages()
    {
        AvailablePackages.Clear();
        SelectedPackage = null;
        SelectedRelease = null;
        DownloadedPackagePath = string.Empty;
        LatestVersionText = "未检查";
        ReleaseNotes = "等待检查更新。";
        ProgressPercent = 0;
    }

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        return true;
    }

    private void NotifyCommandStateChanged()
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(CanRefreshDevice)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(CanCheckUpdates)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(CanDownloadSelectedPackage)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(CanStartUpgrade)));
    }
}
