using System.Globalization;
using OTA.Models;

namespace OTA.Core;

/// <summary>
/// 协调本地升级前的参数校验、镜像识别、槽位探测和推荐逻辑。
/// </summary>
public sealed class LocalUpgradeCoordinator
{
    public bool TryReadSerialSettings(
        string portName,
        string baudRateText,
        string timeoutText,
        out LocalSerialSettings settings,
        out string errorMessage)
    {
        settings = default;

        if (string.IsNullOrWhiteSpace(portName))
        {
            errorMessage = "请先选择串口号。";
            return false;
        }

        if (!int.TryParse(baudRateText, NumberStyles.Integer, CultureInfo.InvariantCulture, out var baudRate) || baudRate <= 0)
        {
            errorMessage = "波特率必须是正整数。";
            return false;
        }

        if (!double.TryParse(timeoutText, NumberStyles.Float, CultureInfo.InvariantCulture, out var timeoutSeconds) || timeoutSeconds <= 0)
        {
            errorMessage = "超时必须是大于 0 的数字。";
            return false;
        }

        settings = new LocalSerialSettings(portName, baudRate, timeoutSeconds);
        errorMessage = string.Empty;
        return true;
    }

    public bool TryBuildLocalUpgradeOptions(
        LocalSerialSettings serialSettings,
        string imagePath,
        out LocalUpgradeOptions options,
        out string errorMessage)
    {
        options = default;

        var normalizedPath = imagePath.Trim();
        if (string.IsNullOrWhiteSpace(normalizedPath))
        {
            errorMessage = "STM32 程序路径不能为空。";
            return false;
        }

        if (!File.Exists(normalizedPath))
        {
            errorMessage = $"STM32 程序不存在：{normalizedPath}";
            return false;
        }

        options = new LocalUpgradeOptions(serialSettings.PortName, serialSettings.BaudRate, serialSettings.TimeoutSeconds, normalizedPath);
        errorMessage = string.Empty;
        return true;
    }

    public bool TryInspectImage(string imagePath, out FirmwareImageInfo imageInfo, out string errorMessage)
    {
        try
        {
            imageInfo = UpgradeAbSupport.InspectImage(imagePath);
            errorMessage = string.Empty;
            return true;
        }
        catch (Exception ex)
        {
            imageInfo = default;
            errorMessage = $"STM32 程序槽位识别失败：{ex.Message}";
            return false;
        }
    }

    public async Task<RunningSlotRefreshResult> ReadRunningSlotAsync(LocalSerialSettings serialSettings)
    {
        try
        {
            var timeout = TimeSpan.FromSeconds(Math.Clamp(serialSettings.TimeoutSeconds, 1d, 10d));
            var result = await Task.Run(() =>
            {
                var success = UpgradeAbSupport.TryReadRunningSlot(serialSettings.PortName, serialSettings.BaudRate, timeout, out var slot, out var errorMessage);
                return (success, slot, errorMessage);
            });

            if (!result.success)
            {
                return new RunningSlotRefreshResult(
                    FirmwareSlot.Unknown,
                    "未读到槽位，请手动选择另一槽镜像。",
                    false,
                    result.errorMessage);
            }

            if (result.slot == FirmwareSlot.Unknown)
            {
                return new RunningSlotRefreshResult(
                    FirmwareSlot.Unknown,
                    "设备返回槽位 unknown，请手动选择另一槽镜像。",
                    false);
            }

            return new RunningSlotRefreshResult(
                result.slot,
                BuildSlotRecommendationText(result.slot),
                false);
        }
        catch (Exception ex)
        {
            return new RunningSlotRefreshResult(
                FirmwareSlot.Unknown,
                "未读到槽位，请手动选择另一槽镜像。",
                false,
                ex.Message);
        }
    }

    public bool TryPrepareLocalUpgrade(
        LocalUpgradeOptions options,
        FirmwareSlot runningSlot,
        out LocalUpgradePreparation preparation,
        out string errorMessage)
    {
        preparation = default;

        if (!TryInspectImage(options.ImagePath, out var imageInfo, out errorMessage))
        {
            return false;
        }

        if (runningSlot != FirmwareSlot.Unknown && imageInfo.DetectedSlot == runningSlot)
        {
            var recommendedFile = UpgradeAbSupport.GetRecommendedFileName(runningSlot);
            errorMessage = $"设备当前运行槽位为 {runningSlot.ToDisplayText()}，不能继续发送同槽镜像。请改选 {recommendedFile}。";
            return false;
        }

        preparation = new LocalUpgradePreparation(options, imageInfo, runningSlot, BuildStartupMessages(runningSlot, imageInfo));
        errorMessage = string.Empty;
        return true;
    }

    public string BuildImageHint(FirmwareImageInfo imageInfo)
    {
        return UpgradeAbSupport.BuildImageHint(imageInfo);
    }

    public string BuildSlotRecommendationText(FirmwareSlot runningSlot)
    {
        return runningSlot switch
        {
            FirmwareSlot.A => "设备当前运行 A，建议选择 App_B.bin。",
            FirmwareSlot.B => "设备当前运行 B，建议选择 App_A.bin。",
            _ => "未读到槽位，请手动选择另一槽镜像。"
        };
    }

    public string GetRecommendedLocalImagePath(string objectsDirectory, FirmwareSlot runningSlot)
    {
        return Path.Combine(objectsDirectory, UpgradeAbSupport.GetRecommendedFileName(runningSlot));
    }

    public bool ShouldAutoApplyRecommendedPath(
        string currentPath,
        string? lastAutoSuggestedPath,
        string slotAImagePath,
        string slotBImagePath)
    {
        if (string.IsNullOrWhiteSpace(currentPath))
        {
            return true;
        }

        if (!string.IsNullOrWhiteSpace(lastAutoSuggestedPath) &&
            string.Equals(currentPath, lastAutoSuggestedPath, StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        if (string.Equals(currentPath, slotAImagePath, StringComparison.OrdinalIgnoreCase) ||
            string.Equals(currentPath, slotBImagePath, StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        return string.Equals(Path.GetFileName(currentPath), "App.bin", StringComparison.OrdinalIgnoreCase);
    }

    private static IReadOnlyList<string> BuildStartupMessages(FirmwareSlot runningSlot, FirmwareImageInfo imageInfo)
    {
        var messages = new List<string>
        {
            "准备升级，模式 本地升级。"
        };

        if (runningSlot == FirmwareSlot.Unknown)
        {
            messages.Add("警告: 未读取到当前运行槽位，将按所选镜像继续升级。");
        }
        else
        {
            messages.Add($"当前运行槽：{runningSlot.ToDisplayText()}。");
            messages.Add($"推荐镜像：{UpgradeAbSupport.GetRecommendedFileName(runningSlot)}。");
        }

        messages.Add($"所选镜像槽：{imageInfo.DetectedSlot.ToDisplayText()}。");
        messages.Add($"镜像复位向量：0x{imageInfo.ResetHandler:X8}。");
        return messages;
    }
}
