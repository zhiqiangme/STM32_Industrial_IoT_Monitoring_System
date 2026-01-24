using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;

namespace Project
{
    /// <summary>
    /// 窗口辅助类，用于启用 Windows 11 特有的视觉效果（如云母效果、深色标题栏）
    /// </summary>
    public static class WindowHelper
    {
        // 引入 dwmapi.dll 中的 DwmSetWindowAttribute 函数，用于设置窗口属性
        [DllImport("dwmapi.dll")]
        private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int attrValue, int attrSize);

        // title bar 深色模式属性常量
        private const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
        // Mica (云母) 效果属性常量
        private const int DWMWA_MICA_EFFECT = 1029;

        /// <summary>
        /// 启用窗口的深色模式（沉浸式深色模式）
        /// </summary>
        /// <param name="window">目标窗口</param>
        public static void EnableDarkMode(Window window)
        {
            if (ENVIRONMENT_OS_VERSION_IS_WIN11_OR_LATER())
            {
                var windowInteropHelper = new WindowInteropHelper(window);
                var hwnd = windowInteropHelper.Handle;
                var trueValue = 1; // 1 表示启用
                // 设置窗口属性以使用深色模式
                DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, ref trueValue, sizeof(int));
            }
        }

        /// <summary>
        /// 启用窗口的 Mica (云母) 背景效果
        /// </summary>
        /// <param name="window">目标窗口</param>
        public static void EnableMicaEffect(Window window)
        {
            if (ENVIRONMENT_OS_VERSION_IS_WIN11_OR_LATER())
            {
                var windowInteropHelper = new WindowInteropHelper(window);
                var hwnd = windowInteropHelper.Handle;
                var trueValue = 1; // 1 表示启用
                // 设置窗口属性以启用 Mica 效果
                DwmSetWindowAttribute(hwnd, DWMWA_MICA_EFFECT, ref trueValue, sizeof(int));
                
                // 必须将窗口背景设置为透明，Mica 效果才能透出来
                window.Background = Brushes.Transparent;
            }
        }

        /// <summary>
        /// 检查操作系统版本是否为 Windows 11 或更高版本
        /// </summary>
        /// <returns>如果是 Windows 11+ 则返回 true</returns>
        private static bool ENVIRONMENT_OS_VERSION_IS_WIN11_OR_LATER()
        {
             // Windows 11 的内部版本号通常从 22000 开始，主版本号为 10
             return Environment.OSVersion.Version.Major >= 10 && Environment.OSVersion.Version.Build >= 22000;
        }
    }
}
