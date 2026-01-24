using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;
using System.Windows.Threading;
using NModbus;
using NModbus.Serial;
using System.Management;
using System.Text.RegularExpressions;

namespace Project
{
    /// <summary>
    /// 主窗口交互逻辑类
    /// </summary>
    public partial class MainWindow : Window
    {
        // Modbus 从站 ID (下位机设备地址)
        private const byte SlaveId = 10;
        // 一次性读取的寄存器数量
        private const ushort RegisterCount = 22;
        
        // --- 寄存器地址定义 (基于 Modbus 协议) ---
        private const ushort REG_PT100_CH1 = 0x0001;      // PT100 温度通道 1
        private const ushort REG_PT100_CH2 = 0x0002;      // PT100 温度通道 2
        private const ushort REG_PT100_CH3 = 0x0003;      // PT100 温度通道 3
        private const ushort REG_PT100_CH4 = 0x0004;      // PT100 温度通道 4
        private const ushort REG_WEIGHT_CH1_H = 0x0005;   // 称重通道 1 (高16位)
        private const ushort REG_WEIGHT_CH1_L = 0x0006;   // 称重通道 1 (低16位)
        private const ushort REG_WEIGHT_CH2_H = 0x0007;   // 称重通道 2 (高16位)
        private const ushort REG_WEIGHT_CH2_L = 0x0008;   // 称重通道 2 (低16位)
        private const ushort REG_WEIGHT_CH3_H = 0x0009;   // 称重通道 3 (高16位)
        private const ushort REG_WEIGHT_CH3_L = 0x000A;   // 称重通道 3 (低16位)
        private const ushort REG_WEIGHT_CH4_H = 0x000B;   // 称重通道 4 (高16位)
        private const ushort REG_WEIGHT_CH4_L = 0x000C;   // 称重通道 4 (低16位)
        private const ushort REG_FLOW_RATE = 0x000D;      // 瞬时流量
        private const ushort REG_FLOW_TOTAL_HIGH = 0x000E;// 累计流量 (高16位)
        private const ushort REG_FLOW_TOTAL_LOW = 0x000F; // 累计流量 (低16位)
        private const ushort REG_RELAY_DO = 0x0011;       // 继电器输出状态 (DO)
        private const ushort REG_RELAY_DI = 0x0012;       // 继电器输入状态 (DI)
        private const ushort REG_SYSTEM_STATUS = 0x0013;  // 系统状态寄存器

        private readonly ModbusFactory _modbusFactory = new();
        private DispatcherTimer? _pollTimer;      // 轮询定时器
        private SerialPort? _serialPort;          // 串口对象
        private IModbusSerialMaster? _master;     // Modbus 主站对象
        private bool _isPolling;                  // 标志位：是否正在进行一次轮询

        public MainWindow()
        {
            InitializeComponent();
            
            // 初始化时应用深色模式和云母效果
            SourceInitialized += (s, e) =>
            {
                WindowHelper.EnableDarkMode(this);
                WindowHelper.EnableMicaEffect(this);
            };
            
            // 默认波特率
            BaudComboBox.Text = "115200";
            
            // 初始刷新端口列表 (使用异步方法，丢弃返回的 Task)
            _ = RefreshPortsAsync();
        }

        /// <summary>
        /// 当点击 COM 口下拉框时触发，手动触发刷新
        /// </summary>
        private void PortComboBox_PreviewMouseDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            _ = RefreshPortsAsync();
        }

        /// <summary>
        /// 刷新按钮点击事件
        /// </summary>
        private void RefreshPorts_Click(object sender, RoutedEventArgs e)
        {
            _ = RefreshPortsAsync();
        }

