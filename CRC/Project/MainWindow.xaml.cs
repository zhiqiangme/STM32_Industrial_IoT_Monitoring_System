using System.Windows;
using System.Windows.Controls;

namespace ModbusFrameTool
{
    public partial class MainWindow : Window
    {
        private enum NumberBase
        {
            Hex,
            Dec
        }

        private bool _suppressAutoGenerate;
        private NumberBase _slaveAddressBase = NumberBase.Dec;
        private NumberBase _functionCodeBase = NumberBase.Dec;
        private NumberBase _registerAddressBase = NumberBase.Hex;
        private NumberBase _dataBase = NumberBase.Hex;

        public MainWindow()
        {
            InitializeComponent();

            SlaveAddressTextBox.Text = "10";
            FunctionCodeTextBox.Text = "6";
            RegisterAddressTextBox.Text = "0030";
            DataTextBox.Text = "A55A";

            RefreshBaseIndicators();
            GenerateAndCopyFrame();
        }

        private void InputTextBox_OnTextChanged(object sender, TextChangedEventArgs e)
        {
            if (_suppressAutoGenerate)
            {
                return;
            }

            GenerateAndCopyFrame();
        }

        private void GenerateButton_OnClick(object sender, RoutedEventArgs e)
        {
            GenerateAndCopyFrame();
        }

        private void FillExampleButton_OnClick(object sender, RoutedEventArgs e)
        {
            _suppressAutoGenerate = true;
            _slaveAddressBase = NumberBase.Dec;
            _functionCodeBase = NumberBase.Dec;
            _registerAddressBase = NumberBase.Hex;
            _dataBase = NumberBase.Hex;

            SlaveAddressTextBox.Text = "10";
            FunctionCodeTextBox.Text = "6";
            RegisterAddressTextBox.Text = "0030";
            DataTextBox.Text = "A55A";
            _suppressAutoGenerate = false;

            RefreshBaseIndicators();
            GenerateAndCopyFrame();
        }

        private void BaseButton_OnClick(object sender, RoutedEventArgs e)
        {
            if (sender is not Button button || button.Tag is not string fieldKey)
            {
                return;
            }

            NumberBase currentBase = GetFieldBase(fieldKey);
            NumberBase targetBase = currentBase == NumberBase.Hex ? NumberBase.Dec : NumberBase.Hex;

            ToggleFieldBase(fieldKey, targetBase);
            GenerateAndCopyFrame();
        }

        private void GenerateAndCopyFrame()
        {
            if (!TryParseByte(SlaveAddressTextBox.Text, _slaveAddressBase, out byte slaveAddress, out string? slaveError))
            {
                ShowValidationError($"从站地址错误：{slaveError}");
                return;
            }

            if (!TryParseByte(FunctionCodeTextBox.Text, _functionCodeBase, out byte functionCode, out string? functionError))
            {
                ShowValidationError($"功能码错误：{functionError}");
                return;
            }

            if (!TryParseUInt16(RegisterAddressTextBox.Text, _registerAddressBase, out ushort registerAddress, out string? registerError))
            {
                ShowValidationError($"寄存器地址错误：{registerError}");
                return;
            }

            if (!TryParseUInt16(DataTextBox.Text, _dataBase, out ushort dataValue, out string? dataError))
            {
                ShowValidationError($"数据错误：{dataError}");
                return;
            }

            byte[] frameWithoutCrc =
            {
                slaveAddress,
                functionCode,
                (byte)(registerAddress >> 8),
                (byte)(registerAddress & 0xFF),
                (byte)(dataValue >> 8),
                (byte)(dataValue & 0xFF)
            };

            ushort crc = ComputeModbusCrc(frameWithoutCrc);
            byte[] frame =
            {
                frameWithoutCrc[0],
                frameWithoutCrc[1],
                frameWithoutCrc[2],
                frameWithoutCrc[3],
                frameWithoutCrc[4],
                frameWithoutCrc[5],
                (byte)(crc & 0xFF),
                (byte)(crc >> 8)
            };

            string rawFrame = string.Join(" ", frame.Select(static b => b.ToString("X2")));

            RawFrameTextBox.Text = rawFrame;
            CrcTextBlock.Text = crc.ToString("X4");
            Clipboard.SetText(rawFrame);

            HeaderStatusText.Text = "已复制到剪贴板";
            CopyStatusTextBlock.Text = "复制成功";
            NextStepTextBlock.Text =
                "下一步：打开有人云平台的“网络调试”，将剪贴板中的原始帧直接粘贴发送。发送后根据返回帧确认写入是否成功。";
        }

        private void ShowValidationError(string message)
        {
            HeaderStatusText.Text = "等待有效输入";
            RawFrameTextBox.Text = string.Empty;
            CrcTextBlock.Text = "--";
            CopyStatusTextBlock.Text = "未复制";
            NextStepTextBlock.Text = message;
        }

        private void ToggleFieldBase(string fieldKey, NumberBase targetBase)
        {
            TextBox textBox = GetFieldTextBox(fieldKey);
            NumberBase currentBase = GetFieldBase(fieldKey);

            if (currentBase == targetBase)
            {
                RefreshBaseIndicators();
                return;
            }

            if (!TryParseFieldValue(textBox.Text, currentBase, GetFieldMaxValue(fieldKey), out uint value, out _))
            {
                SetFieldBase(fieldKey, targetBase);
                textBox.Text = string.Empty;
                RefreshBaseIndicators();
                return;
            }

            _suppressAutoGenerate = true;
            SetFieldBase(fieldKey, targetBase);
            textBox.Text = FormatValue(value, targetBase, GetFieldHexDigits(fieldKey));
            _suppressAutoGenerate = false;

            RefreshBaseIndicators();
        }

