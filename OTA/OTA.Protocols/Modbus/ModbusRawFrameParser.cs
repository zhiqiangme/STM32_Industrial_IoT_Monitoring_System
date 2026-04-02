namespace OTA.Protocols;

/// <summary>
/// 解析用户输入的 Modbus RTU 原始帧文本。
/// </summary>
public static class ModbusRawFrameParser
{
    public static bool TryParse(
        string input,
        out ModbusRawFrameData frameData,
        out bool crcMatchedOriginal,
        out string errorMessage)
    {
        frameData = default;
        crcMatchedOriginal = false;

        var cleaned = new string(input
            .Where(static c => !char.IsWhiteSpace(c) && c != '-' && c != ',')
            .ToArray());

        if (cleaned.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
        {
            cleaned = cleaned[2..];
        }

        if (cleaned.Length == 0)
        {
            errorMessage = "原始帧为空。";
            return false;
        }

        if (cleaned.Length > 16)
        {
            cleaned = cleaned[..16];
        }

        if (cleaned.Length < 12)
        {
            errorMessage = "原始帧至少需要前 12 位有效十六进制字符。";
            return false;
        }

        var payloadHex = cleaned[..12];
        if (payloadHex.Any(static c => !Uri.IsHexDigit(c)))
        {
            errorMessage = "原始帧前 12 位包含非十六进制字符。";
            return false;
        }

        var bytes = Enumerable.Range(0, payloadHex.Length / 2)
            .Select(i => Convert.ToByte(payloadHex.Substring(i * 2, 2), 16))
            .ToArray();

        frameData = new ModbusRawFrameData(
            bytes[0],
            bytes[1],
            (ushort)((bytes[2] << 8) | bytes[3]),
            (ushort)((bytes[4] << 8) | bytes[5]));

        var trailingCrc = cleaned.Length >= 16 ? cleaned.Substring(12, 4) : string.Empty;
        if (trailingCrc.Length == 4 && trailingCrc.All(static c => Uri.IsHexDigit(c)))
        {
            var expectedCrc = ModbusCrc16.Compute(bytes);
            var inputCrc = (ushort)(
                Convert.ToByte(trailingCrc.Substring(0, 2), 16) |
                (Convert.ToByte(trailingCrc.Substring(2, 2), 16) << 8));
            crcMatchedOriginal = expectedCrc == inputCrc;
        }

        errorMessage = string.Empty;
        return true;
    }
}
