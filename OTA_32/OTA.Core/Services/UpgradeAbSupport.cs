using System.Buffers.Binary;
using OTA.Models;
using OTA.Protocols;

namespace OTA.Core;

/// <summary>
/// A/B 升级辅助类。
/// 对外维持原有调用入口，并把镜像识别与运行槽位读取职责分别委托到 Core/Protocols。
/// </summary>
public static class UpgradeAbSupport
{
    private const uint SlotABaseAddress = 0x08008000u;
    private const uint SlotBBaseAddress = 0x08043000u;
    private const uint SlotMaxSize = 0x0003B000u;

    public static bool TryReadRunningSlot(string portName, int baudRate, TimeSpan timeout, out FirmwareSlot slot, out string? errorMessage)
    {
        return RunningSlotProtocol.TryReadRunningSlot(portName, baudRate, timeout, out slot, out errorMessage);
    }

    public static FirmwareSlot ReadRunningSlot(string portName, int baudRate, TimeSpan timeout)
    {
        return RunningSlotProtocol.ReadRunningSlot(portName, baudRate, timeout);
    }

    public static FirmwareImageInfo InspectImage(string imagePath)
    {
        using var stream = File.OpenRead(imagePath);
        if (stream.Length < 8)
        {
            throw new InvalidOperationException("BIN 文件长度不足 8 字节，无法识别向量表。");
        }

        Span<byte> header = stackalloc byte[8];
        var read = stream.Read(header);
        if (read < header.Length)
        {
            throw new InvalidOperationException("读取 BIN 文件头失败。");
        }

        var initialStackPointer = BinaryPrimitives.ReadUInt32LittleEndian(header[..4]);
        var resetHandler = BinaryPrimitives.ReadUInt32LittleEndian(header[4..8]);
        var fileNameSlot = InferSlotFromFileName(Path.GetFileName(imagePath));
        var vectorSlot = ResolveSlotFromAddress(resetHandler);

        if (fileNameSlot != FirmwareSlot.Unknown &&
            vectorSlot != FirmwareSlot.Unknown &&
            fileNameSlot != vectorSlot)
        {
            throw new InvalidOperationException(
                $"文件名指向槽位 {fileNameSlot.ToDisplayText()}，但复位向量 0x{resetHandler:X8} 落在槽位 {vectorSlot.ToDisplayText()}。");
        }

        var detectedSlot = vectorSlot != FirmwareSlot.Unknown ? vectorSlot : fileNameSlot;
        if (detectedSlot == FirmwareSlot.Unknown)
        {
            throw new InvalidOperationException(
                $"无法识别镜像槽位，复位向量为 0x{resetHandler:X8}，文件名也不包含 App_A.bin / App_B.bin。");
        }

        return new FirmwareImageInfo(imagePath, detectedSlot, fileNameSlot, vectorSlot, initialStackPointer, resetHandler);
    }

    public static string GetRecommendedFileName(FirmwareSlot runningSlot)
    {
        return runningSlot switch
        {
            FirmwareSlot.A => "App_B.bin",
            FirmwareSlot.B => "App_A.bin",
            _ => "App_A.bin"
        };
    }

    public static string BuildImageHint(FirmwareImageInfo imageInfo)
    {
        var details = new List<string>
        {
            $"镜像槽位：{imageInfo.DetectedSlot.ToDisplayText()}"
        };

        if (imageInfo.FileNameSlot != FirmwareSlot.Unknown)
        {
            details.Add($"文件名={imageInfo.FileNameSlot.ToDisplayText()}");
        }

        if (imageInfo.VectorSlot != FirmwareSlot.Unknown)
        {
            details.Add($"向量表={imageInfo.VectorSlot.ToDisplayText()}");
        }

        details.Add($"Reset=0x{imageInfo.ResetHandler:X8}");
        return string.Join("，", details);
    }

    private static FirmwareSlot InferSlotFromFileName(string? fileName)
    {
        if (string.IsNullOrWhiteSpace(fileName))
        {
            return FirmwareSlot.Unknown;
        }

        var upperName = fileName.ToUpperInvariant();
        if (upperName.Contains("APP_A", StringComparison.Ordinal))
        {
            return FirmwareSlot.A;
        }

        if (upperName.Contains("APP_B", StringComparison.Ordinal))
        {
            return FirmwareSlot.B;
        }

        return FirmwareSlot.Unknown;
    }

    private static FirmwareSlot ResolveSlotFromAddress(uint address)
    {
        if (address >= SlotABaseAddress && address < SlotABaseAddress + SlotMaxSize)
        {
            return FirmwareSlot.A;
        }

        if (address >= SlotBBaseAddress && address < SlotBBaseAddress + SlotMaxSize)
        {
            return FirmwareSlot.B;
        }

        return FirmwareSlot.Unknown;
    }
}
