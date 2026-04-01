// MainWindow.Shell.cs
// 放构造、模式切换、忙碌状态、日志、标题栏、关闭按钮、WndProc。

using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Effects;
using System.Windows.Threading;

namespace Project;

public partial class MainWindow : Window
{
    /// <summary>
    /// 初始化主窗口和后台刷新机制。
    /// </summary>
    public MainWindow()
    {
        InitializeComponent();
        InitializeRemoteUpgradeModule();
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
            _ = RemoteUpgradeViewControl.RefreshDeviceContextAsync();
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
            button.Background = Brushes.White;
            button.BorderBrush = new SolidColorBrush(Color.FromRgb(213, 218, 227));
            button.Foreground = new SolidColorBrush(Color.FromRgb(15, 23, 42));
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
            button.Background = new SolidColorBrush(Color.FromRgb(248, 249, 251));
            button.BorderBrush = Brushes.Transparent;
            button.Foreground = new SolidColorBrush(Color.FromRgb(75, 85, 99));
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

            current = VisualTreeHelper.GetParent(current);
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
}
