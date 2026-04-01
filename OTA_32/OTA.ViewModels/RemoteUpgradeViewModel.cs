using CommunityToolkit.Mvvm.ComponentModel;

namespace OTA.ViewModels;

/// <summary>
/// 远程升级页面的占位 ViewModel。
/// 后续远程 OTA 功能落地时在这里继续承接页面状态与命令。
/// </summary>
public sealed class RemoteUpgradeViewModel : ObservableObject
{
    public string Title => "STM32 远程 OTA 升级";

    public string Description => "远程升级页已拆分为独立视图，后续在这里承载地址输入、清单下载和在线升级流程。";

    public string StatusTitle => "页面状态";

    public string StatusText => "当前阶段仅保留独立占位视图，尚未接入远程升级业务逻辑。";
}
