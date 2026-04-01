// MainWindow.xaml.cs
// 放多个 partial 文件共享的窗口常量、字段、定时器和运行时状态。

using System.Threading;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;

namespace Project;

/// <summary>
/// 主窗口共享状态。
/// 负责承载多个 partial 文件共用的字段、定时器和常量。
/// </summary>
public partial class MainWindow : Window
{
    // 默认镜像路径配置。
    private const string DefaultObjectsDirectory = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects";
    private const string DefaultSlotAImagePath = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects\App_A.bin";
    private const string DefaultSlotBImagePath = @"D:\Project\STM32_Mill\STM32\MDK-ARM\Objects\App_B.bin";

    // Windows 设备变更消息常量，用于监听串口插拔。
    private const int WmDeviceChange = 0x0219;
    private const int DbtDeviceArrival = 0x8000;
    private const int DbtDeviceRemoveComplete = 0x8004;
    private const int DbtDevNodesChanged = 0x0007;

    // 当前界面模式与刷新状态。
    private UpgradeMode _currentMode = UpgradeMode.Local;
    private bool _isBusy;
    private bool _isRefreshingPortList;
    private bool _isRefreshingRunningSlot;
    private bool _suppressPortSelectionRefresh;
    private int _consecutiveNoPortRefreshCount;

    // 当前识别到的运行槽位，以及最近使用/推荐的镜像路径。
    private FirmwareSlot _runningSlot = FirmwareSlot.Unknown;
    private string _lastLocalImagePath = DefaultSlotAImagePath;
    private string? _lastAutoSuggestedImagePath = DefaultSlotAImagePath;

    // 上一次已知串口列表，用于识别“新插入”的串口。
    private HashSet<string> _knownPortNames = new(StringComparer.OrdinalIgnoreCase);

    // 防止串口刷新与槽位读取并发重入。
    private readonly SemaphoreSlim _runningSlotRefreshLock = new(1, 1);
    private readonly SemaphoreSlim _portListRefreshLock = new(1, 1);
    private readonly PortDiscoveryService _portDiscoveryService = new();
    private readonly RunningSlotService _runningSlotService = new();

    // 空闲轮询和设备插拔防抖刷新定时器。
    private readonly DispatcherTimer _idlePortListRefreshTimer;
    private readonly DispatcherTimer _idleRunningSlotRefreshTimer;
    private readonly DispatcherTimer _deviceChangeRefreshTimer;
    private HwndSource? _hwndSource;

    // 运行日志“连续重复覆盖”所需的上一条状态。
    private string? _lastLoggedMessage;
    private int _lastLoggedLineLength;
}
