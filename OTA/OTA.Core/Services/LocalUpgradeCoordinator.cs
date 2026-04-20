using System.Globalization;
using OTA.Models;
using OTA.Protocols;

namespace OTA.Core;

/// <summary>
/// 协调本地升级前的参数校验、镜像识别、槽位探测和推荐逻辑。
/// </summary>
public sealed class LocalUpgradeCoordinator
{
    /// <summary>
    /// 解析并校验串口号、波特率和超时，生成统一的串口设置对象。
    /// </summary>
    public OperationResult<LocalSerialSettings> ReadSerialSettings(
        string portName,
        string baudRateText,
        string timeoutText)
    {
        if (string.IsNullOrWhiteSpace(portName))
        {
            return OperationResult<LocalSerialSettings>.Failure(OtaErrorCode.PortNameRequired, "请先选择串口号。");
        }

        if (!int.TryParse(baudRateText, NumberStyles.Integer, CultureInfo.InvariantCulture, out var baudRate) || baudRate <= 0)
        {
            return OperationResult<LocalSerialSettings>.Failure(OtaErrorCode.InvalidBaudRate, "波特率必须是正整数。");
        }

        if (!double.TryParse(timeoutText, NumberStyles.Float, CultureInfo.InvariantCulture, out var timeoutSeconds) || timeoutSeconds <= 0)
        {
            return OperationResult<LocalSerialSettings>.Failure(OtaErrorCode.InvalidTimeout, "超时必须是大于 0 的数字。");
        }

        return OperationResult<LocalSerialSettings>.Success(new LocalSerialSettings(portName, baudRate, timeoutSeconds));
    }

    /// <summary>
    /// 校验固件路径并组装升级执行参数。
    /// </summary>
    public OperationResult<LocalUpgradeOptions> BuildLocalUpgradeOptions(
        LocalSerialSettings serialSettings,
        string imagePath)
    {
        var normalizedPath = imagePath.Trim();
        if (string.IsNullOrWhiteSpace(normalizedPath))
        {
            return OperationResult<LocalUpgradeOptions>.Failure(OtaErrorCode.ImagePathRequired, "STM32 程序路径不能为空。");
        }

        if (!File.Exists(normalizedPath))
        {
            return OperationResult<LocalUpgradeOptions>.Failure(OtaErrorCode.ImageFileNotFound, $"STM32 程序不存在：{normalizedPath}");
        }

        return OperationResult<LocalUpgradeOptions>.Success(
            new LocalUpgradeOptions(serialSettings.PortName, serialSettings.BaudRate, serialSettings.TimeoutSeconds, normalizedPath));
    }

    /// <summary>
    /// 使用本地升级默认提示文案检查串口是否存在且可打开。
    /// </summary>
    public OperationResult ValidatePortAvailability(LocalSerialSettings serialSettings)
    {
        return ValidatePortAvailability(serialSettings, "串口 {0} 当前未连接。请先连接 USB 转 485 串口设备并确认端口号。");
    }

    /// <summary>
    /// 按指定模式文案检查串口是否存在且可打开。
    /// </summary>
    public OperationResult ValidatePortAvailability(
        LocalSerialSettings serialSettings,
        string disconnectedPortMessageTemplate)
    {
        string? localErrorMessage = null;
        OtaError? localError = null;
        var success = SerialOperationGate.Run(() =>
        {
            var availablePorts = System.IO.Ports.SerialPort.GetPortNames();
            if (!availablePorts.Any(portName => string.Equals(portName, serialSettings.PortName, StringComparison.OrdinalIgnoreCase)))
            {
                localErrorMessage = string.Format(
                    CultureInfo.InvariantCulture,
                    disconnectedPortMessageTemplate,
                    serialSettings.PortName);
                localError = OtaErrorMapper.Create(OtaErrorCode.PortUnavailable, localErrorMessage);
                return false;
            }

            if (!SerialPortHelper.TryOpen(serialSettings.PortName, serialSettings.BaudRate, out var serialPort, out var openErrorMessage))
            {
                localErrorMessage = openErrorMessage ?? $"无法打开串口 {serialSettings.PortName}。";
                localError = OtaErrorMapper.MapPortOpenFailure(localErrorMessage);
                return false;
            }

            serialPort?.Dispose();
            return true;
        });

        if (success)
        {
            return OperationResult.Success();
        }

        return OperationResult.Failure(localError ?? OtaErrorMapper.Create(
            OtaErrorCode.PortOpenFailed,
            localErrorMessage ?? $"无法打开串口 {serialSettings.PortName}。"));
    }

