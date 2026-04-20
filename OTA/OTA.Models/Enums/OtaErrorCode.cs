namespace OTA.Models;

/// <summary>
/// OTA 业务统一错误码。
/// 面向上层调用方表达可分支处理的失败原因。
/// </summary>
public enum OtaErrorCode
{
    InternalError = 0,

    PortNameRequired,
    InvalidBaudRate,
    InvalidTimeout,
    ImagePathRequired,
    ImageFileNotFound,

    ImageTooShort,
    ImageHeaderReadFailed,
    ImageSlotConflict,
    ImageSlotUnknown,
    SameSlotUpgradeNotAllowed,

    PortUnavailable,
    PortOpenFailed,
    PortBusy,
    PortDisconnected,

    RunningSlotReadTimeout,
    RunningSlotInvalidResponseLength,
    RunningSlotInvalidDeviceAddress,
    RunningSlotInvalidFunctionCode,
    RunningSlotInvalidDataLength,
    RunningSlotCrcMismatch,
    RunningSlotUnknown,
    RunningSlotReadFailed,

    UpgradeHandshakeTimeout,
    UpgradeSessionAborted,
    UpgradeAckFailed,
    UpgradeEotNotConfirmed,
    UpgradeTransmissionFailed,

    InvalidSlaveAddress,
    InvalidFunctionCodeInput,
    InvalidRegisterAddress,
    InvalidDataValue,
    EmptyRawFrame,
    RawFrameTooShort,
    RawFrameContainsNonHex,
    RawFrameInvalid
}
