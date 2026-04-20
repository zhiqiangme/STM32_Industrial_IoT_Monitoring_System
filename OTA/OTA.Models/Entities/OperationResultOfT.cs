namespace OTA.Models;

/// <summary>
/// 统一的带返回值操作结果。
/// </summary>
public readonly record struct OperationResult<T>
{
    private OperationResult(bool isSuccess, T value, OtaError? error)
    {
        IsSuccess = isSuccess;
        Value = value;
        Error = error;
    }

    public bool IsSuccess { get; }

    public T Value { get; }

    public OtaError? Error { get; }

    public OtaErrorCode? ErrorCode => Error?.Code;

    public string ErrorMessage => Error?.Message ?? string.Empty;

    public static OperationResult<T> Success(T value)
    {
        return new OperationResult<T>(true, value, null);
    }

    public static OperationResult<T> Failure(OtaError error)
    {
        ArgumentNullException.ThrowIfNull(error);
        return new OperationResult<T>(false, default!, error);
    }

    public static OperationResult<T> Failure(OtaErrorCode code, string message)
    {
        return Failure(new OtaError(code, message));
    }
}
