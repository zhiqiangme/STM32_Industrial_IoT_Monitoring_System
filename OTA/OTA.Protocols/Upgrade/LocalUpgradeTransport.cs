using System.IO.Ports;
using System.Text;
using OTA.Models;

namespace OTA.Protocols;

/// <summary>
/// 本地升级的串口传输实现。
/// 负责 App 阶段命令发送、Bootloader 切换等待以及 YMODEM 传输过程。
/// </summary>
public static class LocalUpgradeTransport
{
    public static void Run(
        LocalUpgradeOptions options,
        byte[] image,
        uint crc32,
        string sha256Hex,
        byte[] unlockCommand,
        byte[] enterBootloaderCommand,
        TimeSpan appCommandGap,
        TimeSpan bootloaderSwitchDelay,
        TimeSpan handshakeWindow,
        Action<string> log)
    {
        SerialOperationGate.Run(() =>
        {
            using var serialPort = SerialPortHelper.Open(options.PortName, options.BaudRate, Encoding.ASCII);
            Thread.Sleep(200);
            serialPort.DiscardInBuffer();

            log($"打开串口 {options.PortName}，波特率 {options.BaudRate}。");
            SendAppStageCommand(serialPort, unlockCommand, "解锁", TimeSpan.FromSeconds(options.TimeoutSeconds), log);
            Thread.Sleep(appCommandGap);
            SendAppStageCommand(serialPort, enterBootloaderCommand, "进入 Bootloader", TimeSpan.FromSeconds(options.TimeoutSeconds), log);
            log($"等待 {bootloaderSwitchDelay.TotalMilliseconds:0}ms 让设备从 App 切到 Bootloader。");
            Thread.Sleep(bootloaderSwitchDelay);
            serialPort.DiscardInBuffer();

            YModemProtocol.WaitReceiverReady(serialPort, TimeSpan.FromSeconds(options.TimeoutSeconds), handshakeWindow, log);
            YModemProtocol.SendFile(serialPort, options, image, crc32, sha256Hex, log);
            log("升级传输完成，设备应完成校验并自动复位回 App。");
        });
    }

    private static void SendFrame(SerialPort serialPort, byte[] frame, string logMessage, Action<string> log)
    {
        log(logMessage);
        serialPort.Write(frame, 0, frame.Length);
        serialPort.BaseStream.Flush();
    }

    private static void SendAppStageCommand(SerialPort serialPort, byte[] frame, string label, TimeSpan timeout, Action<string> log)
    {
        SendFrame(serialPort, frame, $"TX {label}: {Convert.ToHexString(frame)}", log);

        try
        {
            var response = ReadExact(serialPort, frame.Length, timeout);
            log($"RX {label}: {Convert.ToHexString(response)}");

            if (!response.AsSpan().SequenceEqual(frame))
            {
                log($"警告: {label} 回包与请求帧不一致。");
            }
        }
        catch (TimeoutException)
        {
            log($"警告: {label} 未在 {timeout.TotalSeconds:0.###} 秒内收到回包，继续尝试后续流程。");
        }
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
                throw new TimeoutException($"等待串口回包超时，仅收到 {offset}/{length} 字节。");
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
        }

        return buffer;
    }
}
