using System.Buffers.Binary;
using System.IO;
using System.IO.Ports;

namespace Project;

internal enum FirmwareSlot
{
    Unknown = 0,
    A = 1,
    B = 2
}

internal readonly record struct FirmwareImageInfo(
    string ImagePath,
    FirmwareSlot DetectedSlot,
    FirmwareSlot FileNameSlot,
    FirmwareSlot VectorSlot,
    uint InitialStackPointer,
    uint ResetHandler);

internal static class FirmwareSlotExtensions
{
    public static string ToDisplayText(this FirmwareSlot slot)
    {
        return slot switch
        {
            FirmwareSlot.A => "A",
            FirmwareSlot.B => "B",
            _ => "未知"
        };
    }

    public static FirmwareSlot GetOpposite(this FirmwareSlot slot)
    {
        return slot switch
        {
            FirmwareSlot.A => FirmwareSlot.B,
            FirmwareSlot.B => FirmwareSlot.A,
            _ => FirmwareSlot.Unknown
        };
    }
}

internal static class UpgradeAbSupport
{
    private const byte DeviceAddress = 0x0A;
    private const byte ReadHoldingRegisters = 0x03;
    private const ushort RunningSlotRegisterAddress = 0x005A;
    private const ushort RunningSlotRegisterCount = 0x0001;
    private const int RunningSlotResponseLength = 7;

    private const uint SlotABaseAddress = 0x08008000u;
    private const uint SlotBBaseAddress = 0x08043000u;
    private const uint SlotMaxSize = 0x0003B000u;

    public static FirmwareSlot ReadRunningSlot(string portName, int baudRate, TimeSpan timeout)
    {
        using var serialPort = SerialPortHelper.Open(portName, baudRate);
        Thread.Sleep(200);
        serialPort.DiscardInBuffer();
        serialPort.DiscardOutBuffer();

        var request = BuildReadHoldingRegistersRequest(RunningSlotRegisterAddress, RunningSlotRegisterCount);
        serialPort.Write(request, 0, request.Length);
        serialPort.BaseStream.Flush();

        var response = ReadExact(serialPort, RunningSlotResponseLength, timeout);
        return ParseRunningSlotResponse(response);
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

    private static byte[] BuildReadHoldingRegistersRequest(ushort startAddress, ushort registerCount)
    {
        Span<byte> frame = stackalloc byte[8];
        frame[0] = DeviceAddress;
        frame[1] = ReadHoldingRegisters;
        BinaryPrimitives.WriteUInt16BigEndian(frame[2..4], startAddress);
        BinaryPrimitives.WriteUInt16BigEndian(frame[4..6], registerCount);

        var crc = ComputeModbusCrc(frame[..6]);
        frame[6] = (byte)(crc & 0xFF);
        frame[7] = (byte)((crc >> 8) & 0xFF);
        return frame.ToArray();
    }

    private static FirmwareSlot ParseRunningSlotResponse(ReadOnlySpan<byte> response)
    {
        if (response.Length != RunningSlotResponseLength)
        {
            throw new InvalidOperationException($"读取槽位回包长度错误：{response.Length} 字节。");
        }

        if (response[0] != DeviceAddress)
        {
            throw new InvalidOperationException($"读取槽位回包站号错误：0x{response[0]:X2}。");
        }

        if (response[1] != ReadHoldingRegisters)
        {
            throw new InvalidOperationException($"读取槽位回包功能码错误：0x{response[1]:X2}。");
        }

        if (response[2] != 0x02)
        {
            throw new InvalidOperationException($"读取槽位回包数据长度错误：0x{response[2]:X2}。");
        }

        var expectedCrc = BinaryPrimitives.ReadUInt16LittleEndian(response[^2..]);
        var actualCrc = ComputeModbusCrc(response[..^2]);
        if (actualCrc != expectedCrc)
        {
            throw new InvalidOperationException("读取槽位回包 CRC 校验失败。");
        }

        var registerValue = BinaryPrimitives.ReadUInt16BigEndian(response[3..5]);
        return registerValue switch
        {
            1 => FirmwareSlot.A,
            2 => FirmwareSlot.B,
            _ => FirmwareSlot.Unknown
        };
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

    private static ushort ComputeModbusCrc(ReadOnlySpan<byte> data)
    {
        ushort crc = 0xFFFF;

        foreach (var value in data)
        {
            crc ^= value;
            for (var bit = 0; bit < 8; bit++)
            {
                crc = (crc & 1) != 0
                    ? (ushort)((crc >> 1) ^ 0xA001)
                    : (ushort)(crc >> 1);
            }
        }

        return crc;
    }

    private static byte[] ReadExact(SerialPort serialPort, int length, TimeSpan timeout)
    {
        var buffer = new byte[length];
        var offset = 0;
        var deadline = DateTime.UtcNow + timeout;

        while (offset < length)
        {
            var remaining = deadline - DateTime.UtcNow;
            if (remaining <= TimeSpan.Zero)
            {
                throw new TimeoutException($"读取槽位寄存器超时，仅收到 {offset}/{length} 字节。");
            }

            try
            {
                var read = serialPort.Read(buffer, offset, length - offset);
                if (read > 0)
                {
                    offset += read;
                }
            }
            catch (TimeoutException)
            {
            }
        }

        return buffer;
    }
}
