using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using Microsoft.Win32;

namespace Project;

public partial class MainWindow : Window
{
    private const string DefaultObjectsDirectory = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects";
    private const string DefaultSlotAImagePath = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects\App_A.bin";
    private const string DefaultSlotBImagePath = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects\App_B.bin";
    private const string RemoteScriptPath = @"D:\Project\STM32_Mill\stm32_remote_upgrade.py";

    private UpgradeMode _currentMode = UpgradeMode.Local;
    private bool _isBusy;
    private bool _isRefreshingRunningSlot;
    private bool _suppressPortSelectionRefresh;
    private FirmwareSlot _runningSlot = FirmwareSlot.Unknown;
    private string _lastLocalImagePath = DefaultSlotAImagePath;
    private string _lastRemoteScriptPath = RemoteScriptPath;
    private string? _lastAutoSuggestedImagePath = DefaultSlotAImagePath;
    private readonly SemaphoreSlim _runningSlotRefreshLock = new(1, 1);
    private readonly DispatcherTimer _idleRunningSlotRefreshTimer;

    public MainWindow()
    {
        InitializeComponent();
        _idleRunningSlotRefreshTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromSeconds(3)
        };
        _idleRunningSlotRefreshTimer.Tick += IdleRunningSlotRefreshTimer_OnTick;
        ApplyUpgradeMode(UpgradeMode.Local);
        Loaded += MainWindow_OnLoaded;
        AppendLog("工具已启动。");
    }

    private async void MainWindow_OnLoaded(object sender, RoutedEventArgs e)
    {
        await RefreshPortListAsync();
        UpdateImageHintFromPath(ScriptPathTextBox.Text, logFailure: false);
        AppendLog("串口参数默认使用 115200, 8N1。");
    }

    private async void StartUpgradeButton_OnClick(object sender, RoutedEventArgs e)
    {
        if (_currentMode == UpgradeMode.Remote)
        {
            MessageBox.Show(this, "在线升级脚本暂未接入，当前只保留界面。", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        if (!TryReadLocalSerialSettings(out var portName, out var baudRate, out var timeoutSeconds, out var errorMessage))
        {
            MessageBox.Show(this, errorMessage, "参数错误", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        await RefreshRunningSlotAsync(portName, baudRate, timeoutSeconds, logSuccess: true, logFailure: true, allowAutoSuggestPath: true);

        if (!TryReadLocalSettings(out var options, out errorMessage))
        {
            MessageBox.Show(this, errorMessage, "参数错误", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        if (!TryReadImageInfo(options.ImagePath, out var imageInfo, out errorMessage, logFailure: true))
        {
            MessageBox.Show(this, errorMessage, "镜像错误", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        if (_runningSlot != FirmwareSlot.Unknown && imageInfo.DetectedSlot == _runningSlot)
        {
            var recommendedFile = UpgradeAbSupport.GetRecommendedFileName(_runningSlot);
            errorMessage = $"设备当前运行槽位为 {_runningSlot.ToDisplayText()}，不能继续发送同槽镜像。请改选 {recommendedFile}。";
            MessageBox.Show(this, errorMessage, "槽位错误", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        SetBusyState(true, "执行中");
        LogTextBox.Clear();
        AppendLog("准备升级，模式 本地升级。");
        if (_runningSlot == FirmwareSlot.Unknown)
        {
            AppendLog("警告: 未读取到当前运行槽位，将按所选镜像继续升级。");
        }
        else
        {
            AppendLog($"当前运行槽：{_runningSlot.ToDisplayText()}。");
            AppendLog($"推荐镜像：{UpgradeAbSupport.GetRecommendedFileName(_runningSlot)}。");
        }

        AppendLog($"所选镜像槽：{imageInfo.DetectedSlot.ToDisplayText()}。");
        AppendLog($"镜像复位向量：0x{imageInfo.ResetHandler:X8}。");

        try
        {
            await LocalUpgradeService.RunAsync(options, AppendLogFromWorker);
            AppendLog("流程结束。");
            StatusTextBlock.Text = "完成";
        }
        catch (Exception ex)
        {
            AppendLog($"失败: {ex.Message}");
            StatusTextBlock.Text = "失败";
            MessageBox.Show(this, ex.Message, "执行失败", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        finally
        {
            SetBusyState(false, StatusTextBlock.Text);
        }
    }

    private async void RefreshPortsButton_OnClick(object sender, RoutedEventArgs e)
    {
        await RefreshPortListAsync();
    }

    private void BrowseScriptButton_OnClick(object sender, RoutedEventArgs e)
    {
        var currentDirectory = Path.GetDirectoryName(ScriptPathTextBox.Text);
        var dialog = new OpenFileDialog
        {
            InitialDirectory = !string.IsNullOrWhiteSpace(currentDirectory) && Directory.Exists(currentDirectory)
                ? currentDirectory
                : @"D:\Project\STM32_Mill",
            FileName = Path.GetFileName(ScriptPathTextBox.Text)
        };

        if (_currentMode == UpgradeMode.Local)
        {
            dialog.Title = "选择 STM32 程序文件";
            dialog.Filter = "BIN 文件 (*.bin)|*.bin|所有文件 (*.*)|*.*";
        }
        else
        {
            dialog.Title = "选择在线升级脚本";
            dialog.Filter = "Python 脚本 (*.py)|*.py|所有文件 (*.*)|*.*";
        }

        if (dialog.ShowDialog(this) == true)
        {
            ScriptPathTextBox.Text = dialog.FileName;
            AppendLog(_currentMode == UpgradeMode.Local
                ? $"已选择 STM32 程序：{dialog.FileName}"
                : $"已选择在线脚本：{dialog.FileName}");
        }
    }

    private void LocalModeButton_OnClick(object sender, RoutedEventArgs e)
    {
        ApplyUpgradeMode(UpgradeMode.Local);
    }

    private void RemoteModeButton_OnClick(object sender, RoutedEventArgs e)
    {
        ApplyUpgradeMode(UpgradeMode.Remote);
    }

    private async void PortComboBox_OnSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (PortComboBox.SelectedItem is PortOption option)
        {
            PortComboBox.Text = option.PortName;
        }

        if (_currentMode == UpgradeMode.Local && !_isBusy && !_suppressPortSelectionRefresh)
        {
            await RefreshRunningSlotHintFromUiAsync(logSuccess: false, logFailure: false, allowAutoSuggestPath: true);
        }
    }

    private async void IdleRunningSlotRefreshTimer_OnTick(object? sender, EventArgs e)
    {
        if (_currentMode != UpgradeMode.Local || _isBusy || _isRefreshingRunningSlot)
        {
            return;
        }

        await RefreshRunningSlotHintFromUiAsync(logSuccess: false, logFailure: false, allowAutoSuggestPath: true);
    }

    private void ScriptPathTextBox_OnTextChanged(object sender, TextChangedEventArgs e)
    {
        if (_currentMode == UpgradeMode.Local)
        {
            _lastLocalImagePath = ScriptPathTextBox.Text.Trim();
            UpdateImageHintFromPath(ScriptPathTextBox.Text, logFailure: false);
            return;
        }

        _lastRemoteScriptPath = ScriptPathTextBox.Text.Trim();
    }

    private bool TryReadLocalSerialSettings(out string portName, out int baudRate, out double timeoutSeconds, out string errorMessage)
    {
        portName = GetCurrentPortName();
        baudRate = 0;
        timeoutSeconds = 0;

        if (string.IsNullOrWhiteSpace(portName))
        {
            errorMessage = "请先选择串口号。";
            return false;
        }

        if (!int.TryParse(BaudRateTextBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out baudRate) || baudRate <= 0)
        {
            errorMessage = "波特率必须是正整数。";
            return false;
        }

        if (!double.TryParse(TimeoutTextBox.Text.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out timeoutSeconds) || timeoutSeconds <= 0)
        {
            errorMessage = "超时必须是大于 0 的数字。";
            return false;
        }

        errorMessage = string.Empty;
        return true;
    }

    private bool TryReadLocalSettings(out LocalUpgradeOptions options, out string errorMessage)
    {
        options = default;

        if (!TryReadLocalSerialSettings(out var portName, out var baudRate, out var timeoutSeconds, out errorMessage))
        {
            return false;
        }

        var imagePath = ScriptPathTextBox.Text.Trim();
        if (string.IsNullOrWhiteSpace(imagePath))
        {
            errorMessage = "STM32 程序路径不能为空。";
            return false;
        }

        if (!File.Exists(imagePath))
        {
            errorMessage = $"STM32 程序不存在：{imagePath}";
            return false;
        }

        options = new LocalUpgradeOptions(portName, baudRate, timeoutSeconds, imagePath);
        errorMessage = string.Empty;
        return true;
    }

    private async Task RefreshPortListAsync()
    {
        var currentPort = GetCurrentPortName();

        try
        {
            var lines = await RunPowerShellAsync(
                """
                $portDescriptions = @{}
                Get-CimInstance Win32_PnPEntity | ForEach-Object {
                    if ($_.Name -match '\((COM\d+)\)') {
                        $portDescriptions[$Matches[1].ToUpperInvariant()] = $_.Name
                    }
                }

                [System.IO.Ports.SerialPort]::GetPortNames() |
                    Sort-Object |
                    ForEach-Object {
                        $portName = $_.ToUpperInvariant()
                        $displayName = $portDescriptions[$portName]
                        if ([string]::IsNullOrWhiteSpace($displayName)) {
                            $displayName = $portName
                        }

                        "{0}`t{1}" -f $portName, $displayName
                    }
                """,
                logOutput: false);

            var portOptions = lines
                .Select(ParsePortOption)
                .Where(option => option is not null)
                .Cast<PortOption>()
                .ToList();

            PortOption? selectedOption;
            _suppressPortSelectionRefresh = true;
            try
            {
                PortComboBox.ItemsSource = portOptions;

                selectedOption =
                    portOptions.FirstOrDefault(option => string.Equals(option.PortName, currentPort, StringComparison.OrdinalIgnoreCase)) ??
                    portOptions.FirstOrDefault(option => option.IsCh340) ??
                    portOptions.FirstOrDefault(option => string.Equals(option.PortName, "COM12", StringComparison.OrdinalIgnoreCase)) ??
                    portOptions.FirstOrDefault();

                if (selectedOption is not null)
                {
                    PortComboBox.SelectedItem = selectedOption;
                    PortComboBox.Text = selectedOption.PortName;
                }
                else
                {
                    PortComboBox.SelectedItem = null;
                    PortComboBox.Text = string.IsNullOrWhiteSpace(currentPort) ? "COM12" : currentPort;
                    SetRunningSlotUi(FirmwareSlot.Unknown, "未检测到串口，无法自动识别槽位。", treatUnknownAsUnread: true);
                }
            }
            finally
            {
                _suppressPortSelectionRefresh = false;
            }

            AppendLog($"已刷新串口列表：{(portOptions.Count == 0 ? "未检测到串口" : string.Join(", ", portOptions.Select(option => option.DisplayName)))}");

            if (selectedOption is not null && _currentMode == UpgradeMode.Local && !_isBusy)
            {
                await RefreshRunningSlotHintFromUiAsync(logSuccess: false, logFailure: false, allowAutoSuggestPath: true);
            }
        }
        catch (Exception ex)
        {
            PortComboBox.Text = string.IsNullOrWhiteSpace(currentPort) ? "COM12" : currentPort;
            AppendLog($"刷新串口失败：{ex.Message}");
            SetRunningSlotUi(FirmwareSlot.Unknown, "刷新串口失败，无法自动识别槽位。", treatUnknownAsUnread: true);
        }
    }

    private async Task<IReadOnlyList<string>> RunPowerShellAsync(string script, bool logOutput, string? workingDirectory = null)
    {
        var wrappedScript = $$"""
            $ProgressPreference = 'SilentlyContinue'
            $ErrorActionPreference = 'Stop'
            try {
            {{script}}
            }
            catch {
                [Console]::Error.WriteLine($_.Exception.Message)
                exit 1
            }
            """;

        var encodedCommand = Convert.ToBase64String(Encoding.Unicode.GetBytes(wrappedScript));
        var startInfo = new ProcessStartInfo
        {
            FileName = "powershell.exe",
            Arguments = $"-NoProfile -NonInteractive -OutputFormat Text -ExecutionPolicy Bypass -EncodedCommand {encodedCommand}",
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };

        if (!string.IsNullOrWhiteSpace(workingDirectory))
        {
            startInfo.WorkingDirectory = workingDirectory;
        }

        using var process = new Process { StartInfo = startInfo };
        var outputLines = new List<string>();
        var errorLines = new List<string>();

        process.OutputDataReceived += (_, args) =>
        {
            if (string.IsNullOrWhiteSpace(args.Data))
            {
                return;
            }

            outputLines.Add(args.Data);
            if (logOutput)
            {
                Dispatcher.Invoke(() => AppendLog(args.Data));
            }
        };

        process.ErrorDataReceived += (_, args) =>
        {
            if (string.IsNullOrWhiteSpace(args.Data))
            {
                return;
            }

            errorLines.Add(args.Data);
            if (logOutput)
            {
                Dispatcher.Invoke(() => AppendLog($"ERR: {args.Data}"));
            }
        };

        if (!process.Start())
        {
            throw new InvalidOperationException("PowerShell 进程启动失败。");
        }

        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        await process.WaitForExitAsync();

        if (process.ExitCode != 0)
        {
            var detail = errorLines.Count > 0 ? string.Join(Environment.NewLine, errorLines) : $"退出码 {process.ExitCode}";
            throw new InvalidOperationException(detail);
        }

        return outputLines;
    }

    private void ApplyUpgradeMode(UpgradeMode mode)
    {
        _currentMode = mode;

        var isLocal = mode == UpgradeMode.Local;
        Title = isLocal ? "STM32 OTA 本地升级工具" : "STM32 OTA 在线升级工具";
        ModeTitleTextBlock.Text = isLocal ? "STM32 本地 OTA 升级" : "STM32 在线 OTA 升级";
        ModeDescriptionTextBlock.Text = isLocal
            ? "流程：发送解锁 HEX -> 等待回包 -> 发送进入 Bootloader HEX -> 由 C# 内置 YMODEM 发送 STM32 程序。"
            : "当前先只保留在线升级界面，脚本执行逻辑暂未接入。";
        ModeHintTextBlock.Text = isLocal
            ? "默认优先选择 USB-SERIAL CH340 串口。"
            : "在线模式当前只做参数界面预留，不会执行脚本。";

        LocalConfigGrid.Visibility = isLocal ? Visibility.Visible : Visibility.Collapsed;
        RemoteConfigGrid.Visibility = isLocal ? Visibility.Collapsed : Visibility.Visible;
        BootloaderPanel.Visibility = isLocal ? Visibility.Visible : Visibility.Collapsed;
        StartUpgradeButton.Content = isLocal ? "进入 Bootloader 并刷机" : "在线升级待接入";

        PathLabelTextBlock.Text = isLocal ? "STM32程序路径" : "在线脚本路径";
        BrowseScriptButton.Content = isLocal ? "选择程序" : "选择脚本";
        ScriptPathTextBox.Text = isLocal ? GetCurrentLocalImagePath() : _lastRemoteScriptPath;

        if (isLocal)
        {
            if (_runningSlot == FirmwareSlot.Unknown)
            {
                SetRunningSlotUi(FirmwareSlot.Unknown, "请连接设备后等待自动识别槽位。", treatUnknownAsUnread: true);
            }
            else
            {
                SetRunningSlotUi(_runningSlot, BuildSlotRecommendationText(_runningSlot), treatUnknownAsUnread: false);
            }

            UpdateImageHintFromPath(ScriptPathTextBox.Text, logFailure: false);
        }
        else
        {
            RunningSlotTextBlock.Text = "未启用";
            RecommendedImageTextBlock.Text = "在线模式当前不读取槽位。";
            ImageHintTextBlock.Text = "在线升级界面暂不识别镜像槽位。";
        }

        ApplyModeButtonStyle(LocalModeButton, isLocal);
        ApplyModeButtonStyle(RemoteModeButton, !isLocal);
        UpdateStartButtonState();
        UpdateIdleRunningSlotRefreshState();
        StatusTextBlock.Text = isLocal ? "待机" : "在线升级界面预留";
    }

    private static void ApplyModeButtonStyle(Button button, bool isActive)
    {
        button.Background = isActive
            ? new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(22, 50, 79))
            : new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(245, 247, 250));
        button.Foreground = isActive
            ? System.Windows.Media.Brushes.White
            : new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(22, 50, 79));
        button.BorderBrush = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(217, 225, 234));
    }

    private void SetBusyState(bool isBusy, string statusText)
    {
        _isBusy = isBusy;
        RefreshPortsButton.IsEnabled = !isBusy;
        PortComboBox.IsEnabled = !isBusy;
        BaudRateTextBox.IsEnabled = !isBusy;
        TimeoutTextBox.IsEnabled = !isBusy;
        ScriptPathTextBox.IsEnabled = !isBusy;
        BrowseScriptButton.IsEnabled = !isBusy;
        LocalModeButton.IsEnabled = !isBusy;
        RemoteModeButton.IsEnabled = !isBusy;
        RemoteHostTextBox.IsEnabled = !isBusy;
        RemotePortTextBox.IsEnabled = !isBusy;
        UpdateStartButtonState();
        SetRunningSlotRefreshState(_isRefreshingRunningSlot);
        StatusTextBlock.Text = statusText;
    }

    private void UpdateStartButtonState()
    {
        StartUpgradeButton.IsEnabled = !_isBusy && _currentMode == UpgradeMode.Local;
    }

    private string GetCurrentPortName()
    {
        if (PortComboBox.SelectedItem is PortOption option)
        {
            return option.PortName;
        }

        var rawText = PortComboBox.Text.Trim();
        if (string.IsNullOrWhiteSpace(rawText))
        {
            return string.Empty;
        }

        var match = Regex.Match(rawText, @"COM\d+", RegexOptions.IgnoreCase);
        return match.Success ? match.Value.ToUpperInvariant() : rawText;
    }

    private async Task<FirmwareSlot> RefreshRunningSlotHintFromUiAsync(bool logSuccess, bool logFailure, bool allowAutoSuggestPath)
    {
        if (!TryGetLocalSerialSettingsForRead(out var portName, out var baudRate, out var timeoutSeconds))
        {
            SetRunningSlotUi(FirmwareSlot.Unknown, "请先选择串口后等待自动识别槽位。", treatUnknownAsUnread: true);
            return FirmwareSlot.Unknown;
        }

        return await RefreshRunningSlotAsync(portName, baudRate, timeoutSeconds, logSuccess, logFailure, allowAutoSuggestPath);
    }

    private async Task<FirmwareSlot> RefreshRunningSlotAsync(
        string portName,
        int baudRate,
        double timeoutSeconds,
        bool logSuccess,
        bool logFailure,
        bool allowAutoSuggestPath)
    {
        await _runningSlotRefreshLock.WaitAsync();
        SetRunningSlotRefreshState(true);

        try
        {
            var timeout = TimeSpan.FromSeconds(Math.Clamp(timeoutSeconds, 1d, 10d));
            var runningSlot = await Task.Run(() => UpgradeAbSupport.ReadRunningSlot(portName, baudRate, timeout));

            if (runningSlot == FirmwareSlot.Unknown)
            {
                SetRunningSlotUi(FirmwareSlot.Unknown, "设备返回槽位 unknown，请手动选择另一槽镜像。", treatUnknownAsUnread: false);
                if (logSuccess)
                {
                    AppendLog("已读取当前运行槽：unknown。");
                }

                return FirmwareSlot.Unknown;
            }

            SetRunningSlotUi(runningSlot, BuildSlotRecommendationText(runningSlot), treatUnknownAsUnread: false);
            if (allowAutoSuggestPath)
            {
                ApplyRecommendedLocalImagePath(runningSlot);
            }

            if (logSuccess)
            {
                AppendLog($"已读取当前运行槽：{runningSlot.ToDisplayText()}。");
            }

            return runningSlot;
        }
        catch (Exception ex)
        {
            SetRunningSlotUi(FirmwareSlot.Unknown, "未读到槽位，请手动选择另一槽镜像。", treatUnknownAsUnread: false);
            if (logFailure)
            {
                AppendLog($"读取当前运行槽失败：{ex.Message}");
            }

            return FirmwareSlot.Unknown;
        }
        finally
        {
            SetRunningSlotRefreshState(false);
            _runningSlotRefreshLock.Release();
        }
    }

    private bool TryGetLocalSerialSettingsForRead(out string portName, out int baudRate, out double timeoutSeconds)
    {
        portName = GetCurrentPortName();
        if (string.IsNullOrWhiteSpace(portName))
        {
            baudRate = 115200;
            timeoutSeconds = 5;
            return false;
        }

        if (!int.TryParse(BaudRateTextBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out baudRate) || baudRate <= 0)
        {
            baudRate = 115200;
        }

        if (!double.TryParse(TimeoutTextBox.Text.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out timeoutSeconds) || timeoutSeconds <= 0)
        {
            timeoutSeconds = 5;
        }

        return true;
    }

    private void ApplyRecommendedLocalImagePath(FirmwareSlot runningSlot)
    {
        if (_currentMode != UpgradeMode.Local || runningSlot == FirmwareSlot.Unknown || !ShouldAutoApplyRecommendedPath())
        {
            return;
        }

        var recommendedPath = GetRecommendedLocalImagePath(runningSlot);
        _lastAutoSuggestedImagePath = recommendedPath;
        _lastLocalImagePath = recommendedPath;
        ScriptPathTextBox.Text = recommendedPath;
    }

    private bool ShouldAutoApplyRecommendedPath()
    {
        var currentPath = ScriptPathTextBox.Text.Trim();
        if (string.IsNullOrWhiteSpace(currentPath))
        {
            return true;
        }

        if (!string.IsNullOrWhiteSpace(_lastAutoSuggestedImagePath) &&
            string.Equals(currentPath, _lastAutoSuggestedImagePath, StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        if (string.Equals(currentPath, DefaultSlotAImagePath, StringComparison.OrdinalIgnoreCase) ||
            string.Equals(currentPath, DefaultSlotBImagePath, StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        return string.Equals(Path.GetFileName(currentPath), "App.bin", StringComparison.OrdinalIgnoreCase);
    }

    private string GetCurrentLocalImagePath()
    {
        if (!string.IsNullOrWhiteSpace(_lastLocalImagePath))
        {
            return _lastLocalImagePath;
        }

        var fallbackPath = GetRecommendedLocalImagePath(_runningSlot);
        _lastLocalImagePath = fallbackPath;
        _lastAutoSuggestedImagePath = fallbackPath;
        return fallbackPath;
    }

    private static string GetRecommendedLocalImagePath(FirmwareSlot runningSlot)
    {
        return Path.Combine(DefaultObjectsDirectory, UpgradeAbSupport.GetRecommendedFileName(runningSlot));
    }

    private void SetRunningSlotUi(FirmwareSlot slot, string recommendationText, bool treatUnknownAsUnread)
    {
        _runningSlot = slot;
        if (RunningSlotTextBlock is not null)
        {
            RunningSlotTextBlock.Text = slot == FirmwareSlot.Unknown
                ? (treatUnknownAsUnread ? "未读取" : "未知")
                : slot.ToDisplayText();
        }

        if (RecommendedImageTextBlock is not null)
        {
            RecommendedImageTextBlock.Text = recommendationText;
        }
    }

    private void SetRunningSlotRefreshState(bool isRefreshing)
    {
        _isRefreshingRunningSlot = isRefreshing;
        UpdateIdleRunningSlotRefreshState();
    }

    private void UpdateIdleRunningSlotRefreshState()
    {
        if (_currentMode == UpgradeMode.Local && !_isBusy)
        {
            _idleRunningSlotRefreshTimer.Start();
            return;
        }

        _idleRunningSlotRefreshTimer.Stop();
    }

    private static string BuildSlotRecommendationText(FirmwareSlot runningSlot)
    {
        return runningSlot switch
        {
            FirmwareSlot.A => "设备当前运行 A，建议选择 App_B.bin。",
            FirmwareSlot.B => "设备当前运行 B，建议选择 App_A.bin。",
            _ => "未读到槽位，请手动选择另一槽镜像。"
        };
    }

    private void UpdateImageHintFromPath(string imagePath, bool logFailure)
    {
        if (ImageHintTextBlock is null)
        {
            return;
        }

        if (_currentMode != UpgradeMode.Local)
        {
            ImageHintTextBlock.Text = "在线升级界面暂不识别镜像槽位。";
            return;
        }

        if (string.IsNullOrWhiteSpace(imagePath))
        {
            ImageHintTextBlock.Text = "镜像槽位：未选择程序。";
            return;
        }

        if (!File.Exists(imagePath))
        {
            ImageHintTextBlock.Text = "镜像槽位：文件不存在。";
            return;
        }

        if (TryReadImageInfo(imagePath, out var imageInfo, out _, logFailure))
        {
            ImageHintTextBlock.Text = UpgradeAbSupport.BuildImageHint(imageInfo);
        }
    }

    private bool TryReadImageInfo(string imagePath, out FirmwareImageInfo imageInfo, out string errorMessage, bool logFailure)
    {
        try
        {
            imageInfo = UpgradeAbSupport.InspectImage(imagePath);
            if (ImageHintTextBlock is not null)
            {
                ImageHintTextBlock.Text = UpgradeAbSupport.BuildImageHint(imageInfo);
            }

            errorMessage = string.Empty;
            return true;
        }
        catch (Exception ex)
        {
            imageInfo = default;
            errorMessage = $"STM32 程序槽位识别失败：{ex.Message}";
            if (ImageHintTextBlock is not null)
            {
                ImageHintTextBlock.Text = $"镜像槽位：无法识别。{ex.Message}";
            }

            if (logFailure)
            {
                AppendLog(errorMessage);
            }

            return false;
        }
    }

    private static PortOption? ParsePortOption(string line)
    {
        if (string.IsNullOrWhiteSpace(line))
        {
            return null;
        }

        var parts = line.Split('\t', 2, StringSplitOptions.TrimEntries);
        if (parts.Length == 0 || string.IsNullOrWhiteSpace(parts[0]))
        {
            return null;
        }

        var portName = parts[0].ToUpperInvariant();
        var displayName = parts.Length > 1 && !string.IsNullOrWhiteSpace(parts[1]) ? parts[1] : portName;
        return new PortOption(portName, displayName);
    }

    private void AppendLogFromWorker(string message)
    {
        Dispatcher.Invoke(() => AppendLog(message));
    }

    private void AppendLog(string message)
    {
        LogTextBox.AppendText($"[{DateTime.Now:HH:mm:ss}] {message}{Environment.NewLine}");
        LogTextBox.ScrollToEnd();
    }

    private sealed record PortOption(string PortName, string DisplayName)
    {
        public bool IsCh340 => DisplayName.Contains("CH340", StringComparison.OrdinalIgnoreCase);

        public override string ToString()
        {
            return PortName;
        }
    }

    private enum UpgradeMode
    {
        Local,
        Remote
    }
}
