using System.IO;
using Microsoft.Win32;
using System.Windows;
using System.Windows.Controls;
using OTA.ViewModels;
using UserControl = System.Windows.Controls.UserControl;

namespace OTA.UI.Views;

/// <summary>
/// 本地升级页面视图。
/// 页面内部只处理目录对话框和下拉框交互，状态与业务命令由 LocalUpgradeViewModel 提供。
/// </summary>
public partial class LocalUpgradeView : UserControl
{
    public LocalUpgradeView()
    {
        InitializeComponent();
    }

    private async void PortComboBox_OnSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (DataContext is LocalUpgradeViewModel viewModel)
        {
            await viewModel.OnPortSelectionChangedAsync();
        }
    }

    private void BrowseScriptButton_OnClick(object sender, RoutedEventArgs e)
    {
        if (DataContext is not LocalUpgradeViewModel viewModel)
        {
            return;
        }

        var currentDirectory = Path.GetDirectoryName(viewModel.ScriptPath);
        var dialog = new OpenFolderDialog
        {
            InitialDirectory = !string.IsNullOrWhiteSpace(currentDirectory) && Directory.Exists(currentDirectory)
                ? currentDirectory
                : @"D:\Project\STM32_Mill",
            Title = "选择 STM32 程序目录"
        };

        if (dialog.ShowDialog(Window.GetWindow(this)) == true && !string.IsNullOrWhiteSpace(dialog.FolderName))
        {
            viewModel.ApplySelectedImageDirectory(dialog.FolderName);
        }
    }
}
