using System.Windows;
using Microsoft.Extensions.DependencyInjection;
using OTA.Core;
using OTA.ViewModels;

namespace OTA.UI;

/// <summary>
/// WPF 应用入口。
/// 当前阶段负责组装 Core、ViewModel 和主窗口，
/// 让窗口只承担视图职责，不再自己实例化业务对象。
/// </summary>
public partial class App : Application
{
    private ServiceProvider? _serviceProvider;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        // Phase 3 keeps DI explicit and small: App owns object assembly,
        // MainWindow only receives the ViewModel it needs to display.
        var services = new ServiceCollection();
        services.AddSingleton<PortDiscoveryService>();
        services.AddSingleton<LocalUpgradeCoordinator>();
        services.AddSingleton<MainViewModel>();
        services.AddTransient<MainWindow>();
        _serviceProvider = services.BuildServiceProvider();

        var mainWindow = _serviceProvider.GetRequiredService<MainWindow>();
        MainWindow = mainWindow;
        mainWindow.Show();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        // ServiceProvider owns singleton lifetimes created during startup.
        _serviceProvider?.Dispose();
        base.OnExit(e);
    }
}
