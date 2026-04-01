using System.Collections.ObjectModel;
using System.IO;
using System.Threading;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using OTA.Core;
using OTA.Models;
using OTA.ViewModels.Messages;

namespace OTA.ViewModels;

/// <summary>
/// 主界面的状态与交互入口。
/// 当前阶段把可绑定的界面状态、串口刷新和升级流程从 code-behind 中迁出，
/// 视图层只保留窗口消息、定时器和文件对话框等纯 UI 行为。
/// </summary>
public sealed class MainViewModel : ObservableObject
{
    private const string DefaultObjectsDirectory = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects";
    private const string DefaultSlotAImagePath = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects\App_A.bin";
    private const string DefaultSlotBImagePath = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects\App_B.bin";

    private readonly PortDiscoveryService _portDiscoveryService;
    private readonly LocalUpgradeCoordinator _upgradeCoordinator;
    private readonly SynchronizationContext _uiContext;
    private readonly SemaphoreSlim _runningSlotRefreshLock = new(1, 1);
    private readonly SemaphoreSlim _portListRefreshLock = new(1, 1);
    private readonly HashSet<string> _knownPortNames = new(StringComparer.OrdinalIgnoreCase);

    private UpgradeMode _currentMode = UpgradeMode.Remote;
    private bool _isBusy;
    private bool _isRefreshingPortList;
    private bool _isRefreshingRunningSlot;
    private bool _suppressPortSelectionRefresh;
    private int _consecutiveNoPortRefreshCount;
    private FirmwareSlot _runningSlot = FirmwareSlot.Unknown;
    private string _lastLocalImagePath = DefaultSlotAImagePath;
    private string? _lastAutoSuggestedImagePath = DefaultSlotAImagePath;
    private string? _lastLoggedMessage;
    private int _lastLoggedLineLength;
    private PortOption? _selectedPort;
    private string _baudRateText = "115200";
    private string _timeoutText = "5";
    private string _scriptPath = DefaultSlotAImagePath;
    private string _modeTitle = string.Empty;
    private string _modeDescription = string.Empty;
    private string _modeHint = string.Empty;
    private string _pathLabel = string.Empty;
    private string _startButtonText = string.Empty;
    private string _runningSlotText = "未读取";
    private string _recommendedImageText = "请连接设备后等待自动识别...";
    private string _imageHintText = "镜像槽位：未检查。";
    private string _statusText = "待机";
    private string _logText = string.Empty;

    public MainViewModel(PortDiscoveryService portDiscoveryService, LocalUpgradeCoordinator upgradeCoordinator)
    {
        _portDiscoveryService = portDiscoveryService;
        _upgradeCoordinator = upgradeCoordinator;
        _uiContext = SynchronizationContext.Current ?? new SynchronizationContext();

        SwitchToLocalModeCommand = new RelayCommand(() => ApplyUpgradeMode(UpgradeMode.Local));
        SwitchToRemoteModeCommand = new RelayCommand(() => ApplyUpgradeMode(UpgradeMode.Remote));
        StartUpgradeCommand = new AsyncRelayCommand(StartUpgradeAsync, () => CanStartUpgrade);

        ApplyUpgradeMode(UpgradeMode.Local);
        AppendLog("工具已启动。");
    }

    public ObservableCollection<PortOption> PortOptions { get; } = [];

    public RelayCommand SwitchToLocalModeCommand { get; }

    public RelayCommand SwitchToRemoteModeCommand { get; }

    public AsyncRelayCommand StartUpgradeCommand { get; }

    public event EventHandler<ViewMessage>? ViewMessageRequested;

    public bool IsLocalModeActive => _currentMode == UpgradeMode.Local;

    public bool IsRemoteModeActive => _currentMode == UpgradeMode.Remote;

    public bool CanSwitchMode => !_isBusy;

    public bool CanEditInputs => !_isBusy && IsLocalModeActive;

    public bool CanBrowse => CanEditInputs;

    public bool CanStartUpgrade => !_isBusy && IsLocalModeActive;

    public bool ShouldPollRunningSlot => IsLocalModeActive && !_isBusy && !_isRefreshingRunningSlot;

    public bool ShouldPollPortList => IsLocalModeActive && !_isBusy && !_isRefreshingPortList && !_isRefreshingRunningSlot;

    public bool CanReactToPortSelectionChange => IsLocalModeActive && !_isBusy && !_suppressPortSelectionRefresh;

    public PortOption? SelectedPort
    {
        get => _selectedPort;
        set => SetProperty(ref _selectedPort, value);
    }

