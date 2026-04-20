namespace OTA.Models;

/// <summary>
/// 升级流程日志输出抽象。
/// </summary>
public interface IUpgradeLogSink
{
    void Write(string message);

    void WriteProgress(string message);
}
