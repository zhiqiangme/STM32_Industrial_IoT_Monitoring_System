namespace OTA.ViewModels.Messages;

/// <summary>
/// ViewModel 发给视图层的一次性消息。
/// 用于参数校验、执行失败等需要弹窗提示但不应直接依赖 WPF 的场景。
/// </summary>
public sealed class ViewMessage : EventArgs
{
    public ViewMessage(string title, string message, ViewMessageSeverity severity)
    {
        Title = title;
        Message = message;
        Severity = severity;
    }

    public string Title { get; }

    public string Message { get; }

    public ViewMessageSeverity Severity { get; }
}

public enum ViewMessageSeverity
{
    Info,
    Warning,
    Error
}
