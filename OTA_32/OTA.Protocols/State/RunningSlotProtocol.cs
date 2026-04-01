using System.Buffers.Binary;
using System.IO;
using System.IO.Ports;
using OTA.Models;

namespace OTA.Protocols;

/// <summary>
/// 当前运行槽位的串口协议实现。
/// 负责构造 Modbus 读寄存器请求、解析回包，并统一处理串口异常。
/// </summary>
public static class RunningSlotProtocol
{
    private const byte DeviceAddress = 0x0A;
    private const byte ReadHoldingRegisters = 0x03;
    private const ushort RunningSlotRegisterAddress = 0x005A;
    private const ushort RunningSlotRegisterCount = 0x0001;
    private const int RunningSlotResponseLength = 7;

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
            catch (Exception ex) when (ex is UnauthorizedAccessException or IOException or InvalidOperationException)
            {
                throw new InvalidOperationException(BuildSerialDisconnectMessage(ex), ex);
            }
        }

        return buffer;
    }

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