        /// <summary>
        /// 异步刷新可用的串口列表
        /// WMI 查询在后台线程执行，避免阻塞 UI
        /// </summary>
        private async Task RefreshPortsAsync()
        {
            // 记录当前选中的端口名
            var oldPortName = (PortComboBox.SelectedItem as PortInfo)?.PortName ?? "";

            // 在后台线程执行耗时的 WMI 查询
            var list = await Task.Run(() =>
            {
                var portList = new List<PortInfo>();
                try
                {
                    // 使用 WMI 查询所有包含 "(COM" 的即插即用设备
                    using var searcher = new ManagementObjectSearcher("SELECT Name FROM Win32_PnPEntity WHERE Name LIKE '%(COM%'");
                    foreach (var item in searcher.Get())
                    {
                        var fullName = item["Name"]?.ToString() ?? "";
                        // 正则匹配获取 COM 口号，例如 "USB Serial Device (COM3)" -> "COM3"
                        var match = Regex.Match(fullName, @"\((COM\d+)\)");
                        if (match.Success)
                        {
                            var portName = match.Groups[1].Value;
                            portList.Add(new PortInfo { PortName = portName, Description = fullName });
                        }
                    }
                }
                catch
                {
                    // 如果 WMI 不可用或出错，忽略
                }

                // 降级方案：合并 SerialPort.GetPortNames 的结果，确保没有遗漏
                var existingNames = new HashSet<string>(portList.Select(x => x.PortName));
                foreach (var p in SerialPort.GetPortNames())
                {
                    if (!existingNames.Contains(p))
                    {
                        portList.Add(new PortInfo { PortName = p, Description = p + " (标准端口)" });
                    }
                }

                return portList.OrderBy(x => x.PortName).ToList();
            });

            // 回到 UI 线程更新数据源
            PortComboBox.ItemsSource = list;

            // 自动选中逻辑
            if (!string.IsNullOrEmpty(oldPortName) && list.Any(x => x.PortName == oldPortName))
            {
                 // 如果之前选中的端口仍然存在，保持选中状态
                 PortComboBox.SelectedItem = list.First(x => x.PortName == oldPortName);
            }
            else if (list.Count > 0)
            {
                // 优先选择包含 "USB" 字样的设备 (通常是我们需要的设备)
                var usbDevice = list.FirstOrDefault(x => x.Description.Contains("USB", StringComparison.OrdinalIgnoreCase));
                
                if (usbDevice != null) PortComboBox.SelectedItem = usbDevice;
                else PortComboBox.SelectedIndex = 0;
            }
        }

        /// <summary>
        /// 点击“连接/断开”按钮的事件处理
        /// </summary>
        private async void ConnectButton_Click(object sender, RoutedEventArgs e)
        {
            if (_serialPort?.IsOpen == true)
            {
                Disconnect();
            }
            else
            {
                await ConnectAsync();
            }
        }

        /// <summary>
        /// 异步连接串口设备
        /// </summary>
        private async Task ConnectAsync()
        {
            // 从 SelectedItem 获取端口名
            var selectedPort = PortComboBox.SelectedItem as PortInfo;
            var portName = selectedPort?.PortName ?? "";
            if (string.IsNullOrEmpty(portName)) return;
            
            if (!int.TryParse(BaudComboBox.Text, out var baud)) baud = 115200;

            try
            {
                UpdateStatus($"正在连接到 {portName}...");
                
                // 配置串口参数：无校验位，8数据位，1停止位
                _serialPort = new SerialPort(portName, baud, Parity.None, 8, StopBits.One)
                {
                    ReadTimeout = 1000,
                    WriteTimeout = 1000
                };
                
                // 注意：通常不建议默认开启 Dtr/Rts，防止某些 Prolific 芯片复位
                // _serialPort.DtrEnable = true; 
                // _serialPort.RtsEnable = true;

                _serialPort.Open();

                // 初始化 Modbus RTU 主站
                var adapter = new SerialPortAdapter(_serialPort);
                _master = _modbusFactory.CreateRtuMaster(adapter);
                _master.Transport.Retries = 0;       // 不重试，超时即失败
                _master.Transport.ReadTimeout = 1000;

                // 更新 UI 状态为“已连接”
                ConnectButton.Content = "断开";
                ConnectButton.Background = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#FF5252")); // 红色
                PortComboBox.IsEnabled = false;
                BaudComboBox.IsEnabled = false;
                PollIntervalBox.IsEnabled = false;
                TogglePollButton.IsEnabled = true;
                
                UpdateStatus($"已连接到 {portName} @ {baud}");

                // 连接成功后立即开始轮询并读取一次
                StartPolling();
                await PollOnceAsync();
            }
            catch (Exception ex)
            {
                Disconnect();
                MessageBox.Show($"连接失败: {ex.Message}", "错误");
            }
        }

        /// <summary>
        /// 断开连接并释放资源
        /// </summary>
        private void Disconnect()
        {
            StopPolling();
            _master?.Dispose();
            _master = null;
            if (_serialPort?.IsOpen == true) _serialPort.Close();
            _serialPort = null;

            // 恢复 UI 状态
            ConnectButton.Content = "连接设备";
            ConnectButton.Background = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#2196F3")); // 蓝色
            PortComboBox.IsEnabled = true;
            BaudComboBox.IsEnabled = true;
            PollIntervalBox.IsEnabled = true;
            TogglePollButton.IsEnabled = false;
            UpdateStatus("已断开");
        }

        /// <summary>
        /// 切换自动轮询状态
        /// </summary>
        private void TogglePoll_Click(object sender, RoutedEventArgs e)
        {
            if (_pollTimer?.IsEnabled == true) StopPolling();
            else StartPolling();
        }

