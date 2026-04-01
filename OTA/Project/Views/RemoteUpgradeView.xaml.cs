// RemoteUpgradeView.xaml.cs
// 放远程升级页面事件处理和工作流调用，保持界面层只做轻量转发。

using System.Windows;
using System.Windows.Controls;

namespace Project;

/// <summary>
/// 远程升级视图。
/// </summary>
public partial class RemoteUpgradeView : UserControl
{
    private readonly RemoteUpgradeState _state = new();
    private RemoteUpgradeWorkflow? _workflow;

    public RemoteUpgradeView()
    {
        InitializeComponent();
        DataContext = _state;
    }

    /// <summary>
    /// 为视图注入主窗口设备桥接。
    /// </summary>
    internal void Initialize(IRemoteUpgradeDeviceBridge deviceBridge)
    {
        ArgumentNullException.ThrowIfNull(deviceBridge);

        if (_workflow is not null)
        {
            return;
        }

        _workflow = new RemoteUpgradeWorkflow(
            _state,
            new RemoteUpgradeApiClient(),
            new RemotePackageStore(),
            deviceBridge,
            AppendLogSafe);
    }

    /// <summary>
    /// 刷新当前设备信息。
    /// 供主窗口切换到远程页时调用。
    /// </summary>
    internal async Task RefreshDeviceContextAsync()
    {
        if (_workflow is null)
        {
            return;
        }

        try
        {
            await _workflow.RefreshDeviceContextAsync();
        }
        catch (Exception ex)
        {
            AppendLogSafe($"设备信息刷新失败：{ex.Message}");
            _state.StatusText = "失败";
        }
    }

    private async void RefreshDeviceButton_OnClick(object sender, RoutedEventArgs e)
    {
        await RunWorkflowAsync(workflow => workflow.RefreshDeviceContextAsync());
    }

    private async void CheckUpdatesButton_OnClick(object sender, RoutedEventArgs e)
    {
        await RunWorkflowAsync(workflow => workflow.CheckForUpdatesAsync());
    }

    private async void DownloadPackageButton_OnClick(object sender, RoutedEventArgs e)
    {
        await RunWorkflowAsync(workflow => workflow.DownloadSelectedPackageAsync());
    }

    private async void StartUpgradeButton_OnClick(object sender, RoutedEventArgs e)
    {
        await RunWorkflowAsync(workflow => workflow.DownloadAndUpgradeAsync());
    }

    private async Task RunWorkflowAsync(Func<RemoteUpgradeWorkflow, Task> action)
    {
        if (_workflow is null)
        {
            MessageBox.Show(Window.GetWindow(this), "远程升级模块尚未初始化。", "模块错误", MessageBoxButton.OK, MessageBoxImage.Warning);
            return;
        }

        try
        {
            await action(_workflow);
        }
        catch (Exception ex)
        {
            AppendLogSafe($"失败：{ex.Message}");
            _state.StatusText = "失败";
            MessageBox.Show(Window.GetWindow(this), ex.Message, "远程升级失败", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void AppendLogSafe(string message)
    {
        if (!Dispatcher.CheckAccess())
        {
            Dispatcher.Invoke(() => _state.AppendLog(message));
            return;
        }

        _state.AppendLog(message);
    }
}
