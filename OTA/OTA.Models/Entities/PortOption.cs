namespace OTA.Models;

/// <summary>
/// 串口下拉项。
/// PortName 用于闭合状态显示和实际操作，DisplayName 用于展开列表补充设备类型。
/// </summary>
public sealed record PortOption(string PortName, string DisplayName)
{
    public bool HasDisplayDetail =>
        !string.IsNullOrWhiteSpace(DisplayName) &&
        !string.Equals(DisplayName, PortName, StringComparison.OrdinalIgnoreCase);

    public string DisplayText => HasDisplayDetail ? $"{PortName} {DisplayName}" : PortName;

    public bool IsCh340 => DisplayName.Contains("CH340", StringComparison.OrdinalIgnoreCase);

    public bool IsBluetooth =>
        DisplayName.Contains("蓝牙", StringComparison.OrdinalIgnoreCase) ||
        DisplayName.Contains("Bluetooth", StringComparison.OrdinalIgnoreCase);

    public bool IsUsbSerial =>
        DisplayName.Contains("USB", StringComparison.OrdinalIgnoreCase) &&
        (DisplayName.Contains("SERIAL", StringComparison.OrdinalIgnoreCase) ||
         DisplayName.Contains("串行", StringComparison.OrdinalIgnoreCase));

    public bool IsUsrVcom =>
        DisplayName.Contains("USR", StringComparison.OrdinalIgnoreCase) &&
        DisplayName.Contains("VCOM", StringComparison.OrdinalIgnoreCase);

    public override string ToString()
    {
        return PortName;
    }
}
