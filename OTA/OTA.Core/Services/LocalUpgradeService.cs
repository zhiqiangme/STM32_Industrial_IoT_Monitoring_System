using System.Security.Cryptography;
using OTA.Models;
using OTA.Protocols;

namespace OTA.Core;

/// <summary>
/// 本地 OTA 升级执行器。
/// 完整流程为：
/// 1. 打开串口并打印固件摘要；
/// 2. 发送解锁帧；
/// 3. 发送进入 Bootloader 帧；
/// 4. 等待 Bootloader 进入 YMODEM 握手；
/// 5. 发送头包、数据包、EOT 和结束空包。
/// </summary>
public static class LocalUpgradeService
{
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
        var crc32 = YModemProtocol.ComputeCrc32(image);
        var sha256Hex = Convert.ToHexString(SHA256.HashData(image)).ToLowerInvariant();

        log($"固件: {options.ImagePath}");
        log($"固件大小: {image.Length} bytes (0x{image.Length:X8})");
        log($"固件 CRC32: 0x{crc32:X8}");
        log($"固件 SHA256: {sha256Hex}");

        LocalUpgradeTransport.Run(
            options,
            image,
            crc32,
            sha256Hex,
            UnlockCommand,
            EnterBootloaderCommand,
            AppCommandGap,
            BootloaderSwitchDelay,
            HandshakeWindow,
            log);
    }
}
