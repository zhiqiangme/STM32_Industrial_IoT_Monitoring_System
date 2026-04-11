using CommunityToolkit.Mvvm.ComponentModel;

namespace OTA.ViewModels;

/// <summary>
/// 应用壳层 ViewModel。
/// 只负责管理页签状态，并持有本地升级与远程升级两个子模块。
/// </summary>
public sealed class MainViewModel : ObservableObject
{
    private int _selectedTabIndex = 1;

    public MainViewModel(
        LocalUpgradeViewModel localVM,
        RemoteUpgradeViewModel remoteVM,
        RemoteMaintenanceViewModel maintenanceVM)
    {
        LocalVM = localVM;
        RemoteVM = remoteVM;
        MaintenanceVM = maintenanceVM;
    }

    public LocalUpgradeViewModel LocalVM { get; }

    public RemoteUpgradeViewModel RemoteVM { get; }

    public RemoteMaintenanceViewModel MaintenanceVM { get; }

    public SerialUpgradeViewModelBase? ActiveSerialUpgradeVM => SelectedTabIndex switch
    {
        0 => LocalVM,
        1 => RemoteVM,
        _ => null
    };

    public bool IsLocalTabSelected => SelectedTabIndex == 0;

    public bool IsRemoteTabSelected => SelectedTabIndex == 1;

    public bool IsSerialUpgradeTabSelected => SelectedTabIndex is 0 or 1;

    public bool IsMaintenanceTabSelected => SelectedTabIndex == 2;

    public int SelectedTabIndex
    {
        get => _selectedTabIndex;
        set
        {
            if (SetProperty(ref _selectedTabIndex, value))
            {
                OnPropertyChanged(nameof(ActiveSerialUpgradeVM));
                OnPropertyChanged(nameof(IsLocalTabSelected));
                OnPropertyChanged(nameof(IsRemoteTabSelected));
                OnPropertyChanged(nameof(IsSerialUpgradeTabSelected));
                OnPropertyChanged(nameof(IsMaintenanceTabSelected));
            }
        }
    }
}
