using System.IO;
using System.IO.Ports;
using System.Text;

namespace Project;

internal static class SerialPortHelper
{
    public static SerialPort Open(string portName, int baudRate, Encoding? encoding = null)
    {
        var requestedPortName = NormalizeRequestedPortName(portName);
        try
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
        catch (Exception ex) when (ex is UnauthorizedAccessException or IOException or ArgumentException)
        {
            throw CreateOpenPortException(requestedPortName, ex);
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
            ArgumentException => new InvalidOperationException(
                $"串口号 {requestedPortName} 无效，请检查输入。",
                error),
            _ => new InvalidOperationException(
                $"无法打开串口 {requestedPortName}。",
                error)
        };
    }
}
