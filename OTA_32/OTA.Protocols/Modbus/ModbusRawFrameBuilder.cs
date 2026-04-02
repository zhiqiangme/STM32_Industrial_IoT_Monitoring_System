namespace OTA.Protocols;

/// <summary>
/// 构造标准 8 字节 Modbus RTU 原始帧。
/// </summary>
public static class ModbusRawFrameBuilder
{
    public static byte[] BuildFrame(ModbusRawFrameData frameData)
    {
        var frame = new byte[8];
        frame[0] = frameData.SlaveAddress;
        frame[1] = frameData.FunctionCode;
        frame[2] = (byte)(frameData.RegisterAddress >> 8);
        frame[3] = (byte)(frameData.RegisterAddress & 0xFF);
        frame[4] = (byte)(frameData.DataValue >> 8);
        frame[5] = (byte)(frameData.DataValue & 0xFF);

        var crc = ModbusCrc16.Compute(frame.AsSpan(0, 6));
        frame[6] = (byte)(crc & 0xFF);
        frame[7] = (byte)(crc >> 8);
        return frame;
    }
}

public readonly record struct ModbusRawFrameData(
    byte SlaveAddress,
    byte FunctionCode,
    ushort RegisterAddress,
    ushort DataValue);
