// LocalUpgradeService.cs
// 放本地 OTA 升级执行逻辑，负责解锁、切换 Bootloader 和 YMODEM 发送固件。

using System.IO;
using System.IO.Ports;
using System.Security.Cryptography;
using System.Text;

namespace Project;

/// <summary>
/// 本地 OTA 升级执行器。
/// 完整流程为：
/// 1. 打开串口并打印固件摘要；
/// 2. 发送解锁帧；
/// 3. 发送进入 Bootloader 帧；
/// 4. 等待 Bootloader 进入 YMODEM 握手；
/// 5. 发送头包、数据包、EOT 和结束空包。
/// </summary>
internal static class LocalUpgradeService
{
    // YMODEM/CPM 控制字节。
    private const byte Soh = 0x01;
    private const byte Stx = 0x02;
    private const byte Eot = 0x04;
    private const byte Ack = 0x06;
    private const byte Nak = 0x15;
    private const byte Ca = 0x18;
    private const byte Crc16Request = 0x43;
    private const int PacketSize = 128;
    private const int Packet1KSize = 1024;
    private const int MaxRetries = 10;
    private const byte CpmEof = 0x1A;
    private static readonly TimeSpan AppCommandGap = TimeSpan.FromMilliseconds(300);
    private static readonly TimeSpan BootloaderSwitchDelay = TimeSpan.FromMilliseconds(2500);
    private static readonly TimeSpan HandshakeWindow = TimeSpan.FromSeconds(20);

    private static readonly byte[] UnlockCommand = Convert.FromHexString("0A060030A55A73D5");
    private static readonly byte[] EnterBootloaderCommand = Convert.FromHexString("0A0600310005197D");

    /// <summary>
    /// 在后台线程启动升级流程，避免阻塞界面线程。
    /// </summary>
    public static Task RunAsync(LocalUpgradeOptions options, Action<string> log)
    {
        ArgumentNullException.ThrowIfNull(log);
        return Task.Run(() => Execute(options, log));
    }

    /// <summary>
    /// 执行升级主流程。
    /// 这里先打印固件信息，再串行执行解锁、切换 Bootloader 和 YMODEM 发送。
    /// </summary>
    private static void Execute(LocalUpgradeOptions options, Action<string> log)
    {
        var image = File.ReadAllBytes(options.ImagePath);
        var crc32 = ComputeCrc32(image);
        var sha256Hex = Convert.ToHexString(SHA256.HashData(image)).ToLowerInvariant();

        log($"固件: {options.ImagePath}");
        log($"固件大小: {image.Length} bytes (0x{image.Length:X8})");
        log($"固件 CRC32: 0x{crc32:X8}");
        log($"固件 SHA256: {sha256Hex}");

        SerialOperationGate.Run(() =>
        {
            using var serialPort = SerialPortHelper.Open(options.PortName, options.BaudRate, Encoding.ASCII);
            Thread.Sleep(200);
            serialPort.DiscardInBuffer();

            log($"打开串口 {options.PortName}，波特率 {options.BaudRate}。");
            SendAppStageCommand(serialPort, UnlockCommand, "解锁", TimeSpan.FromSeconds(options.TimeoutSeconds), log);
            Thread.Sleep(AppCommandGap);
            SendAppStageCommand(serialPort, EnterBootloaderCommand, "进入 Bootloader", TimeSpan.FromSeconds(options.TimeoutSeconds), log);
            log($"等待 {BootloaderSwitchDelay.TotalMilliseconds:0}ms 让设备从 App 切到 Bootloader。");
            Thread.Sleep(BootloaderSwitchDelay);
            serialPort.DiscardInBuffer();

            WaitReceiverReady(serialPort, TimeSpan.FromSeconds(options.TimeoutSeconds), HandshakeWindow, log);
            SendFile(serialPort, options, image, crc32, sha256Hex, log);
            log("升级传输完成，设备应完成校验并自动复位回 App。");
        });
    }

