namespace OTA.Models;

/// <summary>
/// 运行槽位探测结果。
/// </summary>
public readonly record struct RunningSlotRefreshResult(
    FirmwareSlot Slot,
    string RecommendationText,
    bool TreatUnknownAsUnread,
    OtaError? Error = null)
{
    public bool HasError => Error is not null;

    public OtaErrorCode? ErrorCode => Error?.Code;

    public string ErrorMessage => Error?.Message ?? string.Empty;
}
