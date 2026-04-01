using System.Buffers.Binary;
using System.IO;
using System.IO.Ports;

namespace Project;

/// <summary>
/// 固件槽位枚举。
/// 当前 STM32 采用 A/B 双镜像升级，因此界面和升级逻辑都围绕这两个槽位展开。
/// </summary>
internal enum FirmwareSlot
{
    Unknown = 0,
    A = 1,
    B = 2
}

/// <summary>
/// 固件镜像识别结果。
/// 同时保存文件名推断结果、向量表推断结果以及关键地址，便于界面做提示和校验。
/// </summary>
internal readonly record struct FirmwareImageInfo(
    string ImagePath,
    FirmwareSlot DetectedSlot,
    FirmwareSlot FileNameSlot,
    FirmwareSlot VectorSlot,
    uint InitialStackPointer,
    uint ResetHandler);

/// <summary>
/// 槽位相关的显示与换槽辅助方法。
/// </summary>
internal static class FirmwareSlotExtensions
{
    /// <summary>
    /// 把内部枚举转换成界面显示文本。
    /// </summary>
    public static string ToDisplayText(this FirmwareSlot slot)
    {
        return slot switch
        {
            FirmwareSlot.A => "A",
            FirmwareSlot.B => "B",
            _ => "未知"
        };
    }

    /// <summary>
    /// 获取当前槽位的另一槽。
    /// 例如当前运行 A，则建议升级 B。
    /// </summary>
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

/// <summary>
/// A/B 升级辅助类。
/// 负责两类工作：
/// 1. 通过串口读取设备当前运行槽位；
/// 2. 通过 BIN 文件名和向量表判断镜像属于哪个槽位。
/// </summary>
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

    /// <summary>
    /// 尝试读取设备当前运行槽位。
    /// 该方法用于界面后台轮询，因此失败时不抛给上层，而是返回 false 和中文错误信息。
    /// </summary>
    public static bool TryReadRunningSlot(string portName, int baudRate, TimeSpan timeout, out FirmwareSlot slot, out string? errorMessage)
    {
        var result = SerialOperationGate.Run(() =>
        {
            var slot = FirmwareSlot.Unknown;
            string? errorMessage = null;

            if (!SerialPortHelper.TryOpen(portName, baudRate, out var serialPort, out errorMessage))
            {
                return (Success: false, Slot: slot, ErrorMessage: errorMessage);
            }

            if (serialPort is null)
            {
                errorMessage = "无法打开串口。";
                return (Success: false, Slot: slot, ErrorMessage: errorMessage);
            }

            try
            {
                using (serialPort)
                {
                    Thread.Sleep(200);
                    serialPort.DiscardInBuffer();
                    serialPort.DiscardOutBuffer();

                    var request = BuildReadHoldingRegistersRequest(RunningSlotRegisterAddress, RunningSlotRegisterCount);
                    serialPort.Write(request, 0, request.Length);
                    serialPort.BaseStream.Flush();

                    var response = ReadExact(serialPort, RunningSlotResponseLength, timeout);
                    slot = ParseRunningSlotResponse(response);
                    return (Success: true, Slot: slot, ErrorMessage: errorMessage);
                }
            }
            catch (Exception ex) when (ex is TimeoutException or IOException or InvalidOperationException or UnauthorizedAccessException)
            {
                errorMessage = BuildSerialDisconnectMessage(ex);
                slot = FirmwareSlot.Unknown;
                return (Success: false, Slot: slot, ErrorMessage: errorMessage);
            }
        });

        slot = result.Slot;
        errorMessage = result.ErrorMessage;
        return result.Success;
    }

    /// <summary>
    /// 读取设备当前运行槽位。
    /// 与 TryReadRunningSlot 的区别是：这里把异常继续向上抛，适合更严格的调用场景。
    /// </summary>
    public static FirmwareSlot ReadRunningSlot(string portName, int baudRate, TimeSpan timeout)
    {
        return SerialOperationGate.Run(() =>
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
        });
    }

    /// <summary>
    /// 检查 BIN 镜像，识别该文件应写入 A 槽还是 B 槽。
    /// 识别优先级为：
    /// 1. 复位向量落在哪个地址范围；
    /// 2. 文件名是否包含 App_A / App_B。
    /// 两者都能识别时，还会做一次交叉校验，防止文件名和内容不一致。
    /// </summary>
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

    /// <summary>
    /// 根据当前运行槽位给出推荐升级文件名。
    /// 当前运行 A，就推荐刷 B；当前运行 B，就推荐刷 A。
    /// </summary>
    public static string GetRecommendedFileName(FirmwareSlot runningSlot)
    {
        return runningSlot switch
        {
            FirmwareSlot.A => "App_B.bin",
            FirmwareSlot.B => "App_A.bin",
            _ => "App_A.bin"
        };
    }

    /// <summary>
    /// 生成界面下方镜像提示文本。
    /// 把识别来源和复位向量一起展示，便于现场排查镜像打包错误。
    /// </summary>
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

    /// <summary>
    /// 构造 Modbus 03 功能码读寄存器请求帧。
    /// 当前用于读取“当前运行槽位”寄存器。
    /// </summary>
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

    /// <summary>
    /// 解析设备返回的槽位响应帧，并执行长度、站号、功能码、CRC 等校验。
    /// </summary>
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

    /// <summary>
    /// 从文件名推断槽位，例如 App_A.bin / App_B.bin。
    /// </summary>
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

    /// <summary>
    /// 根据复位向量地址判断镜像属于哪个槽位。
    /// 这是比文件名更可靠的判定来源。
    /// </summary>
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

    /// <summary>
    /// 计算 Modbus RTU CRC16。
    /// </summary>
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

    /// <summary>
    /// 从串口精确读取指定长度的数据。
    /// 中间允许多次超时重试，但总耗时受 timeout 限制。
    /// 如果设备在读取过程中被拔掉，会统一转成可读的断开提示。
    /// </summary>
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
            catch (Exception ex) when (ex is UnauthorizedAccessException or IOException or InvalidOperationException)
            {
                throw new InvalidOperationException(BuildSerialDisconnectMessage(ex), ex);
            }
        }

        return buffer;
    }

    /// <summary>
    /// 把“串口断开/系统回收”等底层异常翻译成统一中文提示。
    /// </summary>
    private static string BuildSerialDisconnectMessage(Exception ex)
    {
        return ex switch
        {
            UnauthorizedAccessException => "串口已断开或被系统回收，无法继续读取当前槽位。",
            IOException => "串口连接已断开，无法继续读取当前槽位。",
            InvalidOperationException => ex.Message,
            _ => ex.Message
        };
    }
}