    /// <summary>
    /// 发送完整的 YMODEM 文件。
    /// 包括头包、全部数据包、EOT 和结尾空包。
    /// </summary>
    private static void SendFile(SerialPort serialPort, LocalUpgradeOptions options, byte[] image, uint crc32, string sha256Hex, Action<string> log)
    {
        var headerPacket = BuildInitialPacket(Path.GetFileName(options.ImagePath), image.Length, crc32, sha256Hex, options.TargetFirmwareVersion);
        SendPacketWithRetry(serialPort, headerPacket, TimeSpan.FromSeconds(options.TimeoutSeconds), log, expectCrcRequest: true);
        log("YMODEM 头包已确认。");

        var packetNo = 1;
        var sent = 0;
        while (sent < image.Length)
        {
            var packetSize = ChooseDataBlock(image.Length - sent);
            var chunk = image.AsSpan(sent, Math.Min(packetSize, image.Length - sent)).ToArray();
            var packetType = packetSize == Packet1KSize ? Stx : Soh;
            var packet = BuildPacket(packetNo, chunk, packetType);

            SendPacketWithRetry(serialPort, packet, TimeSpan.FromSeconds(options.TimeoutSeconds), log, expectCrcRequest: false);

            sent += chunk.Length;
            packetNo = (packetNo + 1) & 0xFF;
            log($"DATA: {sent}/{image.Length} bytes ({sent * 100 / image.Length}%)");
        }

        var eotAccepted = false;
        for (var retry = 0; retry < MaxRetries; retry++)
        {
            serialPort.Write(new[] { (char)Eot }, 0, 1);
            try
            {
                var value = ReadAcceptedControl(serialPort, TimeSpan.FromSeconds(options.TimeoutSeconds), new HashSet<int> { Ack, Nak, Ca });
                if (value == Ack)
                {
                    eotAccepted = true;
                    break;
                }

                if (value == Nak)
                {
                    continue;
                }

                var other = ReadByte(serialPort, TimeSpan.FromMilliseconds(500));
                if (other == Ca)
                {
                    throw new InvalidOperationException("Bootloader 中止了 YMODEM 会话。");
                }
            }
            catch (TimeoutException)
            {
                continue;
            }
        }

        if (!eotAccepted)
        {
            throw new InvalidOperationException("Bootloader 未确认 EOT。");
        }

        log("EOT 已确认。");
        var finalPacket = BuildFinalPacket();
        SendPacketWithRetry(serialPort, finalPacket, TimeSpan.FromSeconds(options.TimeoutSeconds), log, expectCrcRequest: false);
        log("最终空包已确认。");
    }

    /// <summary>
    /// 带重试发送单个 YMODEM 包。
    /// 只要对端返回 NAK 或超时，就允许重发；如果对端明确中止，则立即结束。
    /// </summary>
    private static void SendPacketWithRetry(SerialPort serialPort, byte[] packet, TimeSpan timeout, Action<string> log, bool expectCrcRequest)
    {
        Exception? lastError = null;
        for (var retry = 0; retry < MaxRetries; retry++)
        {
            try
            {
                serialPort.Write(packet, 0, packet.Length);
                serialPort.BaseStream.Flush();
                WaitAck(serialPort, timeout, expectCrcRequest);
                return;
            }
            catch (Exception ex) when (ex is TimeoutException or InvalidOperationException)
            {
                lastError = ex;
                if (ex.Message.Contains("中止", StringComparison.OrdinalIgnoreCase))
                {
                    throw;
                }
            }
        }

        throw new InvalidOperationException(lastError?.Message ?? "YMODEM 发包失败。");
    }

