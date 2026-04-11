namespace OTA.Models;

/// <summary>
/// 串口升级页面的模式配置。
/// 用于区分本地升级与远程升级的文案、默认值和串口提示。
/// </summary>
public sealed record SerialUpgradeModeProfile(
    string ModeTitle,
    string ModeDescription,
    string ModeHint,
    string PathLabel,
    string StartButtonText,
    string DefaultBaudRateText,
    string DefaultTimeoutText,
    string PreparationModeName,
    string DisconnectedPortMessageTemplate,
    string BrowseDialogTitle);
