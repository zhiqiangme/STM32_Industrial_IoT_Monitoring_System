using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Effects;
using System.Windows.Threading;
using Microsoft.Win32;

namespace Project;

/// <summary>
/// 主窗口。
/// 负责界面交互、串口列表维护、运行槽位自动识别，以及本地 OTA 升级流程的组织。
/// </summary>
public partial class MainWindow : Window
{
    // 默认镜像路径配置。
    private const string DefaultObjectsDirectory = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects";
    private const string DefaultSlotAImagePath = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects\App_A.bin";
    private const string DefaultSlotBImagePath = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects\App_B.bin";

    // Windows 设备变更消息常量，用于监听串口插拔。
    private const int WmDeviceChange = 0x0219;
    private const int DbtDeviceArrival = 0x8000;
    private const int DbtDeviceRemoveComplete = 0x8004;
    private const int DbtDevNodesChanged = 0x0007;

    // 当前界面模式与刷新状态。
    private UpgradeMode _currentMode = UpgradeMode.Local;
    private bool _isBusy;
    private bool _isRefreshingPortList;
    private bool _isRefreshingRunningSlot;
    private bool _suppressPortSelectionRefresh;
    private int _consecutiveNoPortRefreshCount;

    // 当前识别到的运行槽位，以及最近使用/推荐的镜像路径。
    private FirmwareSlot _runningSlot = FirmwareSlot.Unknown;
    private string _lastLocalImagePath = DefaultSlotAImagePath;
    private string? _lastAutoSuggestedImagePath = DefaultSlotAImagePath;

    // 上一次已知串口列表，用于识别“新插入”的串口。
    private HashSet<string> _knownPortNames = new(StringComparer.OrdinalIgnoreCase);

    // 防止串口刷新与槽位读取并发重入。
    private readonly SemaphoreSlim _runningSlotRefreshLock = new(1, 1);
    private readonly SemaphoreSlim _portListRefreshLock = new(1, 1);

    // 空闲轮询和设备插拔防抖刷新定时器。
    private readonly DispatcherTimer _idlePortListRefreshTimer;
    private readonly DispatcherTimer _idleRunningSlotRefreshTimer;
    private readonly DispatcherTimer _deviceChangeRefreshTimer;
    private HwndSource? _hwndSource;

    // 运行日志“连续重复覆盖”所需的上一条状态。
    private string? _lastLoggedMessage;
    private int _lastLoggedLineLength;

