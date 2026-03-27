using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;

namespace Project;

public partial class MainWindow : Window
{
    private const string DefaultLocalImagePath = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects\App.bin";
    private const string RemoteScriptPath = @"D:\Project\STM32_Mill\stm32_remote_upgrade.py";

    private UpgradeMode _currentMode = UpgradeMode.Local;
    private bool _isBusy;

    public MainWindow()
    {
        InitializeComponent();
        ApplyUpgradeMode(UpgradeMode.Local);
        Loaded += MainWindow_OnLoaded;
        AppendLog("工具已启动。");
    }

    private async void MainWindow_OnLoaded(object sender, RoutedEventArgs e)
    {
        await RefreshPortListAsync();
        AppendLog("串口参数默认使用 115200, 8N1。");
    }

    private async void StartUpgradeButton_OnClick(object sender, RoutedEventArgs e)
    {
        if (_currentMode == UpgradeMode.Remote)
        {
            MessageBox.Show(this, "在线升级脚本暂未接入，当前只保留界面。", "提示", MessageBoxButton.OK, MessageBoxImage.Information);
            return;
        }

        if (!TryReadLocalSettings(out var options, out var errorMessage))
        {
            MessageBox.Show(this, errorMessage, "参数错误", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        SetBusyState(true, "执行中");
        LogTextBox.Clear();
        AppendLog("准备升级，模式 本地升级。");

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

    private void PortComboBox_OnSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (PortComboBox.SelectedItem is PortOption option)
        {
            PortComboBox.Text = option.PortName;
        }
    }

    private bool TryReadLocalSettings(out LocalUpgradeOptions options, out string errorMessage)
    {
        options = default;

        var portName = GetCurrentPortName();
        if (string.IsNullOrWhiteSpace(portName))
        {
            errorMessage = "请先选择串口号。";
            return false;
        }

        if (!int.TryParse(BaudRateTextBox.Text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out var baudRate) || baudRate <= 0)
        {
            errorMessage = "波特率必须是正整数。";
            return false;
        }

        if (!double.TryParse(TimeoutTextBox.Text.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out var timeoutSeconds) || timeoutSeconds <= 0)
        {
            errorMessage = "超时必须是大于 0 的数字。";
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

            PortComboBox.ItemsSource = portOptions;

            var selectedOption =
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
            }

            AppendLog($"已刷新串口列表：{(portOptions.Count == 0 ? "未检测到串口" : string.Join(", ", portOptions.Select(option => option.DisplayName)))}");
        }
        catch (Exception ex)
        {
            PortComboBox.Text = string.IsNullOrWhiteSpace(currentPort) ? "COM12" : currentPort;
            AppendLog($"刷新串口失败：{ex.Message}");
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
            ? "流程：发送解锁 HEX -> 等待 500ms -> 发送进入 Bootloader HEX -> 由 C# 内置 YMODEM 发送 STM32 程序。"
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
        ScriptPathTextBox.Text = isLocal ? DefaultLocalImagePath : RemoteScriptPath;

        ApplyModeButtonStyle(LocalModeButton, isLocal);
        ApplyModeButtonStyle(RemoteModeButton, !isLocal);
        UpdateStartButtonState();
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