    public string BaudRateText
    {
        get => _baudRateText;
        set => SetProperty(ref _baudRateText, value);
    }

    public string TimeoutText
    {
        get => _timeoutText;
        set => SetProperty(ref _timeoutText, value);
    }

    public string ScriptPath
    {
        get => _scriptPath;
        set
        {
            if (SetProperty(ref _scriptPath, value))
            {
                _lastLocalImagePath = value.Trim();
                UpdateImageHintFromPath(_scriptPath, logFailure: false);
            }
        }
    }

    public string ModeTitle
    {
        get => _modeTitle;
        private set => SetProperty(ref _modeTitle, value);
    }

    public string ModeDescription
    {
        get => _modeDescription;
        private set => SetProperty(ref _modeDescription, value);
    }

    public string ModeHint
    {
        get => _modeHint;
        private set => SetProperty(ref _modeHint, value);
    }

    public string PathLabel
    {
        get => _pathLabel;
        private set => SetProperty(ref _pathLabel, value);
    }

    public string StartButtonText
    {
        get => _startButtonText;
        private set => SetProperty(ref _startButtonText, value);
    }

    public string RunningSlotText
    {
        get => _runningSlotText;
        private set => SetProperty(ref _runningSlotText, value);
    }

    public string RecommendedImageText
    {
        get => _recommendedImageText;
        private set => SetProperty(ref _recommendedImageText, value);
    }

    public string ImageHintText
    {
        get => _imageHintText;
        private set => SetProperty(ref _imageHintText, value);
    }

    public string StatusText
    {
        get => _statusText;
        private set => SetProperty(ref _statusText, value);
    }

    public string LogText
    {
        get => _logText;
        private set => SetProperty(ref _logText, value);
    }

    public async Task InitializeAsync()
    {
        await RefreshPortListAsync();
        UpdateImageHintFromPath(ScriptPath, logFailure: false);
        AppendLog("串口参数默认使用 115200, 8N1。");
    }

    public async Task OnPortSelectionChangedAsync()
    {
        if (!CanReactToPortSelectionChange)
        {
            return;
        }

        await RefreshRunningSlotHintFromUiAsync(logSuccess: false, logFailure: false, allowAutoSuggestPath: true);
    }

    public async Task PollPortListAsync()
    {
        if (!ShouldPollPortList)
        {
            return;
        }

        await RefreshPortListAsync();
    }

    public async Task PollRunningSlotAsync()
    {
        if (!ShouldPollRunningSlot)
        {
            return;
        }

        await RefreshRunningSlotHintFromUiAsync(logSuccess: false, logFailure: false, allowAutoSuggestPath: true);
    }

    public void ApplySelectedImagePath(string imagePath)
    {
        ScriptPath = imagePath;
        AppendLog($"已选择 STM32 程序：{imagePath}");
    }

    public async Task RefreshPortListAsync()
    {
        await _portListRefreshLock.WaitAsync();
        _isRefreshingPortList = true;
        NotifyUiStateChanged();

        var currentPort = SelectedPort?.PortName ?? string.Empty;

        try
        {
            var portOptions = await _portDiscoveryService.GetPortOptionsAsync();
            var newlyAddedPortNames = portOptions
                .Select(option => option.PortName)
                .Where(portName => !_knownPortNames.Contains(portName))
                .ToHashSet(StringComparer.OrdinalIgnoreCase);

            PortOption? selectedOption;
            _suppressPortSelectionRefresh = true;
            NotifyUiStateChanged();
            try
            {
                ReplacePortOptions(portOptions);

                selectedOption =
                    portOptions.FirstOrDefault(option => string.Equals(option.PortName, currentPort, StringComparison.OrdinalIgnoreCase)) ??
                    SelectPreferredPort(portOptions, newlyAddedPortNames);

                SelectedPort = selectedOption;

                if (selectedOption is null)
                {
                    SetRunningSlotState(FirmwareSlot.Unknown, "未检测到串口，无法自动识别槽位。", treatUnknownAsUnread: true);
                }
            }
            finally
            {
                _suppressPortSelectionRefresh = false;
                NotifyUiStateChanged();
            }

            if (portOptions.Count == 0)
            {
                _consecutiveNoPortRefreshCount++;
                if (_consecutiveNoPortRefreshCount <= 3)
                {
                    AppendLog("已刷新串口列表：未检测到串口");
                }
            }
            else
            {
                _consecutiveNoPortRefreshCount = 0;
                AppendLog($"已刷新串口列表：{string.Join(", ", portOptions.Select(option => option.DisplayText))}");
            }

            _knownPortNames.Clear();
            foreach (var portName in portOptions.Select(option => option.PortName))
            {
                _knownPortNames.Add(portName);
            }

            if (selectedOption is not null && IsLocalModeActive && !_isBusy)
            {
                await RefreshRunningSlotHintFromUiAsync(logSuccess: false, logFailure: false, allowAutoSuggestPath: true);
            }
        }
        catch (Exception ex)
        {
            _consecutiveNoPortRefreshCount = 0;
            SelectedPort = null;
            AppendLog($"刷新串口失败：{ex.Message}");
            SetRunningSlotState(FirmwareSlot.Unknown, "刷新串口失败，无法自动识别槽位。", treatUnknownAsUnread: true);
        }
        finally
        {
            _isRefreshingPortList = false;
            NotifyUiStateChanged();
            _portListRefreshLock.Release();
        }
    }