    /// <summary>
    /// 初始化主窗口和后台刷新机制。
    /// </summary>
    public MainWindow()
    {
        InitializeComponent();
        _idlePortListRefreshTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromSeconds(2)
        };
        _idlePortListRefreshTimer.Tick += IdlePortListRefreshTimer_OnTick;
        _idleRunningSlotRefreshTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromSeconds(3)
        };
        _idleRunningSlotRefreshTimer.Tick += IdleRunningSlotRefreshTimer_OnTick;
        _deviceChangeRefreshTimer = new DispatcherTimer
        {
            Interval = TimeSpan.FromMilliseconds(350)
        };
        _deviceChangeRefreshTimer.Tick += DeviceChangeRefreshTimer_OnTick;
        Closing += MainWindow_Closing;
        SourceInitialized += MainWindow_OnSourceInitialized;
        ApplyUpgradeMode(UpgradeMode.Local);
        Loaded += MainWindow_OnLoaded;
        AppendLog("工具已启动。");
    }

    /// <summary>
    /// 关闭窗口时清理定时器和窗口消息钩子。
    /// </summary>
    private void MainWindow_Closing(object? sender, System.ComponentModel.CancelEventArgs e)
    {
        _idlePortListRefreshTimer.Stop();
        _idleRunningSlotRefreshTimer.Stop();
        _deviceChangeRefreshTimer.Stop();
        if (_hwndSource is not null)
        {
            _hwndSource.RemoveHook(WndProc);
            _hwndSource = null;
        }
    }

    /// <summary>
    /// 句柄创建完成后挂接 WndProc，以便接收串口插拔事件。
    /// </summary>
    private void MainWindow_OnSourceInitialized(object? sender, EventArgs e)
    {
        _hwndSource = PresentationSource.FromVisual(this) as HwndSource;
        _hwndSource?.AddHook(WndProc);
    }

    /// <summary>
    /// 首次加载时刷新串口和镜像提示。
    /// </summary>
    private async void MainWindow_OnLoaded(object sender, RoutedEventArgs e)
    {
        await RefreshPortListAsync();
        UpdateImageHintFromPath(ScriptPathTextBox.Text, logFailure: false);
        AppendLog("串口参数默认使用 115200, 8N1。");
    }

    /// <summary>
    /// 本地升级按钮入口。
    /// 依次完成参数校验、运行槽位识别、镜像校验和真正升级。
    /// </summary>
    private async void StartUpgradeButton_OnClick(object sender, RoutedEventArgs e)
    {
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
        ClearLog();
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

    /// <summary>
    /// 浏览选择本地 BIN 文件。
    /// </summary>
    private void BrowseScriptButton_OnClick(object sender, RoutedEventArgs e)
    {
        var currentDirectory = Path.GetDirectoryName(ScriptPathTextBox.Text);
        var dialog = new OpenFileDialog
        {
            InitialDirectory = !string.IsNullOrWhiteSpace(currentDirectory) && Directory.Exists(currentDirectory)
                ? currentDirectory
                : @"D:\Project\STM32_Mill",
            FileName = Path.GetFileName(ScriptPathTextBox.Text),
            Title = "选择 STM32 程序文件",
            Filter = "BIN 文件 (*.bin)|*.bin|所有文件 (*.*)|*.*"
        };

        if (dialog.ShowDialog(this) == true)
        {
            ScriptPathTextBox.Text = dialog.FileName;
            AppendLog($"已选择 STM32 程序：{dialog.FileName}");
        }
    }

    /// <summary>
    /// 切回本地升级标签。
    /// </summary>
    private void LocalModeButton_OnClick(object sender, RoutedEventArgs e)
    {
        if (_currentMode == UpgradeMode.Local)
        {
            return;
        }

        ApplyUpgradeMode(UpgradeMode.Local);
    }

    /// <summary>
    /// 切到远程升级标签。
    /// 当前页面仅占位，不执行远程升级逻辑。
    /// </summary>
    private void RemoteModeButton_OnClick(object sender, RoutedEventArgs e)
    {
        if (_currentMode == UpgradeMode.Remote)
        {
            return;
        }

        ApplyUpgradeMode(UpgradeMode.Remote);
    }
    /// <summary>
    /// 串口选择变化后，同步刷新当前运行槽位提示。
    /// </summary>
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

    /// <summary>
    /// 空闲轮询当前运行槽位。
    /// </summary>
    private async void IdleRunningSlotRefreshTimer_OnTick(object? sender, EventArgs e)
    {
        if (_currentMode != UpgradeMode.Local || _isBusy || _isRefreshingRunningSlot)
        {
            return;
        }

        await RefreshRunningSlotHintFromUiAsync(logSuccess: false, logFailure: false, allowAutoSuggestPath: true);
    }

    /// <summary>
    /// 空闲轮询串口列表。
    /// 当设备变更消息偶发漏掉时，这里负责兜底刷新。
    /// </summary>
    private async void IdlePortListRefreshTimer_OnTick(object? sender, EventArgs e)
    {
        if (_currentMode != UpgradeMode.Local || _isBusy || _isRefreshingPortList || _isRefreshingRunningSlot)
        {
            return;
        }

        await RefreshPortListAsync();
    }

    /// <summary>
    /// 设备插拔后的防抖刷新入口。
    /// 如果当前正忙，会延后到下一轮再刷新，避免和串口操作打架。
    /// </summary>
    private async void DeviceChangeRefreshTimer_OnTick(object? sender, EventArgs e)
    {
        _deviceChangeRefreshTimer.Stop();

        if (_currentMode != UpgradeMode.Local)
        {
            return;
        }

        if (_isBusy || _isRefreshingPortList || _isRefreshingRunningSlot)
        {
            _deviceChangeRefreshTimer.Start();
            return;
        }

        await RefreshPortListAsync();
    }

    /// <summary>
    /// 镜像路径变化后，更新缓存并重新识别镜像槽位提示。
    /// </summary>
    private void ScriptPathTextBox_OnTextChanged(object sender, TextChangedEventArgs e)
    {
        _lastLocalImagePath = ScriptPathTextBox.Text.Trim();
        UpdateImageHintFromPath(ScriptPathTextBox.Text, logFailure: false);
    }

    /// <summary>
    /// 读取并校验界面上的串口参数。
    /// </summary>
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

        if (!int.TryParse(GetBaudRateText(), NumberStyles.Integer, CultureInfo.InvariantCulture, out baudRate) || baudRate <= 0)
        {
            errorMessage = "波特率必须是正整数。";
            return false;
        }

        if (!double.TryParse(GetTimeoutText(), NumberStyles.Float, CultureInfo.InvariantCulture, out timeoutSeconds) || timeoutSeconds <= 0)
        {
            errorMessage = "超时必须是大于 0 的数字。";
            return false;
        }

        errorMessage = string.Empty;
        return true;
    }

    /// <summary>
    /// 读取并校验本地升级所需的完整参数。
    /// </summary>
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

    /// <summary>
    /// 刷新串口列表，并尽量保留或智能切换当前选中项。
    /// </summary>
    private async Task RefreshPortListAsync()
    {
        await _portListRefreshLock.WaitAsync();
        _isRefreshingPortList = true;
        var currentPort = GetCurrentPortName();

        try
        {
            var portNames = SerialPort.GetPortNames()
                .OrderBy(GetPortSortKey)
                .ThenBy(n => n, StringComparer.OrdinalIgnoreCase)
                .ToList();
            var portOptions = await BuildPortOptionsAsync(portNames);
            var newlyAddedPortNames = portOptions
                .Select(option => option.PortName)
                .Where(portName => !_knownPortNames.Contains(portName))
                .ToHashSet(StringComparer.OrdinalIgnoreCase);

            PortOption? selectedOption;
            // 刷新数据源时会触发 SelectionChanged，这里先临时压住，避免重复读取槽位。
            _suppressPortSelectionRefresh = true;
            try
            {
                PortComboBox.ItemsSource = portOptions;

                selectedOption =
                    portOptions.FirstOrDefault(option => string.Equals(option.PortName, currentPort, StringComparison.OrdinalIgnoreCase)) ??
                    SelectPreferredPort(portOptions, newlyAddedPortNames);

                if (selectedOption is not null)
                {
                    PortComboBox.SelectedItem = selectedOption;
                    PortComboBox.Text = selectedOption.PortName;
                }
                else
                {
                    PortComboBox.SelectedItem = null;
                    PortComboBox.Text = string.Empty;
                    SetRunningSlotUi(FirmwareSlot.Unknown, "未检测到串口，无法自动识别槽位。", treatUnknownAsUnread: true);
                }
            }
            finally
            {
                _suppressPortSelectionRefresh = false;
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

            _knownPortNames = portOptions
                .Select(option => option.PortName)
                .ToHashSet(StringComparer.OrdinalIgnoreCase);

            if (selectedOption is not null && _currentMode == UpgradeMode.Local && !_isBusy)
            {
                await RefreshRunningSlotHintFromUiAsync(logSuccess: false, logFailure: false, allowAutoSuggestPath: true);
            }
        }
        catch (Exception ex)
        {
            _consecutiveNoPortRefreshCount = 0;
            PortComboBox.Text = string.Empty;
            AppendLog($"刷新串口失败：{ex.Message}");
            SetRunningSlotUi(FirmwareSlot.Unknown, "刷新串口失败，无法自动识别槽位。", treatUnknownAsUnread: true);
        }
        finally
        {
            _isRefreshingPortList = false;
            _portListRefreshLock.Release();
        }
    }

    /// <summary>
    /// 启动一个后台 PowerShell 查询。
    /// 当前主要用于向系统查询串口的设备描述。
    /// </summary>
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

    /// <summary>
    /// 把原始 COM 口名补全成带显示描述的下拉项。
    /// 如果系统查询描述失败，会自动退回只显示 COMx。
    /// </summary>
    private async Task<List<PortOption>> BuildPortOptionsAsync(IReadOnlyList<string> portNames)
    {
        var descriptions = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        try
        {
            var outputLines = await RunPowerShellAsync(
                """
                $pattern = '\((COM\d+)\)\s*$'
                Get-CimInstance Win32_PnPEntity |
                    Where-Object { $_.Name } |
                    ForEach-Object {
                        if ($_.Name -match $pattern) {
                            $portName = $matches[1].ToUpperInvariant()
                            $displayName = ($_.Name -replace "\s*\($portName\)\s*$", '').Trim()
                            "$portName`t$displayName"
                        }
                    }
                """,
                logOutput: false);

            foreach (var line in outputLines)
            {
                var option = ParsePortOption(line);
                if (option is null || string.IsNullOrWhiteSpace(option.DisplayName))
                {
                    continue;
                }

                descriptions[option.PortName] = NormalizePortDisplayName(option.DisplayName);
            }
        }
        catch
        {
            // Fall back to plain COM port names when device descriptions cannot be queried.
        }

        return portNames
            .Select(name =>
            {
                var portName = name.ToUpperInvariant();
                descriptions.TryGetValue(portName, out var displayName);
                return new PortOption(portName, displayName ?? string.Empty);
            })
            .ToList();
    }

    /// <summary>
    /// 应用本地/远程模式对应的界面状态。
    /// </summary>
    private void ApplyUpgradeMode(UpgradeMode mode)
    {
        _currentMode = mode;
        LocalContentGrid.Visibility = mode == UpgradeMode.Local ? Visibility.Visible : Visibility.Collapsed;
        RemoteContentGrid.Visibility = mode == UpgradeMode.Remote ? Visibility.Visible : Visibility.Collapsed;
        ApplyModeButtonStyle(LocalModeButton, mode == UpgradeMode.Local);
        ApplyModeButtonStyle(RemoteModeButton, mode == UpgradeMode.Remote);

        if (mode == UpgradeMode.Remote)
        {
            _idlePortListRefreshTimer.Stop();
            _idleRunningSlotRefreshTimer.Stop();
            StatusTextBlock.Text = "待机";
            UpdateStartButtonState();
            return;
        }

        ModeTitleTextBlock.Text = "STM32 本地 OTA 升级";
        ModeDescriptionTextBlock.Text = "流程：发送解锁 HEX -> 等待回包 -> 发送进入 Bootloader HEX -> 由 C# 内置 YMODEM 发送 STM32 程序。";
        ModeHintTextBlock.Text = "默认优先选择 USB-SERIAL CH340 串口。";

        StartUpgradeButton.Content = "进入 Bootloader 并刷机";

        PathLabelTextBlock.Text = "STM32程序路径";
        BrowseScriptButton.Content = "选择程序";
        ScriptPathTextBox.Text = GetCurrentLocalImagePath();

        if (_runningSlot == FirmwareSlot.Unknown)
        {
            SetRunningSlotUi(FirmwareSlot.Unknown, "请连接设备后等待自动识别槽位。", treatUnknownAsUnread: true);
        }
        else
        {
            SetRunningSlotUi(_runningSlot, BuildSlotRecommendationText(_runningSlot), treatUnknownAsUnread: false);
        }

        UpdateImageHintFromPath(ScriptPathTextBox.Text, logFailure: false);
        UpdateStartButtonState();
        UpdateIdleRunningSlotRefreshState();
        StatusTextBlock.Text = "待机";
    }

    /// <summary>
    /// 根据选中状态更新顶部标签外观。
    /// </summary>
    private void ApplyModeButtonStyle(Button button, bool isActive)
    {
        if (isActive)
        {
            button.Background = System.Windows.Media.Brushes.White;
            button.BorderBrush = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(213, 218, 227));
            button.Foreground = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(15, 23, 42));
            button.Effect = new DropShadowEffect
            {
                BlurRadius = 10,
                ShadowDepth = 1,
                Opacity = 0.16,
                Color = Color.FromRgb(15, 23, 42)
            };
        }
        else
        {
            button.Background = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(248, 249, 251));
            button.BorderBrush = System.Windows.Media.Brushes.Transparent;
            button.Foreground = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(75, 85, 99));
            button.Effect = null;
        }
    }

    /// <summary>
    /// 统一切换忙碌状态，并联动禁用关键输入控件。
    /// </summary>
    private void SetBusyState(bool isBusy, string statusText)
    {
        _isBusy = isBusy;
        PortComboBox.IsEnabled = !isBusy;
        BaudRateComboBox.IsEnabled = !isBusy;
        TimeoutComboBox.IsEnabled = !isBusy;
        ScriptPathTextBox.IsEnabled = !isBusy;
        BrowseScriptButton.IsEnabled = !isBusy;
        LocalModeButton.IsEnabled = !isBusy;
        RemoteModeButton.IsEnabled = !isBusy;
        UpdateStartButtonState();
        SetRunningSlotRefreshState(_isRefreshingRunningSlot);
        StatusTextBlock.Text = statusText;
    }

    /// <summary>
    /// 刷新开始升级按钮的可用状态。
    /// </summary>
    private void UpdateStartButtonState()
    {
        StartUpgradeButton.IsEnabled = !_isBusy && _currentMode == UpgradeMode.Local;
    }

    /// <summary>
    /// 从下拉框当前状态中提取串口号。
    /// 兼容“选中对象”和“手工输入文本”两种来源。
    /// </summary>
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

    /// <summary>
    /// 按当前界面参数发起一次运行槽位读取。
    /// </summary>
    private async Task<FirmwareSlot> RefreshRunningSlotHintFromUiAsync(bool logSuccess, bool logFailure, bool allowAutoSuggestPath)
    {
        if (!TryGetLocalSerialSettingsForRead(out var portName, out var baudRate, out var timeoutSeconds))
        {
            SetRunningSlotUi(FirmwareSlot.Unknown, "未检测到串口，无法自动识别槽位。", treatUnknownAsUnread: true);
            return FirmwareSlot.Unknown;
        }

        return await RefreshRunningSlotAsync(portName, baudRate, timeoutSeconds, logSuccess, logFailure, allowAutoSuggestPath);
    }

    /// <summary>
    /// 真正执行一次运行槽位读取。
    /// 底层串口操作放到后台线程，避免阻塞界面。
    /// </summary>
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
            var result = await Task.Run(() =>
            {
                var success = UpgradeAbSupport.TryReadRunningSlot(portName, baudRate, timeout, out var slot, out var errorMessage);
                return new RunningSlotReadResult(success, slot, errorMessage);
            });

            if (!result.Success)
            {
                SetRunningSlotUi(FirmwareSlot.Unknown, "未读到槽位，请手动选择另一槽镜像。", treatUnknownAsUnread: false);
                if (logFailure && !string.IsNullOrWhiteSpace(result.ErrorMessage))
                {
                    AppendLog($"读取当前运行槽失败：{result.ErrorMessage}");
                }

                return FirmwareSlot.Unknown;
            }

            var runningSlot = result.Slot;

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

    /// <summary>
    /// 读取用于后台槽位探测的串口参数。
    /// 如果界面参数不完整，会退回默认波特率和超时值。
    /// </summary>
    private bool TryGetLocalSerialSettingsForRead(out string portName, out int baudRate, out double timeoutSeconds)
    {
        if (!TryGetDetectedPortName(out portName))
        {
            baudRate = 115200;
            timeoutSeconds = 5;
            return false;
        }

        if (!int.TryParse(GetBaudRateText(), NumberStyles.Integer, CultureInfo.InvariantCulture, out baudRate) || baudRate <= 0)
        {
            baudRate = 115200;
        }

        if (!double.TryParse(GetTimeoutText(), NumberStyles.Float, CultureInfo.InvariantCulture, out timeoutSeconds) || timeoutSeconds <= 0)
        {
            timeoutSeconds = 5;
        }

        return true;
    }

    /// <summary>
    /// 从当前界面状态中解析出一个确实存在于数据源里的串口号。
    /// </summary>
    private bool TryGetDetectedPortName(out string portName)
    {
        portName = string.Empty;
        var currentPort = GetCurrentPortName();
        if (string.IsNullOrWhiteSpace(currentPort))
        {
            return false;
        }

        if (PortComboBox.SelectedItem is PortOption selectedOption &&
            string.Equals(selectedOption.PortName, currentPort, StringComparison.OrdinalIgnoreCase))
        {
            portName = selectedOption.PortName;
            return true;
        }

        if (PortComboBox.ItemsSource is IEnumerable<PortOption> portOptions)
        {
            var matchedOption = portOptions.FirstOrDefault(option =>
                string.Equals(option.PortName, currentPort, StringComparison.OrdinalIgnoreCase));
            if (matchedOption is not null)
            {
                portName = matchedOption.PortName;
                return true;
            }
        }

        return false;
    }

    /// <summary>
    /// 读取波特率文本。
    /// </summary>
    private string GetBaudRateText()
    {
        return BaudRateComboBox.Text.Trim();
    }

    /// <summary>
    /// 读取超时文本。
    /// </summary>
    private string GetTimeoutText()
    {
        return TimeoutComboBox.Text.Trim();
    }

    /// <summary>
    /// 自动把镜像路径切到当前推荐槽位的文件。
    /// 只在仍然处于“默认路径/自动推荐路径”时覆盖，避免打断用户手工选择。
    /// </summary>
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

    /// <summary>
    /// 判断当前镜像路径是否允许被自动推荐逻辑覆盖。
    /// </summary>
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

    /// <summary>
    /// 获取当前应展示的本地镜像路径。
    /// </summary>
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

    /// <summary>
    /// 根据当前运行槽位生成推荐镜像完整路径。
    /// </summary>
    private static string GetRecommendedLocalImagePath(FirmwareSlot runningSlot)
    {
        return Path.Combine(DefaultObjectsDirectory, UpgradeAbSupport.GetRecommendedFileName(runningSlot));
    }

    /// <summary>
    /// 把槽位识别结果和推荐文案同步到界面。
    /// </summary>
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

    /// <summary>
    /// 设置“正在读取槽位”的标志，并联动后台轮询状态。
    /// </summary>
    private void SetRunningSlotRefreshState(bool isRefreshing)
    {
        _isRefreshingRunningSlot = isRefreshing;
        UpdateIdleRunningSlotRefreshState();
    }

    /// <summary>
    /// 根据当前模式和忙碌状态启停后台轮询。
    /// </summary>
    private void UpdateIdleRunningSlotRefreshState()
    {
        if (_currentMode == UpgradeMode.Local && !_isBusy)
        {
            _idleRunningSlotRefreshTimer.Start();
            _idlePortListRefreshTimer.Start();
            return;
        }

        _idleRunningSlotRefreshTimer.Stop();
        _idlePortListRefreshTimer.Stop();
    }

    /// <summary>
    /// 根据当前运行槽位生成推荐镜像文案。
    /// </summary>
    private static string BuildSlotRecommendationText(FirmwareSlot runningSlot)
    {
        return runningSlot switch
        {
            FirmwareSlot.A => "设备当前运行 A，建议选择 App_B.bin。",
            FirmwareSlot.B => "设备当前运行 B，建议选择 App_A.bin。",
            _ => "未读到槽位，请手动选择另一槽镜像。"
        };
    }

    /// <summary>
    /// 根据当前 BIN 文件路径更新镜像槽位提示。
    /// </summary>
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

    /// <summary>
    /// 尝试解析镜像文件信息。
    /// </summary>
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

    /// <summary>
    /// 解析 PowerShell 返回的一行串口描述。
    /// </summary>
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

    /// <summary>
    /// 归一化串口显示名，避免系统返回的名称风格差异过大。
    /// </summary>
    private static string NormalizePortDisplayName(string displayName)
    {
        if (string.IsNullOrWhiteSpace(displayName))
        {
            return string.Empty;
        }

        var normalized = displayName.Trim();
        if (normalized.Contains("USB", StringComparison.OrdinalIgnoreCase) &&
            (normalized.Contains("SERIAL", StringComparison.OrdinalIgnoreCase) ||
             normalized.Contains("串行", StringComparison.OrdinalIgnoreCase) ||
             normalized.Contains("CH340", StringComparison.OrdinalIgnoreCase)))
        {
            return "USB串行设备";
        }

        return normalized;
    }

    /// <summary>
    /// 提取 COM 口数字部分用于排序，确保 COM10 不会排在 COM2 前面。
    /// </summary>
    private static int GetPortSortKey(string? portName)
    {
        if (string.IsNullOrWhiteSpace(portName))
        {
            return int.MaxValue;
        }

        var match = Regex.Match(portName, @"COM(\d+)", RegexOptions.IgnoreCase);
        return match.Success && int.TryParse(match.Groups[1].Value, out var portNumber)
            ? portNumber
            : int.MaxValue;
    }

    /// <summary>
    /// 选择最适合自动选中的串口。
    /// 优先考虑新插入的 USB 串口，其次 CH340，再次任意新串口。
    /// </summary>
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

    /// <summary>
    /// 供后台线程安全写日志的包装。
    /// </summary>
    private void AppendLogFromWorker(string message)
    {
        Dispatcher.Invoke(() => AppendLog(message));
    }

    /// <summary>
    /// 追加运行日志。
    /// 如果连续两次收到相同文本，则覆盖上一条，只更新时间戳。
    /// </summary>
    private void AppendLog(string message)
    {
        var normalizedMessage = (message ?? string.Empty).TrimEnd('\r', '\n');
        var renderedLine = $"[{DateTime.Now:HH:mm:ss}] {normalizedMessage}{Environment.NewLine}";

        if (string.Equals(_lastLoggedMessage, normalizedMessage, StringComparison.Ordinal) &&
            _lastLoggedLineLength > 0 &&
            LogTextBox.Text.Length >= _lastLoggedLineLength)
        {
            LogTextBox.Text = LogTextBox.Text[..^_lastLoggedLineLength] + renderedLine;
        }
        else
        {
            LogTextBox.AppendText(renderedLine);
        }

        _lastLoggedMessage = normalizedMessage;
        _lastLoggedLineLength = renderedLine.Length;
        LogTextBox.ScrollToEnd();
    }

    /// <summary>
    /// 清空日志并重置重复覆盖状态。
    /// </summary>
    private void ClearLog()
    {
        LogTextBox.Clear();
        _lastLoggedMessage = null;
        _lastLoggedLineLength = 0;
    }

    /// <summary>
    /// 串口下拉项。
    /// PortName 用于闭合状态显示和实际操作，DisplayName 用于展开列表补充设备类型。
    /// </summary>
    private sealed record PortOption(string PortName, string DisplayName)
    {
        public bool HasDisplayDetail =>
            !string.IsNullOrWhiteSpace(DisplayName) &&
            !string.Equals(DisplayName, PortName, StringComparison.OrdinalIgnoreCase);

        public string DisplayText => HasDisplayDetail ? $"{PortName} {DisplayName}" : PortName;

        public bool IsCh340 => DisplayName.Contains("CH340", StringComparison.OrdinalIgnoreCase);
        public bool IsUsbSerial =>
            DisplayName.Contains("USB", StringComparison.OrdinalIgnoreCase) &&
            (DisplayName.Contains("SERIAL", StringComparison.OrdinalIgnoreCase) ||
             DisplayName.Contains("串行", StringComparison.OrdinalIgnoreCase));

        public override string ToString()
        {
            return PortName;
        }
    }

    /// <summary>
    /// 运行槽位读取结果对象。
    /// </summary>
    private readonly record struct RunningSlotReadResult(bool Success, FirmwareSlot Slot, string? ErrorMessage);

    /// <summary>
    /// 在标题栏空白区域按下鼠标时拖动窗口。
    /// 为避免误触，按钮、输入框、下拉框上的点击都不会触发拖动。
    /// </summary>
    private void TitleBar_MouseLeftButtonDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
    {
        if (e.ClickCount != 1)
        {
            return;
        }

        if (e.OriginalSource is DependencyObject source &&
            (HasAncestor<Button>(source) || HasAncestor<TextBox>(source) || HasAncestor<ComboBox>(source)))
        {
            return;
        }

        try
        {
            DragMove();
        }
        catch (InvalidOperationException)
        {
            // Rapid clicks on the custom title bar can re-enter DragMove while WPF
            // is still processing the previous mouse interaction. Ignore safely.
        }
    }

    /// <summary>
    /// 判断某个可视元素是否位于指定类型父控件内部。
    /// </summary>
    private static bool HasAncestor<T>(DependencyObject? current) where T : DependencyObject
    {
        while (current is not null)
        {
            if (current is T)
            {
                return true;
            }

            current = System.Windows.Media.VisualTreeHelper.GetParent(current);
        }

        return false;
    }

    /// <summary>
    /// 关闭窗口。
    /// </summary>
    private void CloseButton_Click(object sender, RoutedEventArgs e)
    {
        Close();
    }

    /// <summary>
    /// 处理窗口消息。
    /// 这里只关心串口插拔对应的设备变更消息。
    /// </summary>
    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg == WmDeviceChange)
        {
            var eventCode = wParam.ToInt32();
            if (eventCode is DbtDeviceArrival or DbtDeviceRemoveComplete or DbtDevNodesChanged)
            {
                SchedulePortListRefresh();
            }
        }

        return IntPtr.Zero;
    }

    /// <summary>
    /// 安排一次串口列表防抖刷新。
    /// </summary>
    private void SchedulePortListRefresh()
    {
        if (_currentMode != UpgradeMode.Local)
        {
            return;
        }

        _deviceChangeRefreshTimer.Stop();
        _deviceChangeRefreshTimer.Start();
    }

    /// <summary>
    /// 顶部标签对应的界面模式。
    /// </summary>
    private enum UpgradeMode
    {
        Local,
        Remote
    }
}
