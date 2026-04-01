// PortModels.cs
// 放串口发现与展示使用的纯数据结构。

namespace Project;

internal sealed record PortOption(string PortName, string DisplayName)
{
    public bool HasDisplayDetail =>
        !string.IsNullOrWhiteSpace(DisplayName) &&
        !string.Equals(DisplayName, PortName, StringComparison.OrdinalIgnoreCase);

    public string DisplayText => HasDisplayDetail ? $"{PortName} {DisplayName}" : PortName;

    public bool IsCh340 => DisplayName.Contains("CH340", StringComparison.OrdinalIgnoreCase);

    public bool IsUsbSerial =>
        DisplayName.Contains("USB", StringComparison.OrdinalIgnoreCase) &&
        (DisplayName.Contains("SERIAL", StringComparison.OrdinalIgnoreCase) ||
         DisplayName.Contains("串行", StringComparison.OrdinalIgnoreCase));

    public override string ToString()
    {
        return PortName;
    }
}