    /// <summary>
    /// 等待一个数据包被 Bootloader 确认。
    /// 头包之后有些 Bootloader 还会额外再发一个 'C'，因此 expectCrcRequest 为 true 时要兼容这种行为。
    /// </summary>
    private static void WaitAck(SerialPort serialPort, TimeSpan timeout, bool expectCrcRequest)
    {
        for (var retry = 0; retry < MaxRetries; retry++)
        {
            var value = ReadAcceptedControl(serialPort, timeout, new HashSet<int> { Ack, Nak, Ca });
            if (value == Ack)
            {
                if (expectCrcRequest)
                {
                    try
                    {
                        var follow = ReadAcceptedControl(serialPort, TimeSpan.FromMilliseconds(500), new HashSet<int> { Crc16Request, Ack, Nak, Ca });
                        if (follow == Crc16Request || follow == Ack)
                        {
                            return;
                        }

                        if (follow == Nak)
                        {
                            continue;
                        }

                        var other = ReadByte(serialPort, TimeSpan.FromMilliseconds(500));
                        if (other == Ca)
                        {
                            throw new InvalidOperationException("Bootloader 中止了 YMODEM 会话。");
                        }
                    }
                    catch (TimeoutException)
                    {
                        return;
                    }
                }

                return;
            }

            if (value == Nak)
            {
                throw new InvalidOperationException("Bootloader 请求重发。");
            }

            var next = ReadByte(serialPort, TimeSpan.FromMilliseconds(500));
            if (next == Ca)
            {
                throw new InvalidOperationException("Bootloader 中止了 YMODEM 会话。");
            }
        }

        throw new InvalidOperationException("等待 ACK 重试次数过多。");
    }

    /// <summary>
    /// 等待 Bootloader 进入 YMODEM 接收态。
    /// 标志是持续收到字符 'C'（0x43）。
    /// </summary>
    private static void WaitReceiverReady(SerialPort serialPort, TimeSpan readTimeout, TimeSpan totalTimeout, Action<string> log)
    {
        log("等待 Bootloader YMODEM 握手 ('C')...");
        var deadline = DateTime.UtcNow + totalTimeout;
        var ignoredCount = 0;

        while (true)
        {
            if (DateTime.UtcNow >= deadline)
            {
                throw new TimeoutException($"等待 Bootloader 握手超时，{totalTimeout.TotalSeconds:0} 秒内未收到字符 'C'。");
            }

            try
            {
                var remaining = deadline - DateTime.UtcNow;
                var currentTimeout = remaining < readTimeout ? remaining : readTimeout;
                if (currentTimeout <= TimeSpan.Zero)
                {
                    currentTimeout = TimeSpan.FromMilliseconds(200);
                }

                var value = ReadByte(serialPort, currentTimeout);
                if (value == Crc16Request)
                {
                    log("已收到 Bootloader 握手字符 'C'。");
                    return;
                }

                if (value == Ca)
                {
                    var other = ReadByte(serialPort, TimeSpan.FromMilliseconds(500));
                    if (other == Ca)
                    {
                        throw new InvalidOperationException("Bootloader 中止了 YMODEM 会话。");
                    }
                }

                ignoredCount++;
                if (ignoredCount <= 8)
                {
                    log($"握手阶段忽略字节: 0x{value:X2}");
                }
                else if (ignoredCount == 9)
                {
                    log("握手阶段仍有非 'C' 字节输入，后续忽略日志已省略。");
                }
            }
            catch (TimeoutException)
            {
                log("仍在等待 Bootloader 握手...");
            }
        }
    }

    /// <summary>
    /// 发送一帧原始应用层命令，并记录日志。
    /// </summary>
    private static void SendFrame(SerialPort serialPort, byte[] frame, string logMessage, Action<string> log)
    {
        log(logMessage);
        serialPort.Write(frame, 0, frame.Length);
        serialPort.BaseStream.Flush();
    }

    /// <summary>
    /// 发送 App 阶段命令（解锁 / 进入 Bootloader），并尝试读取等长回包。
    /// 没有回包时只记警告，不直接终止流程，兼容部分设备固件实现。
    /// </summary>
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

    /// <summary>
    /// 循环读取一个控制字节，直到碰到允许的集合中的值。
    /// 其他字节会被直接忽略。
    /// </summary>
    private static int ReadAcceptedControl(SerialPort serialPort, TimeSpan timeout, HashSet<int> accepted)
    {
        while (true)
        {
            var value = ReadByte(serialPort, timeout);
            if (accepted.Contains(value))
            {
                return value;
            }
        }
    }