        private void RefreshBaseIndicators()
        {
            SlaveAddressBaseButton.Content = _slaveAddressBase == NumberBase.Hex ? "0x" : "0d";
            FunctionCodeBaseButton.Content = _functionCodeBase == NumberBase.Hex ? "0x" : "0d";
            RegisterAddressBaseButton.Content = _registerAddressBase == NumberBase.Hex ? "0x" : "0d";
            DataBaseButton.Content = _dataBase == NumberBase.Hex ? "0x" : "0d";
        }

        private NumberBase GetFieldBase(string fieldKey)
        {
            return fieldKey switch
            {
                "SlaveAddress" => _slaveAddressBase,
                "FunctionCode" => _functionCodeBase,
                "RegisterAddress" => _registerAddressBase,
                "Data" => _dataBase,
                _ => NumberBase.Hex
            };
        }

        private void SetFieldBase(string fieldKey, NumberBase numberBase)
        {
            switch (fieldKey)
            {
                case "SlaveAddress":
                    _slaveAddressBase = numberBase;
                    break;
                case "FunctionCode":
                    _functionCodeBase = numberBase;
                    break;
                case "RegisterAddress":
                    _registerAddressBase = numberBase;
                    break;
                case "Data":
                    _dataBase = numberBase;
                    break;
            }
        }

        private TextBox GetFieldTextBox(string fieldKey)
        {
            return fieldKey switch
            {
                "SlaveAddress" => SlaveAddressTextBox,
                "FunctionCode" => FunctionCodeTextBox,
                "RegisterAddress" => RegisterAddressTextBox,
                "Data" => DataTextBox,
                _ => SlaveAddressTextBox
            };
        }

        private static uint GetFieldMaxValue(string fieldKey)
        {
            return fieldKey switch
            {
                "SlaveAddress" => byte.MaxValue,
                "FunctionCode" => byte.MaxValue,
                "RegisterAddress" => ushort.MaxValue,
                "Data" => ushort.MaxValue,
                _ => ushort.MaxValue
            };
        }

        private static int GetFieldHexDigits(string fieldKey)
        {
            return fieldKey switch
            {
                "SlaveAddress" => 2,
                "FunctionCode" => 2,
                "RegisterAddress" => 4,
                "Data" => 4,
                _ => 4
            };
        }

        private static bool TryParseByte(string input, NumberBase numberBase, out byte value, out string? errorMessage)
        {
            if (!TryParseFieldValue(input, numberBase, byte.MaxValue, out uint parsedValue, out errorMessage))
            {
                value = 0;
                return false;
            }

            value = (byte)parsedValue;
            errorMessage = null;
            return true;
        }

        private static bool TryParseUInt16(string input, NumberBase numberBase, out ushort value, out string? errorMessage)
        {
            if (!TryParseFieldValue(input, numberBase, ushort.MaxValue, out uint parsedValue, out errorMessage))
            {
                value = 0;
                return false;
            }

            value = (ushort)parsedValue;
            errorMessage = null;
            return true;
        }

        private static bool TryParseFieldValue(string? input, NumberBase numberBase, uint maxValue, out uint value, out string? errorMessage)
        {
            value = 0;

            if (string.IsNullOrWhiteSpace(input))
            {
                errorMessage = "请输入有效数值。";
                return false;
            }

            string cleaned = input.Trim().Replace(" ", string.Empty);
            if (cleaned.Length == 0)
            {
                errorMessage = "请输入有效数值。";
                return false;
            }

            if (numberBase == NumberBase.Hex)
            {
                if (cleaned.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                {
                    cleaned = cleaned[2..];
                }

                if (cleaned.Length == 0)
                {
                    errorMessage = "请输入十六进制数值。";
                    return false;
                }

                if (cleaned.Any(static c => !Uri.IsHexDigit(c)))
                {
                    errorMessage = "只能输入十六进制字符 0-9、A-F。";
                    return false;
                }

                value = Convert.ToUInt32(cleaned, 16);
            }
            else
            {
                if (cleaned.StartsWith("0d", StringComparison.OrdinalIgnoreCase))
                {
                    cleaned = cleaned[2..];
                }

                if (cleaned.Any(static c => !char.IsAsciiDigit(c)))
                {
                    errorMessage = "十进制模式下只能输入 0-9。";
                    return false;
                }

                if (!uint.TryParse(cleaned, out value))
                {
                    errorMessage = "十进制数值无效。";
                    return false;
                }
            }

            if (value > maxValue)
            {
                errorMessage = $"数值超出范围，最大允许 {maxValue}。";
                return false;
            }

            errorMessage = null;
            return true;
        }

        private static string FormatValue(uint value, NumberBase numberBase, int hexDigits)
        {
            return numberBase == NumberBase.Hex
                ? value.ToString($"X{hexDigits}")
                : value.ToString();
        }

        private static ushort ComputeModbusCrc(IEnumerable<byte> data)
        {
            ushort crc = 0xFFFF;

            foreach (byte value in data)
            {
                crc ^= value;

                for (int i = 0; i < 8; i++)
                {
                    bool leastBitSet = (crc & 0x0001) != 0;
                    crc >>= 1;

                    if (leastBitSet)
                    {
                        crc ^= 0xA001;
                    }
                }
            }

            return crc;
        }
    }
}
