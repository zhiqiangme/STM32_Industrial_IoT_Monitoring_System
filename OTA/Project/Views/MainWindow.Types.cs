// MainWindow.Types.cs
// 放主窗口局部使用的内部类型。

using System.Windows;

namespace Project;

public partial class MainWindow : Window
{
    /// <summary>
    /// 顶部标签对应的界面模式。
    /// </summary>
    private enum UpgradeMode
    {
        Local,
        Remote
    }
}
