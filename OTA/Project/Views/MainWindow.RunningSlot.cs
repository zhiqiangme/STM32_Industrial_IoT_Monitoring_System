// MainWindow.RunningSlot.cs
// 放运行槽位读取、定时器轮询、槽位 UI 更新。

using System.Globalization;
using System.Windows;
using System.Windows.Controls;

namespace Project;

public partial class MainWindow : Window
{
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
            var result = await _runningSlotService.ReadAsync(portName, baudRate, timeoutSeconds);

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
}
