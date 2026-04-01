// SerialPortHelper.cs
// 放串口打开与异常翻译辅助逻辑，统一处理串口名规范化和打开失败提示。

using System.IO;
using System.IO.Ports;
using System.Text;

namespace Project;

/// <summary>
/// 串口打开辅助类。
/// 这里统一封装串口名规范化、超时参数、异常翻译等细节，
/// 让上层界面层和升级层只关心“能否打开”和“失败时给用户什么提示”。
/// </summary>
internal static class SerialPortHelper
{
    /// <summary>
    /// 打开串口。
    /// 如果底层抛出的是常见的串口访问异常，这里会统一转换为更适合界面提示的异常信息。
    /// </summary>
    public static SerialPort Open(string portName, int baudRate, Encoding? encoding = null)
    {
        var requestedPortName = NormalizeRequestedPortName(portName);
        try
        {
            return OpenCore(requestedPortName, baudRate, encoding);
        }
        catch (Exception ex) when (IsExpectedOpenException(ex))
        {
            throw CreateOpenPortException(requestedPortName, ex);
        }
    }

    /// <summary>
    /// 尝试打开串口，不抛出常见打开异常。
    /// 适合“后台探测”“自动识别槽位”这类失败属于正常分支的场景。
    /// </summary>
    public static bool TryOpen(string portName, int baudRate, out SerialPort? serialPort, out string? errorMessage, Encoding? encoding = null)
    {
        serialPort = null;
        errorMessage = null;

        string requestedPortName;
        try
        {
            requestedPortName = NormalizeRequestedPortName(portName);
        }
        catch (ArgumentException ex)
        {
            errorMessage = CreateOpenPortException(portName, ex).Message;
            return false;
        }

        try
        {
            serialPort = OpenCore(requestedPortName, baudRate, encoding);
            return true;
        }
        catch (Exception ex) when (IsExpectedOpenException(ex))
        {
            serialPort?.Dispose();
            serialPort = null;
            errorMessage = CreateOpenPortException(requestedPortName, ex).Message;
            return false;
        }
    }

    /// <summary>
    /// 规范化串口号表示。
    /// 支持 COM3 和 \\.\COM3 两种输入，最终统一转成 COM3 形式，便于后续比较和显示。
    /// </summary>
    private static string NormalizeRequestedPortName(string portName)
    {
        if (string.IsNullOrWhiteSpace(portName))
        {
            throw new ArgumentException("串口号不能为空。", nameof(portName));
        }

        var normalized = portName.Trim().ToUpperInvariant();
        if (normalized.StartsWith(@"\\.\COM", StringComparison.Ordinal))
        {
            normalized = normalized[4..];
        }

        if (!normalized.StartsWith("COM", StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException($"串口号 {normalized} 无效。", nameof(portName));
        }

        return normalized;
    }

    /// <summary>
    /// 真正执行串口打开的底层方法。
    /// 统一在这里收口基础串口配置，避免不同调用点设置不一致。
    /// </summary>
    private static SerialPort OpenCore(string requestedPortName, int baudRate, Encoding? encoding)
    {
        var serialPort = new SerialPort(requestedPortName, baudRate, Parity.None, 8, StopBits.One)
        {
            Handshake = Handshake.None,
            ReadTimeout = 100,
            WriteTimeout = 1000
        };

        if (encoding is not null)
        {
            serialPort.Encoding = encoding;
        }

        serialPort.Open();
        return serialPort;
    }

    /// <summary>
    /// 判断是否属于“串口打开阶段的预期异常”。
    /// 这些异常会被转成业务友好的提示，而不是直接把底层异常暴露给界面。
    /// </summary>
    private static bool IsExpectedOpenException(Exception ex)
    {
        return ex is UnauthorizedAccessException or IOException or ArgumentException or TimeoutException;
    }

    /// <summary>
    /// 把底层串口异常翻译成更明确的中文异常。
    /// 这样界面层只需要直接显示 Message，就能让用户知道是“被占用”“不存在”还是“超时”。
    /// </summary>
    private static InvalidOperationException CreateOpenPortException(string requestedPortName, Exception? error)
    {
        return error switch
        {
            UnauthorizedAccessException => new InvalidOperationException(
                $"无法打开串口 {requestedPortName}。该串口可能已被其它程序占用，或当前进程没有访问权限。",
                error),
            IOException => new InvalidOperationException(
                $"无法打开串口 {requestedPortName}。请确认设备已连接且串口号正确。",
                error),
            TimeoutException => new InvalidOperationException(
                $"无法打开串口 {requestedPortName}。设备响应超时，请确认设备已连接且串口号正确。",
                error),
            ArgumentException => new InvalidOperationException(
                $"串口号 {requestedPortName} 无效，请检查输入。",
                error),
            _ => new InvalidOperationException(
                $"无法打开串口 {requestedPortName}。",
                error)
        };
    }
}
