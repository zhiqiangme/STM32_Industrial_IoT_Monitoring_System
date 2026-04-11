using OTA.Models;

namespace OTA.ViewModels;

/// <summary>
/// 串口升级页面的默认串口选择策略。
/// </summary>
public static class SerialUpgradePortSelector
{
    /// <summary>
    /// 为本地升级页面选择默认串口，优先 USB 串口与 CH340。
    /// </summary>
    public static PortOption? SelectLocalPreferredPort(IReadOnlyList<PortOption> portOptions, IReadOnlySet<string> newlyAddedPortNames)
    {
        if (portOptions.Count == 0)
        {
            return null;
        }

        var newlyAddedOptions = portOptions
            .Where(option => !option.IsBluetooth)
            .Where(option => newlyAddedPortNames.Contains(option.PortName))
            .ToList();

        var supportedOptions = portOptions
            .Where(option => !option.IsBluetooth)
            .ToList();

        return newlyAddedOptions.FirstOrDefault(option => option.IsUsbSerial) ??
               newlyAddedOptions.FirstOrDefault(option => option.IsCh340) ??
               newlyAddedOptions.FirstOrDefault() ??
               supportedOptions.FirstOrDefault(option => option.IsUsbSerial) ??
               supportedOptions.FirstOrDefault(option => option.IsCh340) ??
               supportedOptions.FirstOrDefault();
    }

    /// <summary>
    /// 为远程升级页面选择默认串口，优先指定端口名和 USR VCOM 虚拟串口。
    /// </summary>
    public static PortOption? SelectRemotePreferredPort(
        IReadOnlyList<PortOption> portOptions,
        IReadOnlySet<string> newlyAddedPortNames,
        string defaultPortName = "COM80")
    {
        if (portOptions.Count == 0)
        {
            return null;
        }

        var exactDefault = portOptions.FirstOrDefault(option => string.Equals(option.PortName, defaultPortName, StringComparison.OrdinalIgnoreCase));
        if (exactDefault is not null)
        {
            return exactDefault;
        }

        var usrVcomOption = portOptions.FirstOrDefault(option => option.IsUsrVcom);
        if (usrVcomOption is not null)
        {
            return usrVcomOption;
        }

        var newlyAddedDefault = portOptions.FirstOrDefault(option => newlyAddedPortNames.Contains(option.PortName));
        return newlyAddedDefault ?? portOptions.FirstOrDefault();
    }
}
