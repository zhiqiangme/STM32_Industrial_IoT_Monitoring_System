namespace OTA.Models;

/// <summary>
/// 固件槽位枚举。
/// 当前 STM32 采用 A/B 双镜像升级，因此界面和升级逻辑都围绕这两个槽位展开。
/// </summary>
public enum FirmwareSlot
{
    Unknown = 0,
    A = 1,
    B = 2
}

/// <summary>
/// 槽位相关的显示与换槽辅助方法。
/// </summary>
public static class FirmwareSlotExtensions
{
    /// <summary>
    /// 把内部枚举转换成界面显示文本。
    /// </summary>
    public static string ToDisplayText(this FirmwareSlot slot)
    {
        return slot switch
        {
            FirmwareSlot.A => "A",
            FirmwareSlot.B => "B",
            _ => "未知"
        };
    }

    /// <summary>
    /// 获取当前槽位的另一槽。
    /// 例如当前运行 A，则建议升级 B。
    /// </summary>
    public static FirmwareSlot GetOpposite(this FirmwareSlot slot)
    {
        return slot switch
        {
            FirmwareSlot.A => FirmwareSlot.B,
            FirmwareSlot.B => FirmwareSlot.A,
            _ => FirmwareSlot.Unknown
        };
    }
}
