namespace OTA.Models;

/// <summary>
/// 统一的无返回值操作结果。
/// </summary>
public readonly record struct OperationResult
{
    private OperationResult(bool isSuccess, OtaError? error)
    {
        IsSuccess = isSuccess;
        Error = error;
    }

    public bool IsSuccess { get; }

    public OtaError? Error { get; }

    public OtaErrorCode? ErrorCode => Error?.Code;

    public string ErrorMessage => Error?.Message ?? string.Empty;

    public static OperationResult Success()
    {
        return new OperationResult(true, null);
    }

    public static OperationResult Failure(OtaError error)
    {
        ArgumentNullException.ThrowIfNull(error);
        return new OperationResult(false, error);
    }

    public static OperationResult Failure(OtaErrorCode code, string message)
    {
        return Failure(new OtaError(code, message));
    }
}
