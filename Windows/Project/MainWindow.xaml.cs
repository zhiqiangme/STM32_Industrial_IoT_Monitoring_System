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

namespace Project
{
    public partial class MainWindow : Window
    {
        private const byte SlaveId = 10;
        private const ushort RegisterCount = 22;
        
        // Register Addresses
        private const ushort REG_PT100_CH1 = 0x0001;
        private const ushort REG_PT100_CH2 = 0x0002;
        private const ushort REG_PT100_CH3 = 0x0003;
        private const ushort REG_PT100_CH4 = 0x0004;
        private const ushort REG_WEIGHT_CH1_H = 0x0005;
        private const ushort REG_WEIGHT_CH1_L = 0x0006;
        private const ushort REG_WEIGHT_CH2_H = 0x0007;
        private const ushort REG_WEIGHT_CH2_L = 0x0008;
        private const ushort REG_WEIGHT_CH3_H = 0x0009;
        private const ushort REG_WEIGHT_CH3_L = 0x000A;
        private const ushort REG_WEIGHT_CH4_H = 0x000B;
        private const ushort REG_WEIGHT_CH4_L = 0x000C;
        private const ushort REG_FLOW_RATE = 0x000D;
        private const ushort REG_FLOW_TOTAL_HIGH = 0x000E;
        private const ushort REG_FLOW_TOTAL_LOW = 0x000F;
        private const ushort REG_RELAY_DO = 0x0011;
        private const ushort REG_RELAY_DI = 0x0012;
        private const ushort REG_SYSTEM_STATUS = 0x0013;

        private readonly ModbusFactory _modbusFactory = new();
        private DispatcherTimer? _pollTimer;
        private SerialPort? _serialPort;
        private IModbusSerialMaster? _master;
        private bool _isPolling;

        public MainWindow()
        {
            InitializeComponent();
            
            // Apply Dark Mode & Mica
            SourceInitialized += (s, e) =>
            {
                WindowHelper.EnableDarkMode(this);
                WindowHelper.EnableMicaEffect(this);
            };
            
            BaudComboBox.Text = "115200";
            RefreshPorts();
        }

        private void RefreshPorts_Click(object sender, RoutedEventArgs e) => RefreshPorts();

        private void RefreshPorts()
        {
            PortComboBox.Items.Clear();
            var ports = SerialPort.GetPortNames().OrderBy(p => p).ToArray();
            foreach (var p in ports) PortComboBox.Items.Add(p);
            if (ports.Length > 0) PortComboBox.SelectedIndex = 0;
        }

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

        private async Task ConnectAsync()
        {
            var portName = PortComboBox.Text;
            if (string.IsNullOrEmpty(portName)) return;
            
            if (!int.TryParse(BaudComboBox.Text, out var baud)) baud = 115200;

            try
            {
                UpdateStatus($"Connecting to {portName}...");
                
                _serialPort = new SerialPort(portName, baud, Parity.None, 8, StopBits.One)
                {
                    ReadTimeout = 1000,
                    WriteTimeout = 1000
                };
                
                // Do NOT set Dtr/Rts to avoid issues with some cheap prolific adapters
                // _serialPort.DtrEnable = true; 
                // _serialPort.RtsEnable = true;

                _serialPort.Open();

                var adapter = new SerialPortAdapter(_serialPort);
                _master = _modbusFactory.CreateRtuMaster(adapter);
                _master.Transport.Retries = 0;
                _master.Transport.ReadTimeout = 1000;

                ConnectButton.Content = "断开";
                ConnectButton.Background = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#FF5252")); // Red
                PortComboBox.IsEnabled = false;
                BaudComboBox.IsEnabled = false;
                TogglePollButton.IsEnabled = true;
                
                UpdateStatus($"Connected to {portName} @ {baud}");

                StartPolling();
                await PollOnceAsync();
            }
            catch (Exception ex)
            {
                Disconnect();
                MessageBox.Show($"Connection Failed: {ex.Message}", "Error");
            }
        }

        private void Disconnect()
        {
            StopPolling();
            _master?.Dispose();
            _master = null;
            if (_serialPort?.IsOpen == true) _serialPort.Close();
            _serialPort = null;

            ConnectButton.Content = "连接设备";
            ConnectButton.Background = new SolidColorBrush((Color)ColorConverter.ConvertFromString("#2196F3")); // Blue
            PortComboBox.IsEnabled = true;
            BaudComboBox.IsEnabled = true;
            TogglePollButton.IsEnabled = false;
            UpdateStatus("Disconnected");
        }

        private void TogglePoll_Click(object sender, RoutedEventArgs e)
        {
            if (_pollTimer?.IsEnabled == true) StopPolling();
            else StartPolling();
        }