    private void ApplyUpgradeMode(UpgradeMode mode)
    {
        if (_currentMode == mode)
        {
            return;
        }

        _currentMode = mode;

        if (mode == UpgradeMode.Remote)
        {
            StatusText = "待机";
            NotifyUiStateChanged();
            return;
        }

        ModeTitle = "STM32 本地 OTA 升级";
        ModeDescription = "流程：发送解锁 HEX -> 等待回包 -> 发送进入 Bootloader HEX -> 由 C# 内置 YMODEM 发送 STM32 程序。";
        ModeHint = "默认优先选择 USB-SERIAL CH340 串口。";
        PathLabel = "STM32程序路径";
        StartButtonText = "进入 Bootloader 并刷机";
        ScriptPath = GetCurrentLocalImagePath();

        if (_runningSlot == FirmwareSlot.Unknown)
        {
            SetRunningSlotState(FirmwareSlot.Unknown, "请连接设备后等待自动识别槽位。", treatUnknownAsUnread: true);
        }
        else
        {
            SetRunningSlotState(_runningSlot, _upgradeCoordinator.BuildSlotRecommendationText(_runningSlot), treatUnknownAsUnread: false);
        }

        UpdateImageHintFromPath(ScriptPath, logFailure: false);
        StatusText = "待机";
        NotifyUiStateChanged();
    }

    private async Task StartUpgradeAsync()
    {
        if (!TryReadLocalSerialSettings(out var serialSettings, out var errorMessage))
        {
            RequestViewMessage("参数错误", errorMessage, ViewMessageSeverity.Warning);
            return;
        }

        await RefreshRunningSlotAsync(serialSettings, logSuccess: true, logFailure: true, allowAutoSuggestPath: true);

        if (!TryReadLocalSettings(out var options, out errorMessage))
        {
            RequestViewMessage("参数错误", errorMessage, ViewMessageSeverity.Warning);
            return;
        }

        if (!_upgradeCoordinator.TryPrepareLocalUpgrade(options, _runningSlot, out var preparation, out errorMessage))
        {
            RequestViewMessage("升级准备失败", errorMessage, ViewMessageSeverity.Warning);
            return;
        }

        SetBusyState(true, "执行中");
        ClearLog();

        foreach (var message in preparation.StartupMessages)
        {
            AppendLog(message);
        }

        try
        {
            await LocalUpgradeService.RunAsync(preparation.Options, AppendLogFromWorker);
            AppendLog("流程结束。");
            StatusText = "完成";
        }
        catch (Exception ex)
        {
            AppendLog($"失败: {ex.Message}");
            StatusText = "失败";
            RequestViewMessage("执行失败", ex.Message, ViewMessageSeverity.Error);
        }
        finally
        {
            SetBusyState(false, StatusText);
        }
    }

    private bool TryReadLocalSerialSettings(out LocalSerialSettings settings, out string errorMessage)
    {
        return _upgradeCoordinator.TryReadSerialSettings(SelectedPort?.PortName ?? string.Empty, BaudRateText, TimeoutText, out settings, out errorMessage);
    }

    private bool TryReadLocalSettings(out LocalUpgradeOptions options, out string errorMessage)
    {
        options = default;
        if (!TryReadLocalSerialSettings(out var serialSettings, out errorMessage))
        {
            return false;
        }

        return _upgradeCoordinator.TryBuildLocalUpgradeOptions(serialSettings, ScriptPath, out options, out errorMessage);
    }

