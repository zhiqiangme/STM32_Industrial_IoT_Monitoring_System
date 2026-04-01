// UpgradeAbModels.cs
// 放 A/B 槽位相关的纯数据结构和显示扩展。

namespace Project;

internal enum FirmwareSlot
{
    Unknown = 0,
    A = 1,
    B = 2
}

internal readonly record struct FirmwareImageInfo(
    string ImagePath,
    FirmwareSlot DetectedSlot,
    FirmwareSlot FileNameSlot,
    FirmwareSlot VectorSlot,
    uint InitialStackPointer,
    uint ResetHandler);

internal readonly record struct RunningSlotReadResult(bool Success, FirmwareSlot Slot, string? ErrorMessage);

internal static class FirmwareSlotExtensions
{
    public static string ToDisplayText(this FirmwareSlot slot)
    {
        return slot switch
        {
            FirmwareSlot.A => "A",
            FirmwareSlot.B => "B",
            _ => "未知"
        };
    }

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
