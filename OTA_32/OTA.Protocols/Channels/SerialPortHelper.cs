using System.IO;
using System.IO.Ports;
using System.Text;

namespace OTA.Protocols;

/// <summary>
/// 串口打开辅助类。
/// 这里统一封装串口名规范化、超时参数、异常翻译等细节，
/// 让上层界面层和升级层只关心“能否打开”和“失败时给用户什么提示”。
/// </summary>
public static class SerialPortHelper
{
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

    private static bool IsExpectedOpenException(Exception ex)
    {
        return ex is UnauthorizedAccessException or IOException or ArgumentException or TimeoutException;
    }

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
