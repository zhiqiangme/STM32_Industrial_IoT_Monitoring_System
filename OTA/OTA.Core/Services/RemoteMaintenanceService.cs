using System.Globalization;
using OTA.Protocols;

namespace OTA.Core;

/// <summary>
/// 远程维护页的业务编排。
/// 负责输入解析、Modbus 原始帧生成以及导入纠错。
/// </summary>
public sealed class RemoteMaintenanceService
{
    public bool TryGenerateFrame(
        string slaveAddressText,
        NumberBase slaveAddressBase,
        string functionCodeText,
        NumberBase functionCodeBase,
        string registerAddressText,
        NumberBase registerAddressBase,
        string dataText,
        NumberBase dataBase,
        out RemoteMaintenanceFrameResult result,
        out string errorMessage)
    {
        result = default!;

        if (!TryParseByte(slaveAddressText, slaveAddressBase, out var slaveAddress, out var slaveError))
        {
            errorMessage = $"从站地址错误：{slaveError}";
            return false;
        }

        if (!TryParseByte(functionCodeText, functionCodeBase, out var functionCode, out var functionError))
        {
            errorMessage = $"功能码错误：{functionError}";
            return false;
        }

        if (!TryParseUInt16(registerAddressText, registerAddressBase, out var registerAddress, out var registerError))
        {
            errorMessage = $"寄存器地址错误：{registerError}";
            return false;
        }

        if (!TryParseUInt16(dataText, dataBase, out var dataValue, out var dataError))
        {
            errorMessage = $"数据错误：{dataError}";
            return false;
        }

        result = BuildResult(new ModbusRawFrameData(slaveAddress, functionCode, registerAddress, dataValue));
        errorMessage = string.Empty;
        return true;
    }

    public bool TryImportFrame(
        string frameText,
        out RemoteMaintenanceImportResult result,
        out string errorMessage)
    {
        result = default!;

        if (!ModbusRawFrameParser.TryParse(frameText, out var frameData, out var crcMatchedOriginal, out errorMessage))
        {
            return false;
        }

        result = new RemoteMaintenanceImportResult(BuildResult(frameData), crcMatchedOriginal);
        errorMessage = string.Empty;
        return true;
    }

    public bool TryParseByte(string input, NumberBase numberBase, out byte value, out string errorMessage)
    {
        if (!TryParseFieldValue(input, numberBase, byte.MaxValue, out var parsedValue, out errorMessage))
        {
            value = 0;
            return false;
        }

        value = (byte)parsedValue;
        errorMessage = string.Empty;
        return true;
    }

    public bool TryParseUInt16(string input, NumberBase numberBase, out ushort value, out string errorMessage)
    {
        if (!TryParseFieldValue(input, numberBase, ushort.MaxValue, out var parsedValue, out errorMessage))
        {
            value = 0;
            return false;
        }

        value = (ushort)parsedValue;
        errorMessage = string.Empty;
        return true;
    }

    public string FormatValue(uint value, NumberBase numberBase, int hexDigits)
    {
        return numberBase == NumberBase.Hex
            ? value.ToString($"X{hexDigits}", CultureInfo.InvariantCulture)
            : value.ToString(CultureInfo.InvariantCulture);
    }

    public string SanitizeFrameText(string? input)
    {
        var originalText = input ?? string.Empty;
        var sanitizedText = new string(originalText.Where(static c => !char.IsWhiteSpace(c)).ToArray());
        return sanitizedText.Length > 16
            ? sanitizedText[..16]
            : sanitizedText;
    }

    private static RemoteMaintenanceFrameResult BuildResult(ModbusRawFrameData frameData)
    {
        var frame = ModbusRawFrameBuilder.BuildFrame(frameData);
        var crc = ModbusCrc16.Compute(frame.AsSpan(0, 6));
        return new RemoteMaintenanceFrameResult(
            frameData.SlaveAddress,
            frameData.FunctionCode,
            frameData.RegisterAddress,
            frameData.DataValue,
            crc,
            string.Join(" ", frame.Select(static b => b.ToString("X2", CultureInfo.InvariantCulture))),
            Convert.ToHexString(frame));
    }

    private static bool TryParseFieldValue(string? input, NumberBase numberBase, uint maxValue, out uint value, out string errorMessage)
    {
        value = 0;

        if (string.IsNullOrWhiteSpace(input))
        {
            errorMessage = "请输入有效数值。";
            return false;
        }

        var cleaned = input.Trim().Replace(" ", string.Empty, StringComparison.Ordinal);
        if (cleaned.Length == 0)
        {
            errorMessage = "请输入有效数值。";
            return false;
        }

        if (numberBase == NumberBase.Hex)
        {
            if (cleaned.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            {
                cleaned = cleaned[2..];
            }

            if (cleaned.Length == 0)
            {
                errorMessage = "请输入十六进制数值。";
                return false;
            }

            if (cleaned.Any(static c => !Uri.IsHexDigit(c)))
            {
                errorMessage = "只能输入十六进制字符 0-9、A-F。";
                return false;
            }

            if (!uint.TryParse(cleaned, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out value))
            {
                errorMessage = "十六进制数值无效或超出范围。";
                return false;
            }
        }
        else
        {
            if (cleaned.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            {
                cleaned = cleaned[2..];

                if (cleaned.Length == 0)
                {
                    errorMessage = "请输入十六进制数值。";
                    return false;
                }

                if (cleaned.Any(static c => !Uri.IsHexDigit(c)))
                {
                    errorMessage = "只能输入十六进制字符 0-9、A-F。";
                    return false;
                }

                if (!uint.TryParse(cleaned, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out value))
                {
                    errorMessage = "十六进制数值无效或超出范围。";
                    return false;
                }
            }
            else
            {
                if (cleaned.StartsWith("DEC", StringComparison.OrdinalIgnoreCase))
                {
                    cleaned = cleaned[3..];
                }
                else if (cleaned.StartsWith("0d", StringComparison.OrdinalIgnoreCase))
                {
                    cleaned = cleaned[2..];
                }

                if (cleaned.Any(static c => !char.IsAsciiDigit(c)))
                {
                    errorMessage = "十进制模式下只能输入 0-9。";
                    return false;
                }

                if (!uint.TryParse(cleaned, NumberStyles.None, CultureInfo.InvariantCulture, out value))
                {
                    errorMessage = "十进制数值无效或超出范围。";
                    return false;
                }
            }
        }

        if (value > maxValue)
        {
            errorMessage = $"数值超出范围，最大允许 {maxValue}。";
            return false;
        }

        errorMessage = string.Empty;
        return true;
    }
}

public enum NumberBase
{
    Hex,
    Dec
}

public sealed record RemoteMaintenanceFrameResult(
    byte SlaveAddress,
    byte FunctionCode,
    ushort RegisterAddress,
    ushort DataValue,
    ushort Crc,
    string RawFrame,
    string ClipboardFrame);

public sealed record RemoteMaintenanceImportResult(
    RemoteMaintenanceFrameResult Frame,
    bool CrcMatchedOriginal);
