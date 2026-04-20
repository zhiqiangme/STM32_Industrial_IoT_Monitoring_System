using OTA.Models;

namespace OTA.Core;

/// <summary>
/// 把底层异常与原始错误文案归一化为统一错误码。
/// </summary>
internal static class OtaErrorMapper
{
    public static OtaError Create(OtaErrorCode code, string message)
    {
        return new OtaError(code, message);
    }

    public static OtaError MapPortOpenFailure(string message)
    {
        if (message.Contains("已被其它程序占用", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.PortBusy, message);
        }

        if (message.Contains("已连接且串口号正确", StringComparison.Ordinal) ||
            message.Contains("设备响应超时", StringComparison.Ordinal) ||
            message.Contains("串口已断开", StringComparison.Ordinal) ||
            message.Contains("串口连接已断开", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.PortDisconnected, message);
        }

        if (message.Contains("无效", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.PortOpenFailed, message);
        }

        return Create(OtaErrorCode.PortOpenFailed, message);
    }

    public static OtaError MapImageInspectionException(Exception ex)
    {
        var message = ex.Message;

        if (message.Contains("长度不足 8 字节", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.ImageTooShort, $"STM32 程序槽位识别失败：{message}");
        }

        if (message.Contains("读取 BIN 文件头失败", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.ImageHeaderReadFailed, $"STM32 程序槽位识别失败：{message}");
        }

        if (message.Contains("文件名指向槽位", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.ImageSlotConflict, $"STM32 程序槽位识别失败：{message}");
        }

        if (message.Contains("无法识别镜像槽位", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.ImageSlotUnknown, $"STM32 程序槽位识别失败：{message}");
        }

        return Create(OtaErrorCode.InternalError, $"STM32 程序槽位识别失败：{message}");
    }

    public static OtaError MapRunningSlotFailure(string message)
    {
        if (message.Contains("超时", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.RunningSlotReadTimeout, message);
        }

        if (message.Contains("回包长度错误", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.RunningSlotInvalidResponseLength, message);
        }

        if (message.Contains("回包站号错误", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.RunningSlotInvalidDeviceAddress, message);
        }

        if (message.Contains("回包功能码错误", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.RunningSlotInvalidFunctionCode, message);
        }

        if (message.Contains("回包数据长度错误", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.RunningSlotInvalidDataLength, message);
        }

        if (message.Contains("CRC 校验失败", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.RunningSlotCrcMismatch, message);
        }

        if (message.Contains("串口已断开", StringComparison.Ordinal) ||
            message.Contains("串口连接已断开", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.PortDisconnected, message);
        }

        if (message.Contains("无法打开串口", StringComparison.Ordinal))
        {
            return MapPortOpenFailure(message);
        }

        return Create(OtaErrorCode.RunningSlotReadFailed, message);
    }

    public static OtaError MapUpgradeExecutionException(Exception ex)
    {
        var message = ex.Message;

        if (message.Contains("等待 Bootloader 握手超时", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.UpgradeHandshakeTimeout, message);
        }

        if (message.Contains("Bootloader 中止了 YMODEM 会话", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.UpgradeSessionAborted, message);
        }

        if (message.Contains("请求重发", StringComparison.Ordinal) ||
            message.Contains("等待 ACK", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.UpgradeAckFailed, message);
        }

        if (message.Contains("未确认 EOT", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.UpgradeEotNotConfirmed, message);
        }

        if (message.Contains("无法打开串口", StringComparison.Ordinal))
        {
            return MapPortOpenFailure(message);
        }

        if (message.Contains("串口已断开", StringComparison.Ordinal) ||
            message.Contains("串口连接已断开", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.PortDisconnected, message);
        }

        if (ex is TimeoutException)
        {
            return Create(OtaErrorCode.UpgradeTransmissionFailed, message);
        }

        if (ex is InvalidOperationException or IOException or UnauthorizedAccessException)
        {
            return Create(OtaErrorCode.UpgradeTransmissionFailed, message);
        }

        return Create(OtaErrorCode.InternalError, message);
    }

    public static OtaError MapRawFrameImportFailure(string message)
    {
        if (message.Contains("原始帧为空", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.EmptyRawFrame, message);
        }

        if (message.Contains("至少需要前 12 位", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.RawFrameTooShort, message);
        }

        if (message.Contains("非十六进制字符", StringComparison.Ordinal))
        {
            return Create(OtaErrorCode.RawFrameContainsNonHex, message);
        }

        return Create(OtaErrorCode.RawFrameInvalid, message);
    }
}
