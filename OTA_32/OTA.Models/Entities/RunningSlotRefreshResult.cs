namespace OTA.Models;

/// <summary>
/// 运行槽位探测结果。
/// </summary>
public readonly record struct RunningSlotRefreshResult(
    FirmwareSlot Slot,
    string RecommendationText,
    bool TreatUnknownAsUnread,
    string? ErrorMessage = null)
{
    public bool HasError => !string.IsNullOrWhiteSpace(ErrorMessage);
}
