using System.IO;
using System.IO.Ports;
using System.Security.Cryptography;
using System.Text;

namespace Project;

internal static class LocalUpgradeService
{
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

    public static Task RunAsync(LocalUpgradeOptions options, Action<string> log)
    {
        ArgumentNullException.ThrowIfNull(log);
        return Task.Run(() => Execute(options, log));
    }

    private static void Execute(LocalUpgradeOptions options, Action<string> log)
    {
        var image = File.ReadAllBytes(options.ImagePath);
        var crc32 = ComputeCrc32(image);
        var sha256Hex = Convert.ToHexString(SHA256.HashData(image)).ToLowerInvariant();

        log($"固件: {options.ImagePath}");
        log($"固件大小: {image.Length} bytes (0x{image.Length:X8})");
        log($"固件 CRC32: 0x{crc32:X8}");
        log($"固件 SHA256: {sha256Hex}");

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
    }

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

    private static byte[] BuildInitialPacket(string fileName, int fileSize, uint crc32, string sha256Hex, uint targetFirmwareVersion)
    {
        var safeName = Path.GetFileName(fileName);
        var metadata = $"{fileSize} 0x{crc32:X8} {targetFirmwareVersion} {sha256Hex}";
        var payload = Encoding.ASCII.GetBytes(safeName + '\0' + metadata + '\0');
        var packetType = payload.Length <= PacketSize ? Soh : Stx;
        return BuildPacket(0, payload, packetType, 0x00);
    }

    private static byte[] BuildFinalPacket()
    {
        return BuildPacket(0, Array.Empty<byte>(), Soh, 0x00);
    }

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

    private static int ChooseDataBlock(int remaining)
    {
        return remaining >= Packet1KSize ? Packet1KSize : PacketSize;
    }

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

internal readonly record struct LocalUpgradeOptions(
    string PortName,
    int BaudRate,
    double TimeoutSeconds,
    string ImagePath,
    uint TargetFirmwareVersion = 0);
