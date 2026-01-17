using System;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;

namespace Project
{
    public static class WindowHelper
    {
        [DllImport("dwmapi.dll")]
        private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int attrValue, int attrSize);

        private const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
        private const int DWMWA_MICA_EFFECT = 1029;

        public static void EnableDarkMode(Window window)
        {
            if (ENVIRONMENT_OS_VERSION_IS_WIN11_OR_LATER())
            {
                var windowInteropHelper = new WindowInteropHelper(window);
                var hwnd = windowInteropHelper.Handle;
                var trueValue = 1;
                DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, ref trueValue, sizeof(int));
            }
        }

        public static void EnableMicaEffect(Window window)
        {
            if (ENVIRONMENT_OS_VERSION_IS_WIN11_OR_LATER())
            {
                var windowInteropHelper = new WindowInteropHelper(window);
                var hwnd = windowInteropHelper.Handle;
                var trueValue = 1; 
                DwmSetWindowAttribute(hwnd, DWMWA_MICA_EFFECT, ref trueValue, sizeof(int));
                
                window.Background = Brushes.Transparent;
            }
        }

        private static bool ENVIRONMENT_OS_VERSION_IS_WIN11_OR_LATER()
        {
             return Environment.OSVersion.Version.Major >= 10 && Environment.OSVersion.Version.Build >= 22000;
        }
    }
}
