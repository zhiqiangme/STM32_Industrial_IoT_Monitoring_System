using System.ComponentModel;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using OTA.ViewModels;
using OTA.ViewModels.Messages;
using Clipboard = System.Windows.Clipboard;
using HorizontalAlignment = System.Windows.HorizontalAlignment;
using KeyEventArgs = System.Windows.Input.KeyEventArgs;
using TextBox = System.Windows.Controls.TextBox;
using VerticalAlignment = System.Windows.VerticalAlignment;
using UserControl = System.Windows.Controls.UserControl;

namespace OTA.UI.Views;

/// <summary>
/// 远程维护页面视图。
/// 页面只处理剪贴板和回收站窗口等 WPF 专属交互。
/// </summary>
public partial class RemoteMaintenanceView : UserControl
{
    private RemoteMaintenanceViewModel? _viewModel;

    public RemoteMaintenanceView()
    {
        InitializeComponent();
        DataContextChanged += RemoteMaintenanceView_DataContextChanged;
        Unloaded += RemoteMaintenanceView_Unloaded;
    }

    private void RemoteMaintenanceView_DataContextChanged(object sender, DependencyPropertyChangedEventArgs e)
    {
        DetachViewModel();

        _viewModel = e.NewValue as RemoteMaintenanceViewModel;
        if (_viewModel is not null)
        {
            _viewModel.ClipboardTextRequested += ViewModel_ClipboardTextRequested;
            _viewModel.PropertyChanged += ViewModel_PropertyChanged;
        }
    }

    private void RemoteMaintenanceView_Unloaded(object sender, RoutedEventArgs e)
    {
        DetachViewModel();
    }

    private void ViewModel_ClipboardTextRequested(object? sender, ClipboardTextRequest e)
    {
        try
        {
            Clipboard.SetText(e.Text);
        }
        catch
        {
            // 剪贴板失败不阻断维护页主流程。
        }
    }

    private void ViewModel_PropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(RemoteMaintenanceViewModel.FrameImportText))
        {
            FrameImportTextBox.CaretIndex = FrameImportTextBox.Text.Length;
        }
    }

    private void FrameImportTextBox_OnPreviewMouseRightButtonUp(object sender, MouseButtonEventArgs e)
    {
        e.Handled = true;
        ImportClipboardButton_OnClick(sender, new RoutedEventArgs());
    }

    private void FrameImportTextBox_OnKeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key != Key.Enter || DataContext is not RemoteMaintenanceViewModel viewModel)
        {
            return;
        }

        e.Handled = true;
        if (viewModel.ImportFrameCommand.CanExecute(null))
        {
            viewModel.ImportFrameCommand.Execute(null);
        }
    }

    private void ImportClipboardButton_OnClick(object sender, RoutedEventArgs e)
    {
        if (DataContext is RemoteMaintenanceViewModel viewModel)
        {
            var clipboardText = Clipboard.ContainsText() ? Clipboard.GetText() : string.Empty;
            viewModel.ImportFrameFromClipboardText(clipboardText);
        }
    }

    private void RecycleBinButton_OnClick(object sender, RoutedEventArgs e)
    {
        if (DataContext is not RemoteMaintenanceViewModel viewModel)
        {
            return;
        }

        var recycleItems = viewModel.GetRecycleBinItems();
        var contentBox = new TextBox
        {
            Margin = new Thickness(16),
            FontFamily = new System.Windows.Media.FontFamily("Consolas"),
            FontSize = 15,
            IsReadOnly = true,
            AcceptsReturn = true,
            TextWrapping = TextWrapping.Wrap,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Auto,
            VerticalAlignment = VerticalAlignment.Stretch,
            HorizontalAlignment = HorizontalAlignment.Stretch,
            Height = double.NaN,
            MinHeight = 320,
            Text = recycleItems.Count == 0
                ? "回收站为空。"
                : string.Join(
                    Environment.NewLine + Environment.NewLine,
                    recycleItems.Select((item, index) => $"{index + 1}. {item}"))
        };

        var host = new Grid();
        host.Children.Add(contentBox);

        var recycleWindow = new Window
        {
            Title = "回收站",
            Owner = Window.GetWindow(this),
            Width = 720,
            Height = 480,
            MinWidth = 560,
            MinHeight = 360,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            Background = System.Windows.Media.Brushes.White,
            Content = host
        };

        recycleWindow.ShowDialog();
    }

    private void DetachViewModel()
    {
        if (_viewModel is null)
        {
            return;
        }

        _viewModel.ClipboardTextRequested -= ViewModel_ClipboardTextRequested;
        _viewModel.PropertyChanged -= ViewModel_PropertyChanged;
        _viewModel = null;
    }
}