    /// <summary>
    /// 检查镜像文件并识别其槽位信息。
    /// </summary>
    public OperationResult<FirmwareImageInfo> InspectImage(string imagePath)
    {
        try
        {
            return OperationResult<FirmwareImageInfo>.Success(UpgradeAbSupport.InspectImage(imagePath));
        }
        catch (Exception ex)
        {
            return OperationResult<FirmwareImageInfo>.Failure(OtaErrorMapper.MapImageInspectionException(ex));
        }
    }

    /// <summary>
    /// 通过串口读取当前设备运行槽位，并转换为界面可用的提示结果。
    /// </summary>
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
                    result.errorMessage is null ? null : OtaErrorMapper.MapRunningSlotFailure(result.errorMessage));
            }

            if (result.slot == FirmwareSlot.Unknown)
            {
                return new RunningSlotRefreshResult(
                    FirmwareSlot.Unknown,
                    "设备返回槽位 unknown，请手动选择另一槽镜像。",
                    false,
                    OtaErrorMapper.Create(OtaErrorCode.RunningSlotUnknown, "设备返回槽位 unknown，请手动选择另一槽镜像。"));
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
                OtaErrorMapper.MapRunningSlotFailure(ex.Message));
        }
    }

    /// <summary>
    /// 升级前校验镜像与槽位关系，并生成启动日志内容。
    /// </summary>
    public OperationResult<LocalUpgradePreparation> PrepareLocalUpgrade(
        LocalUpgradeOptions options,
        FirmwareSlot runningSlot,
        string modeName)
    {
        var imageResult = InspectImage(options.ImagePath);
        if (!imageResult.IsSuccess)
        {
            return OperationResult<LocalUpgradePreparation>.Failure(imageResult.Error!);
        }

        var imageInfo = imageResult.Value;
        if (runningSlot != FirmwareSlot.Unknown && imageInfo.DetectedSlot == runningSlot)
        {
            var recommendedFile = UpgradeAbSupport.GetRecommendedFileName(runningSlot);
            return OperationResult<LocalUpgradePreparation>.Failure(
                OtaErrorCode.SameSlotUpgradeNotAllowed,
                $"设备当前运行槽位为 {runningSlot.ToDisplayText()}，不能继续发送同槽镜像。请改选 {recommendedFile}。");
        }

        return OperationResult<LocalUpgradePreparation>.Success(
            new LocalUpgradePreparation(options, imageInfo, runningSlot, BuildStartupMessages(modeName, runningSlot, imageInfo)));
    }

    /// <summary>
    /// 生成镜像识别提示文本，供界面显示。
    /// </summary>
    public string BuildImageHint(FirmwareImageInfo imageInfo)
    {
        return UpgradeAbSupport.BuildImageHint(imageInfo);
    }

    /// <summary>
    /// 根据当前运行槽位生成推荐镜像提示。
    /// </summary>
    public string BuildSlotRecommendationText(FirmwareSlot runningSlot)
    {
        return runningSlot switch
        {
            FirmwareSlot.A => "设备当前运行 A，建议选择 App_B.bin。",
            FirmwareSlot.B => "设备当前运行 B，建议选择 App_A.bin。",
            _ => "未读到槽位，请手动选择另一槽镜像。"
        };
    }

    /// <summary>
    /// 根据运行槽位计算推荐的本地镜像完整路径。
    /// </summary>
    public string GetRecommendedLocalImagePath(string objectsDirectory, FirmwareSlot runningSlot)
    {
        if (TryFindSlotImagePaths(objectsDirectory, out var slotAPath, out var slotBPath))
        {
            return runningSlot switch
            {
                FirmwareSlot.A when !string.IsNullOrWhiteSpace(slotBPath) => slotBPath,
                FirmwareSlot.B when !string.IsNullOrWhiteSpace(slotAPath) => slotAPath,
                _ when !string.IsNullOrWhiteSpace(slotAPath) => slotAPath,
                _ when !string.IsNullOrWhiteSpace(slotBPath) => slotBPath,
                _ => Path.Combine(objectsDirectory, UpgradeAbSupport.GetRecommendedFileName(runningSlot))
            };
        }

        return Path.Combine(objectsDirectory, UpgradeAbSupport.GetRecommendedFileName(runningSlot));
    }

    /// <summary>
    /// 从目录中选择合适的升级镜像文件，并补全其完整路径。
    /// </summary>
    public bool TryResolveLocalImagePathForDirectory(
        string imageDirectory,
        FirmwareSlot runningSlot,
        string? currentImagePath,
        out string imagePath,
        out string errorMessage)
    {
        var normalizedDirectory = imageDirectory.Trim();
        imagePath = string.Empty;

        if (string.IsNullOrWhiteSpace(normalizedDirectory))
        {
            errorMessage = "程序目录不能为空。";
            return false;
        }

        if (!Directory.Exists(normalizedDirectory))
        {
            errorMessage = $"程序目录不存在：{normalizedDirectory}";
            return false;
        }

        if (!TryFindSlotImagePaths(normalizedDirectory, out var slotAPath, out var slotBPath))
        {
            errorMessage = $"所选目录及其 MDK-ARM\\Objects 子目录中未找到 App_A.bin 或 App_B.bin：{normalizedDirectory}";
            return false;
        }

        var hasSlotA = !string.IsNullOrWhiteSpace(slotAPath);
        var hasSlotB = !string.IsNullOrWhiteSpace(slotBPath);

        if (!hasSlotA && !hasSlotB)
        {
            errorMessage = $"所选目录及其 MDK-ARM\\Objects 子目录中未找到 App_A.bin 或 App_B.bin：{normalizedDirectory}";
            return false;
        }

        imagePath = runningSlot switch
        {
            FirmwareSlot.A when hasSlotB => slotBPath,
            FirmwareSlot.B when hasSlotA => slotAPath,
            _ => ResolvePreferredImagePath(currentImagePath, slotAPath, slotBPath)
        };

        if (!string.IsNullOrWhiteSpace(imagePath) && File.Exists(imagePath))
        {
            errorMessage = string.Empty;
            return true;
        }

        var targetFileName = runningSlot switch
        {
            FirmwareSlot.A => "App_B.bin",
            FirmwareSlot.B => "App_A.bin",
            _ => "App_A.bin / App_B.bin"
        };

        errorMessage = $"已找到镜像目录，但缺少 {targetFileName}。请确认目录内包含正确的升级文件。";
        return false;
    }

    /// <summary>
    /// 判断当前界面路径是否仍允许被自动推荐镜像覆盖。
    /// </summary>
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

    /// <summary>
    /// 生成升级启动阶段的日志文本。
    /// </summary>
    private static IReadOnlyList<string> BuildStartupMessages(string modeName, FirmwareSlot runningSlot, FirmwareImageInfo imageInfo)
    {
        var messages = new List<string>
        {
            $"准备升级，模式 {modeName}。"
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

    /// <summary>
    /// 在槽位未知时，根据当前文件名和目录内容决定优先使用哪一个镜像。
    /// </summary>
    private static string ResolvePreferredImagePath(string? currentImagePath, string? slotAPath, string? slotBPath)
    {
        var currentFileName = Path.GetFileName(currentImagePath ?? string.Empty);
        if (string.Equals(currentFileName, "App_A.bin", StringComparison.OrdinalIgnoreCase) &&
            !string.IsNullOrWhiteSpace(slotAPath))
        {
            return slotAPath;
        }

        if (string.Equals(currentFileName, "App_B.bin", StringComparison.OrdinalIgnoreCase) &&
            !string.IsNullOrWhiteSpace(slotBPath))
        {
            return slotBPath;
        }

        if (!string.IsNullOrWhiteSpace(slotAPath))
        {
            return slotAPath;
        }

        return slotBPath ?? string.Empty;
    }

    /// <summary>
    /// 在用户所选目录中分别定位 A/B 槽镜像文件。
    /// </summary>
    private static bool TryFindSlotImagePaths(string selectedDirectory, out string slotAPath, out string slotBPath)
    {
        slotAPath = string.Empty;
        slotBPath = string.Empty;

        foreach (var searchRoot in EnumerateCandidateSearchRoots(selectedDirectory))
        {
            TryAssignSlotImagePath(searchRoot, "App_A.bin", ref slotAPath);
            TryAssignSlotImagePath(searchRoot, "App_B.bin", ref slotBPath);

            if (!string.IsNullOrWhiteSpace(slotAPath) || !string.IsNullOrWhiteSpace(slotBPath))
            {
                return true;
            }
        }

        return false;
    }

    /// <summary>
    /// 枚举可用于升级镜像查找的搜索根目录。
    /// </summary>
    private static IEnumerable<string> EnumerateCandidateSearchRoots(string selectedDirectory)
    {
        var yieldedDirectories = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        if (Directory.Exists(selectedDirectory) && yieldedDirectories.Add(selectedDirectory))
        {
            yield return selectedDirectory;
        }

        var currentDirectory = new DirectoryInfo(selectedDirectory);
        while (currentDirectory is not null)
        {
            if (IsMdkArmObjectsDirectory(currentDirectory.FullName) &&
                yieldedDirectories.Add(currentDirectory.FullName))
            {
                yield return currentDirectory.FullName;
                break;
            }

            currentDirectory = currentDirectory.Parent;
        }

        var directObjectsDirectory = Path.Combine(selectedDirectory, "Objects");
        if (IsMdkArmObjectsDirectory(directObjectsDirectory) && yieldedDirectories.Add(directObjectsDirectory))
        {
            yield return directObjectsDirectory;
        }

        IEnumerable<string> recursiveObjectsDirectories;
        try
        {
            recursiveObjectsDirectories = Directory
                .EnumerateDirectories(selectedDirectory, "Objects", SearchOption.AllDirectories)
                .Where(IsMdkArmObjectsDirectory)
                .OrderBy(path => path.Length)
                .ThenBy(path => path, StringComparer.OrdinalIgnoreCase);
        }
        catch (IOException)
        {
            yield break;
        }
        catch (UnauthorizedAccessException)
        {
            yield break;
        }

        foreach (var candidateDirectory in recursiveObjectsDirectories)
        {
            if (yieldedDirectories.Add(candidateDirectory))
            {
                yield return candidateDirectory;
            }
        }
    }

    /// <summary>
    /// 判断目录是否为 MDK-ARM\Objects。
    /// </summary>
    private static bool IsMdkArmObjectsDirectory(string directoryPath)
    {
        if (!Directory.Exists(directoryPath))
        {
            return false;
        }

        var directoryName = Path.GetFileName(directoryPath);
        if (!string.Equals(directoryName, "Objects", StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        var parentDirectory = Directory.GetParent(directoryPath);
        return string.Equals(parentDirectory?.Name, "MDK-ARM", StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>
    /// 在指定搜索根目录下查找目标镜像文件。
    /// </summary>
    private static void TryAssignSlotImagePath(string searchRoot, string fileName, ref string resolvedPath)
    {
        if (!string.IsNullOrWhiteSpace(resolvedPath) || !Directory.Exists(searchRoot))
        {
            return;
        }

        var directFilePath = Path.Combine(searchRoot, fileName);
        if (File.Exists(directFilePath))
        {
            resolvedPath = directFilePath;
            return;
        }

        try
        {
            resolvedPath = Directory
                .EnumerateFiles(searchRoot, fileName, SearchOption.AllDirectories)
                .OrderBy(path => path.Length)
                .ThenBy(path => path, StringComparer.OrdinalIgnoreCase)
                .FirstOrDefault() ?? string.Empty;
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }
}
