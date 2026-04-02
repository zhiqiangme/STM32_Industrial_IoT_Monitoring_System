namespace OTA.ViewModels.Messages;

/// <summary>
/// ViewModel 请求视图层复制文本到剪贴板。
/// </summary>
public sealed class ClipboardTextRequest : EventArgs
{
    public ClipboardTextRequest(string text)
    {
        Text = text;
    }

    public string Text { get; }
}
