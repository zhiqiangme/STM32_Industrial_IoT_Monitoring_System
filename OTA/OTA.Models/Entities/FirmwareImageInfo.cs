namespace OTA.Models;

/// <summary>
/// 固件镜像识别结果。
/// 同时保存文件名推断结果、向量表推断结果以及关键地址，便于界面做提示和校验。
/// </summary>
public readonly record struct FirmwareImageInfo(
    string ImagePath,
    FirmwareSlot DetectedSlot,
    FirmwareSlot FileNameSlot,
    FirmwareSlot VectorSlot,
    uint InitialStackPointer,
    uint ResetHandler);