    /// <summary>
    /// 在指定超时时间内读取一个字节。
    /// </summary>
    private static int ReadByte(SerialPort serialPort, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                return serialPort.ReadByte();
            }
            catch (TimeoutException)
            {
            }
        }

        throw new TimeoutException("等待 Bootloader 控制字节超时。");
    }

    /// <summary>
    /// 精确读取固定长度回包。
    /// 这是发送 App 阶段命令时使用的简单阻塞式读取。
    /// </summary>
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

    /// <summary>
    /// 构造 YMODEM 初始头包。
    /// 头包除了文件名和长度，还额外带上 CRC32、目标版本号和 SHA256，便于下位机做更严格校验。
    /// </summary>
    private static byte[] BuildInitialPacket(string fileName, int fileSize, uint crc32, string sha256Hex, uint targetFirmwareVersion)
    {
        var safeName = Path.GetFileName(fileName);
        var metadata = $"{fileSize} 0x{crc32:X8} {targetFirmwareVersion} {sha256Hex}";
        var payload = Encoding.ASCII.GetBytes(safeName + '\0' + metadata + '\0');
        var packetType = payload.Length <= PacketSize ? Soh : Stx;
        return BuildPacket(0, payload, packetType, 0x00);
    }

    /// <summary>
    /// 构造 YMODEM 结束空包。
    /// </summary>
    private static byte[] BuildFinalPacket()
    {
        return BuildPacket(0, Array.Empty<byte>(), Soh, 0x00);
    }

    /// <summary>
    /// 按 YMODEM 格式构造一个完整包。
    /// 包格式为：包头 + 包序号 + 反码 + 数据区 + CRC16。
    /// </summary>
    private static byte[] BuildPacket(int packetNo, byte[] payload, byte packetType, byte? padByte = null)
    {
        var packetSize = packetType switch
        {
            Stx => Packet1KSize,
            Soh => PacketSize,
            _ => throw new ArgumentOutOfRangeException(nameof(packetType), $"unsupported packet type: 0x{packetType:X2}")
        };

        if (payload.Length > packetSize)
        {
            throw new InvalidOperationException($"payload too large for packet: {payload.Length} > {packetSize}");
        }

        var pad = padByte ?? CpmEof;
        var padded = new byte[packetSize];
        Array.Fill(padded, pad);
        Array.Copy(payload, padded, payload.Length);

        var packet = new byte[3 + packetSize + 2];
        packet[0] = packetType;
        packet[1] = (byte)(packetNo & 0xFF);
        packet[2] = (byte)(~packetNo & 0xFF);
        Array.Copy(padded, 0, packet, 3, padded.Length);

        var crc = ComputeCrc16Xmodem(padded);
        packet[^2] = (byte)((crc >> 8) & 0xFF);
        packet[^1] = (byte)(crc & 0xFF);
        return packet;
    }

    /// <summary>
    /// 根据剩余字节数选择 1K 包还是 128 字节包。
    /// </summary>
    private static int ChooseDataBlock(int remaining)
    {
        return remaining >= Packet1KSize ? Packet1KSize : PacketSize;
    }

    /// <summary>
    /// 计算 YMODEM 使用的 XMODEM CRC16。
    /// </summary>
    private static ushort ComputeCrc16Xmodem(ReadOnlySpan<byte> data)
    {
        ushort crc = 0;
        foreach (var value in data)
        {
            crc ^= (ushort)(value << 8);
            for (var bit = 0; bit < 8; bit++)
            {
                crc = (crc & 0x8000) != 0
                    ? (ushort)(((crc << 1) ^ 0x1021) & 0xFFFF)
                    : (ushort)((crc << 1) & 0xFFFF);
            }
        }

        return crc;
    }

    /// <summary>
    /// 计算固件整体 CRC32，主要用于日志和头包附带校验信息。
    /// </summary>
    private static uint ComputeCrc32(ReadOnlySpan<byte> data)
    {
        var crc = 0xFFFFFFFFu;
        foreach (var value in data)
        {
            crc ^= value;
            for (var bit = 0; bit < 8; bit++)
            {
                crc = (crc & 1) != 0
                    ? (crc >> 1) ^ 0xEDB88320u
                    : crc >> 1;
            }
        }

        return ~crc;
    }
}

/// <summary>
/// 本地升级所需的全部参数。
/// </summary>
internal readonly record struct LocalUpgradeOptions(
    string PortName,
    int BaudRate,
    double TimeoutSeconds,
    string ImagePath,
    uint TargetFirmwareVersion = 0);
