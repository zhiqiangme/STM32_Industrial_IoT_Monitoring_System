using System.IO;
using Microsoft.Win32;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using OTA.ViewModels;
using UserControl = System.Windows.Controls.UserControl;

namespace OTA.UI.Views;

/// <summary>
/// 本地升级与远程升级共用的串口升级页面视图。
/// 页面内部只处理目录对话框和下拉框交互，状态与业务命令由 ViewModel 提供。
/// </summary>
public partial class SerialUpgradeView : UserControl
{
    private ScrollViewer? _logScrollViewer;
    private bool _autoScrollLog = true;

    /// <summary>
    /// 初始化共享串口升级页面视图。
    /// </summary>
    public SerialUpgradeView()
    {
        InitializeComponent();
    }

    /// <summary>
    /// 串口选择变化后通知 ViewModel 刷新槽位提示。
    /// </summary>
    private async void PortComboBox_OnSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (DataContext is SerialUpgradeViewModelBase viewModel)
        {
            await viewModel.OnPortSelectionChangedAsync();
        }
    }

    /// <summary>
    /// 打开目录选择对话框，并把选择结果交给 ViewModel 处理。
    /// </summary>
    private void BrowseScriptButton_OnClick(object sender, RoutedEventArgs e)
    {
        if (DataContext is not SerialUpgradeViewModelBase viewModel)
        {
            return;
        }

        var currentDirectory = Path.GetDirectoryName(viewModel.ScriptPath);
        var dialog = new OpenFolderDialog
        {
            InitialDirectory = !string.IsNullOrWhiteSpace(currentDirectory) && Directory.Exists(currentDirectory)
                ? currentDirectory
                : @"D:\Project\STM32_Mill",
            Title = viewModel.BrowseDialogTitle
        };

        if (dialog.ShowDialog(Window.GetWindow(this)) == true && !string.IsNullOrWhiteSpace(dialog.FolderName))
        {
            viewModel.ApplySelectedImageDirectory(dialog.FolderName);
        }
    }

    /// <summary>
    /// 在日志框加载后挂接滚动事件，并在允许时滚动到末尾。
    /// </summary>
    private void LogTextBox_OnLoaded(object sender, RoutedEventArgs e)
    {
        _logScrollViewer ??= FindDescendant<ScrollViewer>(LogTextBox);
        if (_logScrollViewer is not null)
        {
            _logScrollViewer.ScrollChanged -= LogScrollViewer_OnScrollChanged;
            _logScrollViewer.ScrollChanged += LogScrollViewer_OnScrollChanged;
        }

        if (_autoScrollLog)
        {
            LogTextBox.ScrollToEnd();
        }
    }

    /// <summary>
    /// 日志文本变化时，如果用户没有上翻日志则自动滚动到底部。
    /// </summary>
    private void LogTextBox_OnTextChanged(object sender, TextChangedEventArgs e)
    {
        if (_autoScrollLog)
        {
            LogTextBox.ScrollToEnd();
        }
    }

    /// <summary>
    /// 用户向上滚动日志时暂停自动置底，便于查看旧日志。
    /// </summary>
    private void LogTextBox_OnPreviewMouseWheel(object sender, MouseWheelEventArgs e)
    {
        _logScrollViewer ??= FindDescendant<ScrollViewer>(LogTextBox);
        if (_logScrollViewer is null)
        {
            return;
        }

        if (e.Delta > 0 && _logScrollViewer.VerticalOffset > 0)
        {
            _autoScrollLog = false;
        }
    }

    /// <summary>
    /// 根据当前滚动位置更新日志是否自动置底的状态。
    /// </summary>
    private void LogScrollViewer_OnScrollChanged(object sender, ScrollChangedEventArgs e)
    {
        if (e.ExtentHeightChange != 0)
        {
            return;
        }

        _autoScrollLog = IsNearBottom((ScrollViewer)sender);
    }

    /// <summary>
    /// 判断滚动条是否已经接近底部。
    /// </summary>
    private static bool IsNearBottom(ScrollViewer scrollViewer)
    {
        return scrollViewer.VerticalOffset >= scrollViewer.ScrollableHeight - 1d;
    }

    /// <summary>
    /// 在可视树中递归查找指定类型的子元素。
    /// </summary>
    private static T? FindDescendant<T>(DependencyObject? root) where T : DependencyObject
    {
        if (root is null)
        {
            return null;
        }

        for (var index = 0; index < VisualTreeHelper.GetChildrenCount(root); index++)
        {
            var child = VisualTreeHelper.GetChild(root, index);
            if (child is T typedChild)
            {
                return typedChild;
            }

            var nested = FindDescendant<T>(child);
            if (nested is not null)
            {
                return nested;
            }
        }

        return null;
    }
}
