using System.IO;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using OTA.ViewModels;

namespace OTA.UI.Views;

/// <summary>
/// 本地升级页面视图。
/// 页面内部只处理文件对话框和下拉框交互，状态与业务命令仍由 MainViewModel 提供。
/// </summary>
public partial class LocalUpgradeView : UserControl
{
    public LocalUpgradeView()
    {
        InitializeComponent();
    }

    private async void PortComboBox_OnSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (DataContext is MainViewModel viewModel)
        {
            await viewModel.OnPortSelectionChangedAsync();
        }
    }

    private void BrowseScriptButton_OnClick(object sender, RoutedEventArgs e)
    {
        if (DataContext is not MainViewModel viewModel)
        {
            return;
        }

        var owner = Window.GetWindow(this);
        var currentDirectory = Path.GetDirectoryName(viewModel.ScriptPath);
        var dialog = new OpenFileDialog
        {
            InitialDirectory = !string.IsNullOrWhiteSpace(currentDirectory) && Directory.Exists(currentDirectory)
                ? currentDirectory
                : @"D:\Project\STM32_Mill",
            FileName = Path.GetFileName(viewModel.ScriptPath),
            Title = "选择 STM32 程序文件",
            Filter = "BIN 文件 (*.bin)|*.bin|所有文件 (*.*)|*.*"
        };

        if (dialog.ShowDialog(owner) == true)
        {
            viewModel.ApplySelectedImagePath(dialog.FileName);
        }
    }
}
