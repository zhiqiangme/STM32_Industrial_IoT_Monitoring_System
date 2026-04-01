using System.ComponentModel;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;
using OTA.ViewModels;
using OTA.ViewModels.Messages;

namespace OTA.UI;

/// <summary>
/// 主窗口视图层。
/// 当前主窗口是应用外壳，只负责窗口生命周期、设备消息和后台轮询，
/// 页面导航与内容由 TabControl + UserControl 组合承载。
/// </summary>
public partial class MainWindow : Window
{
    private const int WmDeviceChange = 0x0219;
    private const int DbtDeviceArrival = 0x8000;
    private const int DbtDeviceRemoveComplete = 0x8004;
    private const int DbtDevNodesChanged = 0x0007;

    private readonly MainViewModel _viewModel;
    private readonly DispatcherTimer _idlePortListRefreshTimer;
    private readonly DispatcherTimer _idleRunningSlotRefreshTimer;
    private readonly DispatcherTimer _deviceChangeRefreshTimer;
    private HwndSource? _hwndSource;

    public MainWindow(MainViewModel viewModel)
    {
        _viewModel = viewModel;
        DataContext = viewModel;

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

        Loaded += MainWindow_OnLoaded;
        Closing += MainWindow_Closing;
        SourceInitialized += MainWindow_OnSourceInitialized;
        _viewModel.PropertyChanged += MainViewModel_PropertyChanged;
        _viewModel.ViewMessageRequested += MainViewModel_ViewMessageRequested;
    }

    private async void MainWindow_OnLoaded(object sender, RoutedEventArgs e)
    {
        await _viewModel.InitializeAsync();
        UpdateIdleRefreshTimers();
    }

    private void MainWindow_Closing(object? sender, CancelEventArgs e)
    {
        _idlePortListRefreshTimer.Stop();
        _idleRunningSlotRefreshTimer.Stop();
        _deviceChangeRefreshTimer.Stop();
        _viewModel.PropertyChanged -= MainViewModel_PropertyChanged;
        _viewModel.ViewMessageRequested -= MainViewModel_ViewMessageRequested;

        if (_hwndSource is not null)
        {
            _hwndSource.RemoveHook(WndProc);
            _hwndSource = null;
        }
    }

    private void MainWindow_OnSourceInitialized(object? sender, EventArgs e)
    {
        _hwndSource = PresentationSource.FromVisual(this) as HwndSource;
        _hwndSource?.AddHook(WndProc);
    }

    private void MainViewModel_PropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(MainViewModel.ShouldPollPortList) or nameof(MainViewModel.ShouldPollRunningSlot))
        {
            UpdateIdleRefreshTimers();
        }
    }

    private void MainViewModel_ViewMessageRequested(object? sender, ViewMessage viewMessage)
    {
        var image = viewMessage.Severity switch
        {
            ViewMessageSeverity.Warning => MessageBoxImage.Warning,
            ViewMessageSeverity.Error => MessageBoxImage.Error,
            _ => MessageBoxImage.Information
        };

        MessageBox.Show(this, viewMessage.Message, viewMessage.Title, MessageBoxButton.OK, image);
    }

    private async void IdleRunningSlotRefreshTimer_OnTick(object? sender, EventArgs e)
    {
        await _viewModel.PollRunningSlotAsync();
    }

    private async void IdlePortListRefreshTimer_OnTick(object? sender, EventArgs e)
    {
        await _viewModel.PollPortListAsync();
    }

    private async void DeviceChangeRefreshTimer_OnTick(object? sender, EventArgs e)
    {
        _deviceChangeRefreshTimer.Stop();

        if (!_viewModel.ShouldPollPortList)
        {
            if (_viewModel.IsLocalModeActive)
            {
                _deviceChangeRefreshTimer.Start();
            }

            return;
        }

        await _viewModel.RefreshPortListAsync();
    }

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

    private void SchedulePortListRefresh()
    {
        if (!_viewModel.IsLocalModeActive)
        {
            return;
        }

        _deviceChangeRefreshTimer.Stop();
        _deviceChangeRefreshTimer.Start();
    }

    private void UpdateIdleRefreshTimers()
    {
        if (_viewModel.ShouldPollPortList)
        {
            _idlePortListRefreshTimer.Start();
        }
        else
        {
            _idlePortListRefreshTimer.Stop();
        }

        if (_viewModel.ShouldPollRunningSlot)
        {
            _idleRunningSlotRefreshTimer.Start();
        }
        else
        {
            _idleRunningSlotRefreshTimer.Stop();
        }
    }
}
