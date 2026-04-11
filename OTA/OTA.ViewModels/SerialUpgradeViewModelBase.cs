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
/// 本地升级与远程升级共用的串口升级页面状态与交互入口。
/// </summary>
public abstract class SerialUpgradeViewModelBase : ObservableObject
{
    private const string DefaultObjectsDirectory = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects";
    private const string DefaultSlotAImagePath = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects\App_A.bin";
    private const string DefaultSlotBImagePath = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects\App_B.bin";

    private readonly PortDiscoveryService _portDiscoveryService;
    private readonly LocalUpgradeCoordinator _upgradeCoordinator;
    private readonly Func<SerialUpgradePreferenceSnapshot> _loadPreferences;
    private readonly Action<string?, string?, string?> _savePreferences;
    private readonly SynchronizationContext _uiContext;
    private readonly SemaphoreSlim _runningSlotRefreshLock = new(1, 1);
    private readonly SemaphoreSlim _portListRefreshLock = new(1, 1);
    private readonly HashSet<string> _knownPortNames = new(StringComparer.OrdinalIgnoreCase);

    private bool _isApplyingPreferences;
    private bool _isBusy;
    private bool _isRefreshingPortList;
    private bool _isRefreshingRunningSlot;
    private bool _suppressPortSelectionRefresh;
    private int _consecutiveNoPortRefreshCount;
    private FirmwareSlot _runningSlot = FirmwareSlot.Unknown;
    private string? _lastAutoSuggestedImagePath = DefaultSlotAImagePath;
    private string? _lastLoggedMessage;
    private bool _lastLoggedWasProgress;
    private int _lastLoggedLineLength;
    private string? _preferredPortName;
    private PortOption? _selectedPort;
    private string _baudRateText;
    private string _timeoutText;
    private string _scriptPath = DefaultSlotAImagePath;
    private string _runningSlotText = "未读取";
    private string _recommendedImageText = "请连接设备后等待自动识别...";
    private string _imageHintText = "镜像槽位：未检查。";
    private string _statusText = "待机";
    private string _logText = string.Empty;

    /// <summary>
    /// 初始化共享串口升级页面的基础状态、模式配置与偏好读写委托。
    /// </summary>
    protected SerialUpgradeViewModelBase(
        PortDiscoveryService portDiscoveryService,
        LocalUpgradeCoordinator upgradeCoordinator,
        SerialUpgradeModeProfile modeProfile,
        Func<SerialUpgradePreferenceSnapshot> loadPreferences,
        Action<string?, string?, string?> savePreferences)
    {
        _portDiscoveryService = portDiscoveryService;
        _upgradeCoordinator = upgradeCoordinator;
        ModeProfile = modeProfile;
        _loadPreferences = loadPreferences;
        _savePreferences = savePreferences;
        _uiContext = SynchronizationContext.Current ?? new SynchronizationContext();

        _baudRateText = modeProfile.DefaultBaudRateText;
        _timeoutText = modeProfile.DefaultTimeoutText;

        StartUpgradeCommand = new AsyncRelayCommand(StartUpgradeAsync, () => CanStartUpgrade);
        AppendLog("工具已启动。");
    }

    protected SerialUpgradeModeProfile ModeProfile { get; }

    public ObservableCollection<PortOption> PortOptions { get; } = [];

    public AsyncRelayCommand StartUpgradeCommand { get; }

    public event EventHandler<ViewMessage>? ViewMessageRequested;

    public string ModeTitle => ModeProfile.ModeTitle;

    public string ModeDescription => ModeProfile.ModeDescription;

    public string ModeHint => ModeProfile.ModeHint;

    public string PathLabel => ModeProfile.PathLabel;

    public string StartButtonText => ModeProfile.StartButtonText;

    public string BrowseDialogTitle => ModeProfile.BrowseDialogTitle;

    public bool CanEditInputs => !_isBusy;

    public bool CanBrowse => CanEditInputs;

    public bool CanStartUpgrade => !_isBusy;

    public bool ShouldPollRunningSlot => !_isBusy && !_isRefreshingRunningSlot;

    public bool ShouldPollPortList => !_isBusy && !_isRefreshingPortList && !_isRefreshingRunningSlot;

