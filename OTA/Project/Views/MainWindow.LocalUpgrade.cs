// MainWindow.LocalUpgrade.cs
// 放本地升级入口、参数校验、浏览固件、镜像提示、推荐路径。

using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;

namespace Project;

public partial class MainWindow : Window
{
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
}