    private async Task<FirmwareSlot> RefreshRunningSlotHintFromUiAsync(bool logSuccess, bool logFailure, bool allowAutoSuggestPath)
    {
        if (!TryGetLocalSerialSettingsForRead(out var serialSettings))
        {
            SetRunningSlotState(FirmwareSlot.Unknown, "未检测到串口，无法自动识别槽位。", treatUnknownAsUnread: true);
            return FirmwareSlot.Unknown;
        }

        return await RefreshRunningSlotAsync(serialSettings, logSuccess, logFailure, allowAutoSuggestPath);
    }

    private async Task<FirmwareSlot> RefreshRunningSlotAsync(
        LocalSerialSettings serialSettings,
        bool logSuccess,
        bool logFailure,
        bool allowAutoSuggestPath)
    {
        await _runningSlotRefreshLock.WaitAsync();
        _isRefreshingRunningSlot = true;
        NotifyUiStateChanged();

        try
        {
            var result = await _upgradeCoordinator.ReadRunningSlotAsync(serialSettings);
            SetRunningSlotState(result.Slot, result.RecommendationText, result.TreatUnknownAsUnread);

            if (result.HasError)
            {
                if (logFailure)
                {
                    AppendLog($"读取当前运行槽失败：{result.ErrorMessage}");
                }

                return FirmwareSlot.Unknown;
            }

            if (result.Slot == FirmwareSlot.Unknown)
            {
                if (logSuccess)
                {
                    AppendLog("已读取当前运行槽：unknown。");
                }

                return FirmwareSlot.Unknown;
            }

            if (allowAutoSuggestPath)
            {
                ApplyRecommendedLocalImagePath(result.Slot);
            }

            if (logSuccess)
            {
                AppendLog($"已读取当前运行槽：{result.Slot.ToDisplayText()}。");
            }

            return result.Slot;
        }
        catch (Exception ex)
        {
            SetRunningSlotState(FirmwareSlot.Unknown, "未读到槽位，请手动选择另一槽镜像。", treatUnknownAsUnread: false);
            if (logFailure)
            {
                AppendLog($"读取当前运行槽失败：{ex.Message}");
            }

            return FirmwareSlot.Unknown;
        }
        finally
        {
            _isRefreshingRunningSlot = false;
            NotifyUiStateChanged();
            _runningSlotRefreshLock.Release();
        }
    }

    private bool TryGetLocalSerialSettingsForRead(out LocalSerialSettings settings)
    {
        settings = default;
        if (SelectedPort is null)
        {
            return false;
        }

        if (!_upgradeCoordinator.TryReadSerialSettings(SelectedPort.PortName, BaudRateText, TimeoutText, out settings, out _))
        {
            settings = new LocalSerialSettings(SelectedPort.PortName, 115200, 5);
        }

        return true;
    }

    private void ApplyRecommendedLocalImagePath(FirmwareSlot runningSlot)
    {
        if (!IsLocalModeActive || runningSlot == FirmwareSlot.Unknown || !ShouldAutoApplyRecommendedPath())
        {
            return;
        }

        var recommendedPath = _upgradeCoordinator.GetRecommendedLocalImagePath(DefaultObjectsDirectory, runningSlot);
        _lastAutoSuggestedImagePath = recommendedPath;
        _lastLocalImagePath = recommendedPath;
        ScriptPath = recommendedPath;
    }

    private bool ShouldAutoApplyRecommendedPath()
    {
        return _upgradeCoordinator.ShouldAutoApplyRecommendedPath(
            ScriptPath.Trim(),
            _lastAutoSuggestedImagePath,
            DefaultSlotAImagePath,
            DefaultSlotBImagePath);
    }

    private string GetCurrentLocalImagePath()
    {
        if (!string.IsNullOrWhiteSpace(_lastLocalImagePath))
        {
            return _lastLocalImagePath;
        }

        var fallbackPath = _upgradeCoordinator.GetRecommendedLocalImagePath(DefaultObjectsDirectory, _runningSlot);
        _lastLocalImagePath = fallbackPath;
        _lastAutoSuggestedImagePath = fallbackPath;
        return fallbackPath;
    }

    private void SetRunningSlotState(FirmwareSlot slot, string recommendationText, bool treatUnknownAsUnread)
    {
        _runningSlot = slot;
        RunningSlotText = slot == FirmwareSlot.Unknown
            ? (treatUnknownAsUnread ? "未读取" : "未知")
            : slot.ToDisplayText();
        RecommendedImageText = recommendationText;
    }