    public bool CanReactToPortSelectionChange => !_isBusy && !_suppressPortSelectionRefresh;

    public PortOption? SelectedPort
    {
        get => _selectedPort;
        set
        {
            if (SetProperty(ref _selectedPort, value) && value is not null)
            {
                _preferredPortName = value.PortName;
                PersistUpgradePreferences();
            }
        }
    }

    public string BaudRateText
    {
        get => _baudRateText;
        set
        {
            if (SetProperty(ref _baudRateText, value))
            {
                PersistUpgradePreferences();
            }
        }
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
                UpdateImageHintFromPath(_scriptPath, logFailure: false);
                PersistUpgradePreferences();
            }
        }
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

    /// <summary>
    /// 初始化页面状态并首次刷新串口与镜像提示。
    /// </summary>
    public async Task InitializeAsync()
    {
        ApplySavedPreferences();
        await RefreshPortListAsync();
        UpdateImageHintFromPath(ScriptPath, logFailure: false);
        AppendLog($"串口参数默认使用 {BaudRateText}, 8N1。");
    }

    /// <summary>
    /// 响应串口切换，刷新当前运行槽位提示。
    /// </summary>
    public async Task OnPortSelectionChangedAsync()
    {
        if (!CanReactToPortSelectionChange)
        {
            return;
        }

        await RefreshRunningSlotHintFromUiAsync(logSuccess: false, logFailure: false, allowAutoSuggestPath: true);
    }

    /// <summary>
    /// 在空闲轮询场景下刷新串口列表。
    /// </summary>
    public async Task PollPortListAsync()
    {
        if (!ShouldPollPortList)
        {
            return;
        }

        await RefreshPortListAsync();
    }

    /// <summary>
    /// 在空闲轮询场景下刷新当前运行槽位。
    /// </summary>
    public async Task PollRunningSlotAsync()
    {
        if (!ShouldPollRunningSlot)
        {
            return;
        }

        await RefreshRunningSlotHintFromUiAsync(logSuccess: false, logFailure: false, allowAutoSuggestPath: true);
    }

    /// <summary>
    /// 根据用户选择的目录自动解析要升级的镜像文件。
    /// </summary>
    public void ApplySelectedImageDirectory(string imageDirectory)
    {
        if (!_upgradeCoordinator.TryResolveLocalImagePathForDirectory(
                imageDirectory,
                _runningSlot,
                ScriptPath,
                out var selectedImagePath,
                out var errorMessage))
        {
            AppendLog($"程序目录检查失败：{errorMessage}");
            RequestViewMessage("缺少升级文件", errorMessage, ViewMessageSeverity.Warning);
            return;
        }

        ScriptPath = selectedImagePath;
        AppendLog($"已选择 STM32 程序目录：{imageDirectory}");
        AppendLog($"已自动定位 STM32 程序：{selectedImagePath}");
    }

    /// <summary>
    /// 刷新当前可用串口列表，并按模式策略选择默认串口。
    /// </summary>
    public async Task RefreshPortListAsync()
    {
        await _portListRefreshLock.WaitAsync();
        _isRefreshingPortList = true;
        NotifyUiStateChanged();

        var currentPort = SelectedPort?.PortName ?? _preferredPortName ?? string.Empty;

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

            if (selectedOption is not null && !_isBusy)
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

    /// <summary>
    /// 由子类覆写串口默认选择策略。
    /// </summary>
    protected virtual PortOption? SelectPreferredPort(IReadOnlyList<PortOption> portOptions, IReadOnlySet<string> newlyAddedPortNames)
    {
        return SerialUpgradePortSelector.SelectLocalPreferredPort(portOptions, newlyAddedPortNames);
    }

    /// <summary>
    /// 执行完整升级流程：校验参数、读取槽位、准备镜像并启动传输。
    /// </summary>
    private async Task StartUpgradeAsync()
    {
        if (!TryReadLocalSerialSettings(out var serialSettings, out var errorMessage))
        {
            RequestViewMessage("参数错误", errorMessage, ViewMessageSeverity.Warning);
            return;
        }

        if (!_upgradeCoordinator.TryValidatePortAvailability(serialSettings, ModeProfile.DisconnectedPortMessageTemplate, out errorMessage))
        {
            AppendLog($"串口检查失败：{errorMessage}");
            RequestViewMessage("串口不可用", errorMessage, ViewMessageSeverity.Warning);
            return;
        }

        await RefreshRunningSlotAsync(serialSettings, logSuccess: true, logFailure: true, allowAutoSuggestPath: true);

        if (!TryReadLocalSettings(out var options, out errorMessage))
        {
            RequestViewMessage("参数错误", errorMessage, ViewMessageSeverity.Warning);
            return;
        }

        if (!_upgradeCoordinator.TryPrepareLocalUpgrade(options, _runningSlot, ModeProfile.PreparationModeName, out var preparation, out errorMessage))
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

    /// <summary>
    /// 从界面字段读取并校验串口参数。
    /// </summary>
    private bool TryReadLocalSerialSettings(out LocalSerialSettings settings, out string errorMessage)
    {
        return _upgradeCoordinator.TryReadSerialSettings(SelectedPort?.PortName ?? string.Empty, BaudRateText, TimeoutText, out settings, out errorMessage);
    }

    /// <summary>
    /// 从界面字段读取并校验完整升级参数。
    /// </summary>
    private bool TryReadLocalSettings(out LocalUpgradeOptions options, out string errorMessage)
    {
        options = default;
        if (!TryReadLocalSerialSettings(out var serialSettings, out errorMessage))
        {
            return false;
        }

        return _upgradeCoordinator.TryBuildLocalUpgradeOptions(serialSettings, ScriptPath, out options, out errorMessage);
    }

    /// <summary>
    /// 应用持久化偏好到当前页面状态。
    /// </summary>
    private void ApplySavedPreferences()
    {
        var preferences = _loadPreferences();

        _isApplyingPreferences = true;
        try
        {
            if (!string.IsNullOrWhiteSpace(preferences.BaudRateText))
            {
                BaudRateText = preferences.BaudRateText;
            }

            if (!string.IsNullOrWhiteSpace(preferences.LastFirmwarePath))
            {
                ScriptPath = preferences.LastFirmwarePath;
            }

            if (!string.IsNullOrWhiteSpace(preferences.LastPortName))
            {
                _preferredPortName = preferences.LastPortName.Trim();
            }
        }
        finally
        {
            _isApplyingPreferences = false;
        }
    }

    /// <summary>
    /// 使用当前界面串口参数刷新槽位提示。
    /// </summary>
    private async Task<FirmwareSlot> RefreshRunningSlotHintFromUiAsync(bool logSuccess, bool logFailure, bool allowAutoSuggestPath)
    {
        if (!TryGetLocalSerialSettingsForRead(out var serialSettings))
        {
            SetRunningSlotState(FirmwareSlot.Unknown, "未检测到串口，无法自动识别槽位。", treatUnknownAsUnread: true);
            return FirmwareSlot.Unknown;
        }

        return await RefreshRunningSlotAsync(serialSettings, logSuccess, logFailure, allowAutoSuggestPath);
    }

    /// <summary>
    /// 使用指定串口设置读取当前运行槽位，并在需要时自动推荐镜像。
    /// </summary>
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

    /// <summary>
    /// 为只读槽位操作构造一组可用的串口参数。
    /// </summary>
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

    /// <summary>
    /// 根据当前运行槽位自动切换到推荐镜像路径。
    /// </summary>
    private void ApplyRecommendedLocalImagePath(FirmwareSlot runningSlot)
    {
        if (runningSlot == FirmwareSlot.Unknown || !ShouldAutoApplyRecommendedPath())
        {
            return;
        }

        var recommendedPath = _upgradeCoordinator.GetRecommendedLocalImagePath(DefaultObjectsDirectory, runningSlot);
        _lastAutoSuggestedImagePath = recommendedPath;
        ScriptPath = recommendedPath;
    }

    /// <summary>
    /// 判断当前镜像路径是否仍允许被自动推荐结果覆盖。
    /// </summary>
    private bool ShouldAutoApplyRecommendedPath()
    {
        return _upgradeCoordinator.ShouldAutoApplyRecommendedPath(
            ScriptPath.Trim(),
            _lastAutoSuggestedImagePath,
            DefaultSlotAImagePath,
            DefaultSlotBImagePath);
    }

    /// <summary>
    /// 更新槽位展示文字和推荐镜像提示。
    /// </summary>
    private void SetRunningSlotState(FirmwareSlot slot, string recommendationText, bool treatUnknownAsUnread)
    {
        _runningSlot = slot;
        RunningSlotText = slot == FirmwareSlot.Unknown
            ? (treatUnknownAsUnread ? "未读取" : "未知")
            : slot.ToDisplayText();
        RecommendedImageText = recommendationText;
    }

    /// <summary>
    /// 根据当前镜像路径刷新槽位识别提示。
    /// </summary>
    private void UpdateImageHintFromPath(string imagePath, bool logFailure)
    {
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

    /// <summary>
    /// 读取镜像头并生成识别结果，同时在失败时更新提示文本。
    /// </summary>
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

    /// <summary>
    /// 用新的串口列表替换当前下拉框数据源。
    /// </summary>
    private void ReplacePortOptions(IReadOnlyList<PortOption> portOptions)
    {
        PortOptions.Clear();
        foreach (var portOption in portOptions)
        {
            PortOptions.Add(portOption);
        }
    }

    /// <summary>
    /// 更新忙闲状态以及界面上的执行状态文本。
    /// </summary>
    private void SetBusyState(bool isBusy, string statusText)
    {
        _isBusy = isBusy;
        StatusText = statusText;
        NotifyUiStateChanged();
    }

    /// <summary>
    /// 把当前页面的关键输入保存到偏好文件中。
    /// </summary>
    private void PersistUpgradePreferences()
    {
        if (_isApplyingPreferences)
        {
            return;
        }

        _savePreferences(
            ScriptPath,
            _preferredPortName,
            BaudRateText);
    }

    /// <summary>
    /// 通知界面刷新所有依赖忙闲状态的绑定属性。
    /// </summary>
    private void NotifyUiStateChanged()
    {
        OnPropertyChanged(nameof(CanEditInputs));
        OnPropertyChanged(nameof(CanBrowse));
        OnPropertyChanged(nameof(CanStartUpgrade));
        OnPropertyChanged(nameof(ShouldPollRunningSlot));
        OnPropertyChanged(nameof(ShouldPollPortList));
        OnPropertyChanged(nameof(CanReactToPortSelectionChange));
        StartUpgradeCommand.NotifyCanExecuteChanged();
    }

    /// <summary>
    /// 把后台线程日志切回 UI 线程追加到界面。
    /// </summary>
    private void AppendLogFromWorker(string message)
    {
        _uiContext.Post(_ => AppendLog(message), null);
    }

    /// <summary>
    /// 追加一行日志；连续进度日志会覆盖上一条进度，避免刷屏。
    /// </summary>
    private void AppendLog(string message)
    {
        var normalizedMessage = (message ?? string.Empty).TrimEnd('\r', '\n');
        var renderedLine = $"[{DateTime.Now:HH:mm:ss}] {normalizedMessage}{Environment.NewLine}";
        var isProgressMessage = IsProgressMessage(normalizedMessage);

        if (((string.Equals(_lastLoggedMessage, normalizedMessage, StringComparison.Ordinal)) ||
             (_lastLoggedWasProgress && isProgressMessage)) &&
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
        _lastLoggedWasProgress = isProgressMessage;
        _lastLoggedLineLength = renderedLine.Length;
    }

    /// <summary>
    /// 清空日志并重置上一条日志跟踪状态。
    /// </summary>
    private void ClearLog()
    {
        LogText = string.Empty;
        _lastLoggedMessage = null;
        _lastLoggedWasProgress = false;
        _lastLoggedLineLength = 0;
    }

    /// <summary>
    /// 判断当前日志是否属于可覆盖的进度输出。
    /// </summary>
    private static bool IsProgressMessage(string message)
    {
        return message.StartsWith("DATA:", StringComparison.Ordinal);
    }

    /// <summary>
    /// 向视图层发出提示消息请求，由界面弹框展示。
    /// </summary>
    private void RequestViewMessage(string title, string message, ViewMessageSeverity severity)
    {
        var viewMessage = new ViewMessage(title, message, severity);
        _uiContext.Post(_ => ViewMessageRequested?.Invoke(this, viewMessage), null);
    }
}