        /// <summary>
        /// 启动定时器进行周期性数据轮询
        /// </summary>
        private void StartPolling()
        {
            // 从 UI 获取轮询间隔，最小 50ms
            if (!int.TryParse(PollIntervalBox.Text, out var interval)) interval = 500;
            if (interval < 50) interval = 50;

            _pollTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(interval) };
            _pollTimer.Tick += async (s, e) => await PollOnceAsync();
            _pollTimer.Start();
            TogglePollButton.Content = "停止轮询";
        }

        /// <summary>
        /// 停止轮询定时器
        /// </summary>
        private void StopPolling()
        {
            _pollTimer?.Stop();
            TogglePollButton.Content = "自动轮询";
        }

        /// <summary>
        /// 执行一次 Modbus 读取操作
        /// </summary>
        private async Task PollOnceAsync()
        {
            if (_master == null || _isPolling) return; // 如果未连接或正在轮询中，则跳过
            _isPolling = true;

            try
            {
                // 异步读取保持寄存器 0x0000 开始的 RegisterCount 个数据
                var regs = await _master.ReadInputRegistersAsync(SlaveId, 0, RegisterCount);
                UpdateUI(regs);
                UpdateStatus("读取成功");
            }
            catch (Exception ex)
            {
                UpdateStatus($"读取失败: {ex.Message}");
            }
            finally
            {
                _isPolling = false;
            }
        }

        /// <summary>
        /// 根据读取到的寄存器数据更新界面 UI
        /// </summary>
        /// <param name="regs">寄存器数组</param>
        private void UpdateUI(ushort[] regs)
        {
            // 内部辅助函数：安全获取寄存器值
            ushort Get(int addr) => addr < regs.Length ? regs[addr] : (ushort)0;
            
            // 1. 更新主卡片数据
            
            // 称重 Ch3 -> 对应寄存器 9, 10
            // 逻辑处理：如果数值 >= 900，单位显示为 kg，否则显示为 g
            int weightValRaw = (int)((Get(REG_WEIGHT_CH3_H) << 16) | Get(REG_WEIGHT_CH3_L));
            if (weightValRaw == -1) // 异常值处理
            {
                MainWeightText.Text = "---";
                MainWeightUnit.Text = "g";
            }
            else if (weightValRaw >= 900)
            {
                // 转换为 kg (除以 1000)
                MainWeightText.Text = (weightValRaw / 1000.0).ToString("F2");
                MainWeightUnit.Text = "kg";
            }
            else
            {
                // 显示克 (g)
                MainWeightText.Text = weightValRaw.ToString();
                MainWeightUnit.Text = "g";
            }

            // 温度 Ch4 -> 寄存器 4，单位 0.1 度
            double tempVal = Get(REG_PT100_CH4) / 10.0;
            MainTempText.Text = tempVal.ToString("F1");

            // 瞬时流量 -> 寄存器 13，单位 0.01 L/min
            double flowVal = Get(REG_FLOW_RATE) / 100.0;
            MainFlowText.Text = flowVal.ToString("F2");

            // 累计流量 -> 寄存器 14, 15，单位 0.001 m³
            uint flowTotal = ((uint)Get(REG_FLOW_TOTAL_HIGH) << 16) | Get(REG_FLOW_TOTAL_LOW);
            TotalFlowText.Text = $"累计: {(flowTotal / 1000.0):F3} m³";

            // 2. 更新继电器状态 - 直接控制 Ellipse 颜色
            ushort doReg = Get(REG_RELAY_DO);
            ushort diReg = Get(REG_RELAY_DI);
            
            RelayDoHex.Text = $"0x{doReg:X4}";
            RelayDiHex.Text = $"0x{diReg:X4}";

            // 更新 16 个输出 (DO) 指示灯
            UpdateRelayDot(DO0, doReg, 0);
            UpdateRelayDot(DO1, doReg, 1);
            UpdateRelayDot(DO2, doReg, 2);
            UpdateRelayDot(DO3, doReg, 3);
            UpdateRelayDot(DO4, doReg, 4);
            UpdateRelayDot(DO5, doReg, 5);
            UpdateRelayDot(DO6, doReg, 6);
            UpdateRelayDot(DO7, doReg, 7);
            UpdateRelayDot(DO8, doReg, 8);
            UpdateRelayDot(DO9, doReg, 9);
            UpdateRelayDot(DO10, doReg, 10);
            UpdateRelayDot(DO11, doReg, 11);
            UpdateRelayDot(DO12, doReg, 12);
            UpdateRelayDot(DO13, doReg, 13);
            UpdateRelayDot(DO14, doReg, 14);
            UpdateRelayDot(DO15, doReg, 15);

            // 更新 16 个输入 (DI) 指示灯
            UpdateRelayDot(DI0, diReg, 0);
            UpdateRelayDot(DI1, diReg, 1);
            UpdateRelayDot(DI2, diReg, 2);
            UpdateRelayDot(DI3, diReg, 3);
            UpdateRelayDot(DI4, diReg, 4);
            UpdateRelayDot(DI5, diReg, 5);
            UpdateRelayDot(DI6, diReg, 6);
            UpdateRelayDot(DI7, diReg, 7);
            UpdateRelayDot(DI8, diReg, 8);
            UpdateRelayDot(DI9, diReg, 9);
            UpdateRelayDot(DI10, diReg, 10);
            UpdateRelayDot(DI11, diReg, 11);
            UpdateRelayDot(DI12, diReg, 12);
            UpdateRelayDot(DI13, diReg, 13);
            UpdateRelayDot(DI14, diReg, 14);
            UpdateRelayDot(DI15, diReg, 15);

            // 3. 更新详细日志文本
            var sb = new StringBuilder();
            sb.AppendLine($"[更新时间: {DateTime.Now:HH:mm:ss}]");
            sb.AppendLine("--- 其他通道数据 ---");
            sb.AppendLine($"温度 Ch1: {Get(REG_PT100_CH1)/10.0:F1} °C");
            sb.AppendLine($"温度 Ch2: {Get(REG_PT100_CH2)/10.0:F1} °C");
            sb.AppendLine($"温度 Ch3: {Get(REG_PT100_CH3)/10.0:F1} °C");
            
            sb.AppendLine($"称重 Ch1: {((int)((Get(REG_WEIGHT_CH1_H)<<16)|Get(REG_WEIGHT_CH1_L)))}");
            sb.AppendLine($"称重 Ch2: {((int)((Get(REG_WEIGHT_CH2_H)<<16)|Get(REG_WEIGHT_CH2_L)))}");
            sb.AppendLine($"称重 Ch4: {((int)((Get(REG_WEIGHT_CH4_H)<<16)|Get(REG_WEIGHT_CH4_L)))}");
            
            sb.AppendLine("--- 状态寄存器 ---");
            sb.AppendLine($"系统状态: 0x{Get(REG_SYSTEM_STATUS):X4}");
            sb.AppendLine($"继电器控制字: 0x{Get(0x0010):X4}"); // 寄存器 16 (0x0010)
            
            DetailLogText.Text = sb.ToString();

            LastUpdateText.Text = $"上次更新: {DateTime.Now:HH:mm:ss}";
        }

        /// <summary>
        /// 更新状态栏信息
        /// </summary>
        private void UpdateStatus(string msg) => StatusText.Text = msg;

        /// <summary>
        /// 更新单个继电器圆点的颜色
        /// </summary>
        /// <param name="dot">指示灯 UI 对象</param>
        /// <param name="regValue">寄存器值</param>
        /// <param name="bitIndex">位索引 (0-15)</param>
        private void UpdateRelayDot(System.Windows.Shapes.Ellipse dot, ushort regValue, int bitIndex)
        {
            // 检查对应位是否为 1
            bool isOn = ((regValue >> bitIndex) & 1) == 1;
            dot.Fill = isOn ? Brushes.LimeGreen : Brushes.Gray;
        }
    }

    /// <summary>
    /// 串口信息辅助类，用于 ListBox 绑定
    /// </summary>
    public class PortInfo
    {
        /// <summary>
        /// 端口号 (如 COM3)，用于实际连接
        /// </summary>
        public string PortName { get; set; } = "";
        
        /// <summary>
        /// 描述信息 (如 "USB Serial Device (COM3)")，用于显示
        /// </summary>
        public string Description { get; set; } = "";

        /// <summary>
        /// 完整名称，用于 XAML 绑定兼容 (别名 Description)
        /// </summary>
        public string FullName => Description;

        /// <summary>
        /// 用于下拉列表显示的描述（去掉末尾的 COM 号）
        /// </summary>
        public string DisplayDescription
        {
            get
            {
                if (string.IsNullOrWhiteSpace(Description)) return "";
                return Regex.Replace(Description, @"\s*\(COM\d+\)", "", RegexOptions.IgnoreCase).Trim();
            }
        }

        public override string ToString()
        {
            return PortName;
        }
    }

    /// <summary>
    /// 布尔值转换器：将 true/false 转换为 绿色/灰色 画刷
    /// </summary>
    public class BooleanToBrushConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            return (bool)value ? Brushes.LimeGreen : Brushes.Gray;
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            throw new NotImplementedException();
        }
    }
}
