using OTA.Core;
using OTA.Models;

namespace OTA.ViewModels;

/// <summary>
/// 本地升级页面的状态与交互入口。
/// </summary>
public sealed class LocalUpgradeViewModel : SerialUpgradeViewModelBase
{
    private static readonly SerialUpgradeModeProfile Profile = new(
        ModeTitle: "STM32 本地 OTA 升级",
        ModeDescription: "流程：发送解锁 HEX -> 等待回包 -> 发送进入 Bootloader HEX -> 由 C# 内置 YMODEM 发送 STM32 程序。",
        ModeHint: "默认优先选择 USB-SERIAL CH340 串口。",
        PathLabel: "STM32程序路径",
        StartButtonText: "进入 Bootloader 并刷机",
        DefaultBaudRateText: "115200",
        DefaultTimeoutText: "5",
        PreparationModeName: "本地升级",
        DisconnectedPortMessageTemplate: "串口 {0} 当前未连接。请先连接 USB 转 485 串口设备并确认端口号。",
        BrowseDialogTitle: "选择 STM32 程序目录");

    public LocalUpgradeViewModel(
        PortDiscoveryService portDiscoveryService,
        LocalUpgradeCoordinator upgradeCoordinator,
        AppPreferencesService preferencesService)
        : base(
            portDiscoveryService,
            upgradeCoordinator,
            Profile,
            () =>
            {
                var preferences = preferencesService.GetLocalUpgradePreferences();
                return new SerialUpgradePreferenceSnapshot(
                    preferences.LastFirmwarePath,
                    preferences.LastPortName,
                    preferences.BaudRateText);
            },
            preferencesService.SaveLocalUpgradePreferences)
    {
    }
}
