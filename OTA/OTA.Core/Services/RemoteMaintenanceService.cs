using System.Globalization;
using OTA.Models;
using OTA.Protocols;

namespace OTA.Core;

/// <summary>
/// 远程维护页的业务编排。
/// 负责输入解析、Modbus 原始帧生成以及导入纠错。
/// </summary>
public sealed class RemoteMaintenanceService
{
    public OperationResult<RemoteMaintenanceFrameResult> GenerateFrame(
        string slaveAddressText,
        NumberBase slaveAddressBase,
        string functionCodeText,
        NumberBase functionCodeBase,
        string registerAddressText,
        NumberBase registerAddressBase,
        string dataText,
        NumberBase dataBase)
    {
        var slaveAddressResult = ParseByte(slaveAddressText, slaveAddressBase, OtaErrorCode.InvalidSlaveAddress, "从站地址错误");
        if (!slaveAddressResult.IsSuccess)
        {
            return OperationResult<RemoteMaintenanceFrameResult>.Failure(slaveAddressResult.Error!);
        }

        var functionCodeResult = ParseByte(functionCodeText, functionCodeBase, OtaErrorCode.InvalidFunctionCodeInput, "功能码错误");
        if (!functionCodeResult.IsSuccess)
        {
            return OperationResult<RemoteMaintenanceFrameResult>.Failure(functionCodeResult.Error!);
        }

        var registerAddressResult = ParseUInt16(registerAddressText, registerAddressBase, OtaErrorCode.InvalidRegisterAddress, "寄存器地址错误");
        if (!registerAddressResult.IsSuccess)
        {
            return OperationResult<RemoteMaintenanceFrameResult>.Failure(registerAddressResult.Error!);
        }

        var dataValueResult = ParseUInt16(dataText, dataBase, OtaErrorCode.InvalidDataValue, "数据错误");
        if (!dataValueResult.IsSuccess)
        {
            return OperationResult<RemoteMaintenanceFrameResult>.Failure(dataValueResult.Error!);
        }

        return OperationResult<RemoteMaintenanceFrameResult>.Success(
            BuildResult(new ModbusRawFrameData(
                slaveAddressResult.Value,
                functionCodeResult.Value,
                registerAddressResult.Value,
                dataValueResult.Value)));
    }

    public OperationResult<RemoteMaintenanceImportResult> ImportFrame(string frameText)
    {
        if (!ModbusRawFrameParser.TryParse(frameText, out var frameData, out var crcMatchedOriginal, out var errorMessage))
        {
            return OperationResult<RemoteMaintenanceImportResult>.Failure(OtaErrorMapper.MapRawFrameImportFailure(errorMessage));
        }

        return OperationResult<RemoteMaintenanceImportResult>.Success(
            new RemoteMaintenanceImportResult(BuildResult(frameData), crcMatchedOriginal));
    }

    public OperationResult<byte> ParseByte(string input, NumberBase numberBase)
    {
        return ParseByte(input, numberBase, OtaErrorCode.InvalidDataValue, "数值错误");
    }

    public OperationResult<ushort> ParseUInt16(string input, NumberBase numberBase)
    {
        return ParseUInt16(input, numberBase, OtaErrorCode.InvalidDataValue, "数值错误");
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

    private static OperationResult<byte> ParseByte(string input, NumberBase numberBase, OtaErrorCode errorCode, string errorPrefix)
    {
        if (!TryParseFieldValue(input, numberBase, byte.MaxValue, out var parsedValue, out var errorMessage))
        {
            return OperationResult<byte>.Failure(errorCode, $"{errorPrefix}：{errorMessage}");
        }

        return OperationResult<byte>.Success((byte)parsedValue);
    }

    private static OperationResult<ushort> ParseUInt16(string input, NumberBase numberBase, OtaErrorCode errorCode, string errorPrefix)
    {
        if (!TryParseFieldValue(input, numberBase, ushort.MaxValue, out var parsedValue, out var errorMessage))
        {
            return OperationResult<ushort>.Failure(errorCode, $"{errorPrefix}：{errorMessage}");
        }

        return OperationResult<ushort>.Success((ushort)parsedValue);
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
