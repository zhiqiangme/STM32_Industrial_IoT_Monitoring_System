using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Effects;
using System.Windows.Threading;
using OTA.Core;
using OTA.Models;
using OTA.ViewModels;
using OTA.ViewModels.Messages;
using Button = System.Windows.Controls.Button;
using Brush = System.Windows.Media.Brush;
using Brushes = System.Windows.Media.Brushes;
using Color = System.Windows.Media.Color;
using ComboBox = System.Windows.Controls.ComboBox;
using MessageBox = System.Windows.MessageBox;
using TextBox = System.Windows.Controls.TextBox;

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
    private readonly AppPreferencesService _preferencesService;
    private readonly DispatcherTimer _idlePortListRefreshTimer;
    private readonly DispatcherTimer _idleRunningSlotRefreshTimer;
    private readonly DispatcherTimer _deviceChangeRefreshTimer;
    private HwndSource? _hwndSource;

    public MainWindow(MainViewModel viewModel, AppPreferencesService preferencesService)
    {
        _viewModel = viewModel;
        _preferencesService = preferencesService;
        DataContext = viewModel;

        InitializeComponent();
        ApplyWindowPreferences();

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
        _viewModel.LocalVM.PropertyChanged += LocalViewModel_PropertyChanged;
        _viewModel.LocalVM.ViewMessageRequested += LocalViewModel_ViewMessageRequested;
    }

    private async void MainWindow_OnLoaded(object sender, RoutedEventArgs e)
    {
        UpdateModeButtonStyles();
        await _viewModel.LocalVM.InitializeAsync();
        UpdateIdleRefreshTimers();
    }

    private void MainWindow_Closing(object? sender, CancelEventArgs e)
    {
        SaveWindowPreferences();
        _idlePortListRefreshTimer.Stop();
        _idleRunningSlotRefreshTimer.Stop();
        _deviceChangeRefreshTimer.Stop();
        _viewModel.PropertyChanged -= MainViewModel_PropertyChanged;
        _viewModel.LocalVM.PropertyChanged -= LocalViewModel_PropertyChanged;
        _viewModel.LocalVM.ViewMessageRequested -= LocalViewModel_ViewMessageRequested;

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
        if (e.PropertyName is nameof(MainViewModel.SelectedTabIndex) or nameof(MainViewModel.IsLocalTabSelected))
        {
            UpdateModeButtonStyles();
            UpdateIdleRefreshTimers();
        }
    }

    private void LocalViewModel_PropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(LocalUpgradeViewModel.ShouldPollPortList) or nameof(LocalUpgradeViewModel.ShouldPollRunningSlot))
        {
            UpdateIdleRefreshTimers();
        }
    }

    private void LocalViewModel_ViewMessageRequested(object? sender, ViewMessage viewMessage)
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
        if (_viewModel.IsLocalTabSelected)
        {
            await _viewModel.LocalVM.PollRunningSlotAsync();
        }
    }

    private async void IdlePortListRefreshTimer_OnTick(object? sender, EventArgs e)
    {
        if (_viewModel.IsLocalTabSelected)
        {
            await _viewModel.LocalVM.PollPortListAsync();
        }
    }

    private async void DeviceChangeRefreshTimer_OnTick(object? sender, EventArgs e)
    {
        _deviceChangeRefreshTimer.Stop();

        if (!_viewModel.LocalVM.ShouldPollPortList)
        {
            if (_viewModel.IsLocalTabSelected)
            {
                _deviceChangeRefreshTimer.Start();
            }

            return;
        }

        await _viewModel.LocalVM.RefreshPortListAsync();
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
        if (!_viewModel.IsLocalTabSelected)
        {
            return;
        }

        _deviceChangeRefreshTimer.Stop();
        _deviceChangeRefreshTimer.Start();
    }

    private void UpdateIdleRefreshTimers()
    {
        if (_viewModel.IsLocalTabSelected && _viewModel.LocalVM.ShouldPollPortList)
        {
            _idlePortListRefreshTimer.Start();
        }
        else
        {
            _idlePortListRefreshTimer.Stop();
        }

        if (_viewModel.IsLocalTabSelected && _viewModel.LocalVM.ShouldPollRunningSlot)
        {
            _idleRunningSlotRefreshTimer.Start();
        }
        else
        {
            _idleRunningSlotRefreshTimer.Stop();
        }
    }

    private void ApplyWindowPreferences()
    {
        var preferences = _preferencesService.GetWindowPreferences();

        if (preferences.Width is > 0)
        {
            Width = Math.Max(MinWidth, preferences.Width.Value);
        }

        if (preferences.Height is > 0)
        {
            Height = Math.Max(MinHeight, preferences.Height.Value);
        }

        if (preferences.Left.HasValue && preferences.Top.HasValue)
        {
            WindowStartupLocation = WindowStartupLocation.Manual;
            Left = preferences.Left.Value;
            Top = preferences.Top.Value;
        }

        if (preferences.IsMaximized)
        {
            WindowState = WindowState.Maximized;
        }
    }

    private void SaveWindowPreferences()
    {
        var bounds = WindowState == WindowState.Normal ? new Rect(Left, Top, Width, Height) : RestoreBounds;

        var preferences = new WindowPreferences
        {
            Left = bounds.Left,
            Top = bounds.Top,
            Width = bounds.Width,
            Height = bounds.Height,
            IsMaximized = WindowState == WindowState.Maximized
        };

        _preferencesService.SaveWindowPreferences(preferences);
    }

    private void LocalModeButton_OnClick(object sender, RoutedEventArgs e)
    {
        if (_viewModel.SelectedTabIndex == 0)
        {
            return;
        }

        _viewModel.SelectedTabIndex = 0;
    }

    private void RemoteModeButton_OnClick(object sender, RoutedEventArgs e)
    {
        if (_viewModel.SelectedTabIndex == 1)
        {
            return;
        }

        _viewModel.SelectedTabIndex = 1;
    }

    private void MaintenanceModeButton_OnClick(object sender, RoutedEventArgs e)
    {
        if (_viewModel.SelectedTabIndex == 2)
        {
            return;
        }

        _viewModel.SelectedTabIndex = 2;
    }

    private void UpdateModeButtonStyles()
    {
        ApplyModeButtonStyle(LocalModeButton, _viewModel.SelectedTabIndex == 0);
        ApplyModeButtonStyle(RemoteModeButton, _viewModel.SelectedTabIndex == 1);
        ApplyModeButtonStyle(MaintenanceModeButton, _viewModel.SelectedTabIndex == 2);
    }

    private static void ApplyModeButtonStyle(Button button, bool isActive)
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
            button.Background = (Brush)button.FindResource("TitleBarBrush");
            button.BorderBrush = Brushes.Transparent;
            button.Foreground = new SolidColorBrush(Color.FromRgb(75, 85, 99));
            button.Effect = null;
        }
    }

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
        }
    }

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

    private void CloseButton_Click(object sender, RoutedEventArgs e)
    {
        Close();
    }

    private void MinimizeButton_Click(object sender, RoutedEventArgs e)
    {
        WindowState = WindowState.Minimized;
    }
}
