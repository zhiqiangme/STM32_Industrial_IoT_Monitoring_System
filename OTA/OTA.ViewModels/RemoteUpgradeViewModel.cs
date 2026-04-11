using OTA.Core;
using OTA.Models;

namespace OTA.ViewModels;

/// <summary>
/// 远程升级页面的状态与交互入口。
/// </summary>
public sealed class RemoteUpgradeViewModel : SerialUpgradeViewModelBase
{
    private static readonly SerialUpgradeModeProfile Profile = new(
        ModeTitle: "STM32 远程 OTA 升级（虚拟串口）",
        ModeDescription: "流程与本地升级一致：通过 USR-VCOM 映射出来的虚拟串口，向 G780S 透传解锁指令、进入 Bootloader 指令和 YMODEM 固件数据。",
        ModeHint: "请先在 USR-VCOM 中创建并打开映射串口；默认优先选择 USR VCOM 虚拟串口。",
        PathLabel: "STM32程序路径",
        StartButtonText: "通过虚拟串口进入 Bootloader 并刷机",
        DefaultBaudRateText: "115200",
        DefaultTimeoutText: "10",
        PreparationModeName: "远程升级",
        DisconnectedPortMessageTemplate: "串口 {0} 当前未连接。请确认 USR-VCOM 映射串口已创建并处于连接状态。",
        BrowseDialogTitle: "选择 STM32 程序目录");

    public RemoteUpgradeViewModel(
        PortDiscoveryService portDiscoveryService,
        LocalUpgradeCoordinator upgradeCoordinator,
        AppPreferencesService preferencesService)
        : base(
            portDiscoveryService,
            upgradeCoordinator,
            Profile,
            () =>
            {
                var preferences = preferencesService.GetRemoteUpgradePreferences();
                return new SerialUpgradePreferenceSnapshot(
                    preferences.LastFirmwarePath,
                    preferences.LastPortName,
                    preferences.BaudRateText);
            },
            preferencesService.SaveRemoteUpgradePreferences)
    {
    }

    protected override PortOption? SelectPreferredPort(IReadOnlyList<PortOption> portOptions, IReadOnlySet<string> newlyAddedPortNames)
    {
        return SerialUpgradePortSelector.SelectRemotePreferredPort(portOptions, newlyAddedPortNames, "COM80");
    }
}