    private void UpdateImageHintFromPath(string imagePath, bool logFailure)
    {
        if (IsRemoteModeActive)
        {
            ImageHintText = "在线升级界面暂不识别镜像槽位。";
            return;
        }

        if (string.IsNullOrWhiteSpace(imagePath))
        {
            ImageHintText = "镜像槽位：未选择程序。";
            return;
        }

        if (!File.Exists(imagePath))
        {
            ImageHintText = "镜像槽位：文件不存在。";
            return;
        }

        if (TryReadImageInfo(imagePath, out var imageInfo, out _, logFailure))
        {
            ImageHintText = _upgradeCoordinator.BuildImageHint(imageInfo);
        }
    }

    private bool TryReadImageInfo(string imagePath, out FirmwareImageInfo imageInfo, out string errorMessage, bool logFailure)
    {
        if (_upgradeCoordinator.TryInspectImage(imagePath, out imageInfo, out errorMessage))
        {
            ImageHintText = _upgradeCoordinator.BuildImageHint(imageInfo);
            return true;
        }

        imageInfo = default;
        ImageHintText = $"镜像槽位：无法识别。{errorMessage.Replace("STM32 程序槽位识别失败：", string.Empty)}";

        if (logFailure)
        {
            AppendLog(errorMessage);
        }

        return false;
    }

    private void ReplacePortOptions(IReadOnlyList<PortOption> portOptions)
    {
        PortOptions.Clear();
        foreach (var portOption in portOptions)
        {
            PortOptions.Add(portOption);
        }
    }

    private void SetBusyState(bool isBusy, string statusText)
    {
        _isBusy = isBusy;
        StatusText = statusText;
        NotifyUiStateChanged();
    }

    private void NotifyUiStateChanged()
    {
        OnPropertyChanged(nameof(IsLocalModeActive));
        OnPropertyChanged(nameof(IsRemoteModeActive));
        OnPropertyChanged(nameof(CanSwitchMode));
        OnPropertyChanged(nameof(CanEditInputs));
        OnPropertyChanged(nameof(CanBrowse));
        OnPropertyChanged(nameof(CanStartUpgrade));
        OnPropertyChanged(nameof(ShouldPollRunningSlot));
        OnPropertyChanged(nameof(ShouldPollPortList));
        OnPropertyChanged(nameof(CanReactToPortSelectionChange));
        StartUpgradeCommand.NotifyCanExecuteChanged();
    }

    private void AppendLogFromWorker(string message)
    {
        _uiContext.Post(_ => AppendLog(message), null);
    }

    private void AppendLog(string message)
    {
        var normalizedMessage = (message ?? string.Empty).TrimEnd('\r', '\n');
        var renderedLine = $"[{DateTime.Now:HH:mm:ss}] {normalizedMessage}{Environment.NewLine}";

        if (string.Equals(_lastLoggedMessage, normalizedMessage, StringComparison.Ordinal) &&
            _lastLoggedLineLength > 0 &&
            LogText.Length >= _lastLoggedLineLength)
        {
            LogText = LogText[..^_lastLoggedLineLength] + renderedLine;
        }
        else
        {
            LogText += renderedLine;
        }

        _lastLoggedMessage = normalizedMessage;
        _lastLoggedLineLength = renderedLine.Length;
    }

    private void ClearLog()
    {
        LogText = string.Empty;
        _lastLoggedMessage = null;
        _lastLoggedLineLength = 0;
    }

    private void RequestViewMessage(string title, string message, ViewMessageSeverity severity)
    {
        var viewMessage = new ViewMessage(title, message, severity);
        _uiContext.Post(_ => ViewMessageRequested?.Invoke(this, viewMessage), null);
    }

    private static PortOption? SelectPreferredPort(IReadOnlyList<PortOption> portOptions, IReadOnlySet<string> newlyAddedPortNames)
    {
        if (portOptions.Count == 0)
        {
            return null;
        }

        var newlyAddedOptions = portOptions
            .Where(option => newlyAddedPortNames.Contains(option.PortName))
            .ToList();

        return newlyAddedOptions.FirstOrDefault(option => option.IsUsbSerial) ??
               newlyAddedOptions.FirstOrDefault(option => option.IsCh340) ??
               newlyAddedOptions.FirstOrDefault() ??
               portOptions.FirstOrDefault(option => option.IsUsbSerial) ??
               portOptions.FirstOrDefault(option => option.IsCh340) ??
               portOptions.FirstOrDefault();
    }

    private enum UpgradeMode
    {
        Local,
        Remote
    }
}