        private void StartPolling()
        {
            _pollTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(1000) };
            _pollTimer.Tick += async (s, e) => await PollOnceAsync();
            _pollTimer.Start();
            TogglePollButton.Content = "停止轮询";
        }

        private void StopPolling()
        {
            _pollTimer?.Stop();
            TogglePollButton.Content = "自动轮询";
        }

        private async Task PollOnceAsync()
        {
            if (_master == null || _isPolling) return;
            _isPolling = true;

            try
            {
                var regs = await _master.ReadInputRegistersAsync(SlaveId, 0, RegisterCount);
                UpdateUI(regs);
                UpdateStatus("Read Success");
            }
            catch (Exception ex)
            {
                UpdateStatus($"Read Failed: {ex.Message}");
            }
            finally
            {
                _isPolling = false;
            }
        }

        private void UpdateUI(ushort[] regs)
        {
            // Helper to get reg value safely
            ushort Get(int addr) => addr < regs.Length ? regs[addr] : (ushort)0;
            
            // 1. Update Main Cards
            // Weight Ch3 -> Reg 9, 10 (unit: grams, convert to kg when >= 900)
            int weightValRaw = (int)((Get(REG_WEIGHT_CH3_H) << 16) | Get(REG_WEIGHT_CH3_L));
            if (weightValRaw == -1)
            {
                MainWeightText.Text = "---";
                MainWeightUnit.Text = "g";
            }
            else if (weightValRaw >= 900)
            {
                // Convert to kg
                MainWeightText.Text = (weightValRaw / 1000.0).ToString("F2");
                MainWeightUnit.Text = "kg";
            }
            else
            {
                // Display in grams
                MainWeightText.Text = weightValRaw.ToString();
                MainWeightUnit.Text = "g";
            }

            // Temp Ch4 -> Reg 4
            double tempVal = Get(REG_PT100_CH4) / 10.0;
            MainTempText.Text = tempVal.ToString("F1");

            // Flow Rate -> Reg 13
            double flowVal = Get(REG_FLOW_RATE) / 100.0;
            MainFlowText.Text = flowVal.ToString("F2");

            // Total Flow -> Reg 14, 15
            uint flowTotal = ((uint)Get(REG_FLOW_TOTAL_HIGH) << 16) | Get(REG_FLOW_TOTAL_LOW);
            TotalFlowText.Text = $"累计: {(flowTotal / 1000.0):F3} m³";

            // 2. Update Relays - direct Ellipse update
            ushort doReg = Get(REG_RELAY_DO);
            ushort diReg = Get(REG_RELAY_DI);
            
            RelayDoHex.Text = $"0x{doReg:X4}";
            RelayDiHex.Text = $"0x{diReg:X4}";

            // Update DO indicators
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

            // Update DI indicators
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

            // 3. Update Detail Log
            var sb = new StringBuilder();
            sb.AppendLine($"[更新时间: {DateTime.Now:HH:mm:ss}]");
            sb.AppendLine("--- 其他通道 ---");
            sb.AppendLine($"Temp Ch1: {Get(REG_PT100_CH1)/10.0:F1} °C");
            sb.AppendLine($"Temp Ch2: {Get(REG_PT100_CH2)/10.0:F1} °C");
            sb.AppendLine($"Temp Ch3: {Get(REG_PT100_CH3)/10.0:F1} °C");
            
            sb.AppendLine($"Weight Ch1: {((int)((Get(REG_WEIGHT_CH1_H)<<16)|Get(REG_WEIGHT_CH1_L)))}");
            sb.AppendLine($"Weight Ch2: {((int)((Get(REG_WEIGHT_CH2_H)<<16)|Get(REG_WEIGHT_CH2_L)))}");
            sb.AppendLine($"Weight Ch4: {((int)((Get(REG_WEIGHT_CH4_H)<<16)|Get(REG_WEIGHT_CH4_L)))}");
            
            sb.AppendLine("--- 状态 ---");
            sb.AppendLine($"System Status: 0x{Get(REG_SYSTEM_STATUS):X4}");
            sb.AppendLine($"Relay Ctrl: 0x{Get(0x0010):X4}"); // REG_RELAY_CTRL
            
            DetailLogText.Text = sb.ToString();

            LastUpdateText.Text = $"上次更新: {DateTime.Now:HH:mm:ss}";
        }

        private void UpdateStatus(string msg) => StatusText.Text = msg;

        private void UpdateRelayDot(System.Windows.Shapes.Ellipse dot, ushort regValue, int bitIndex)
        {
            bool isOn = ((regValue >> bitIndex) & 1) == 1;
            dot.Fill = isOn ? Brushes.LimeGreen : Brushes.Gray;
        }
    }

    // Converter for Relay Dots
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
