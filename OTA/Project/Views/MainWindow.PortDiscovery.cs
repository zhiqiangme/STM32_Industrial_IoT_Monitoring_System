// MainWindow.PortDiscovery.cs
// 放串口刷新和界面绑定，底层发现逻辑下沉到 PortDiscoveryService。

using System.Windows;

namespace Project;

public partial class MainWindow : Window
{
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
    /// 刷新串口列表，并尽量保留或智能切换当前选中项。
    /// </summary>
    private async Task RefreshPortListAsync()
    {
        await _portListRefreshLock.WaitAsync();
        _isRefreshingPortList = true;
        var currentPort = GetCurrentPortName();

        try
        {
            var portOptions = await _portDiscoveryService.GetPortOptionsAsync();

            PortOption? selectedOption;
            // 刷新数据源时会触发 SelectionChanged，这里先临时压住，避免重复读取槽位。
            _suppressPortSelectionRefresh = true;
            try
            {
                PortComboBox.ItemsSource = portOptions;
                selectedOption = _portDiscoveryService.SelectPreferredPort(portOptions, currentPort, _knownPortNames);

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
    /// 从下拉框当前状态中提取串口号。
    /// 兼容“选中对象”和“手工输入文本”两种来源。
    /// </summary>
    private string GetCurrentPortName()
    {
        if (PortComboBox.SelectedItem is PortOption option)
        {
            return option.PortName;
        }

        return PortDiscoveryService.NormalizePortName(PortComboBox.Text);
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
}
