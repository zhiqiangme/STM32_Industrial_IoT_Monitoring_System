using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Globalization;

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
        private bool _suppressFrameImportSanitize;
        private NumberBase _slaveAddressBase = NumberBase.Dec;
        private NumberBase _functionCodeBase = NumberBase.Hex;
        private NumberBase _registerAddressBase = NumberBase.Hex;
        private NumberBase _dataBase = NumberBase.Dec;
        private readonly List<string> _frameRecycleBin = [];

        public MainWindow()
        {
            InitializeComponent();

            SlaveAddressTextBox.Text = "10";
            FunctionCodeTextBox.Text = "06";
            RegisterAddressTextBox.Text = "0030";
            DataTextBox.Text = "42330";

            RefreshBaseIndicators();
            GenerateAndCopyFrame();
        }

        private void FrameImportTextBox_OnPreviewMouseRightButtonUp(object sender, MouseButtonEventArgs e)
        {
            e.Handled = true;
            ImportFrameFromClipboard();
        }

        private void FrameImportTextBox_OnTextChanged(object sender, TextChangedEventArgs e)
        {
            if (_suppressFrameImportSanitize)
            {
                return;
            }

            string originalText = FrameImportTextBox.Text;
            string sanitizedText = new string(originalText.Where(static c => !char.IsWhiteSpace(c)).ToArray());
            if (sanitizedText == originalText)
            {
                return;
            }

            _suppressFrameImportSanitize = true;
            int caretIndex = sanitizedText.Length;
            FrameImportTextBox.Text = sanitizedText;
            FrameImportTextBox.CaretIndex = caretIndex;
            _suppressFrameImportSanitize = false;
        }

        private void FrameImportTextBox_OnKeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key != Key.Enter)
            {
                return;
            }

            e.Handled = true;
            ImportFrameFromTextBox();
        }

        private void InputTextBox_OnTextChanged(object sender, TextChangedEventArgs e)
        {
            if (_suppressAutoGenerate)
            {
                return;
            }

            if (sender is TextBox textBox)
            {
                NormalizeFieldTextInput(textBox);
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
            _functionCodeBase = NumberBase.Hex;
            _registerAddressBase = NumberBase.Hex;
            _dataBase = NumberBase.Dec;

            SlaveAddressTextBox.Text = "10";
            FunctionCodeTextBox.Text = "06";
            RegisterAddressTextBox.Text = "0030";
            DataTextBox.Text = "42330";
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
            SetFrameImportStatus(null);

            if (!TryGetCurrentFieldValues(out byte slaveAddress, out byte functionCode, out ushort registerAddress, out ushort dataValue, out string? errorMessage))
            {
                ShowValidationError(errorMessage ?? "输入参数无效。");
                return;
            }

            ApplyFrameValues(slaveAddress, functionCode, registerAddress, dataValue, "已复制到剪贴板");
        }

        private void ImportFrameFromClipboard()
        {
            string existingText = FrameImportTextBox.Text.Trim();
            if (!string.IsNullOrWhiteSpace(existingText)
                && (_frameRecycleBin.Count == 0 || !string.Equals(_frameRecycleBin[^1], existingText, StringComparison.OrdinalIgnoreCase)))
            {
                _frameRecycleBin.Add(existingText);
            }

            string clipboardText = Clipboard.ContainsText() ? Clipboard.GetText() : string.Empty;
            FrameImportTextBox.Text = clipboardText;

            if (string.IsNullOrWhiteSpace(clipboardText))
            {
                ShowValidationError("剪贴板为空，无法导入原始帧。");
                return;
            }

            ImportFrameTextCore(clipboardText, "已导入并复制");
        }

        private void ImportFrameFromTextBox()
        {
            string inputText = FrameImportTextBox.Text.Trim();
            if (string.IsNullOrWhiteSpace(inputText))
            {
                ShowValidationError("原始帧输入框为空。");
                return;
            }

            ImportFrameTextCore(inputText, "已复制");
        }

        private void ImportFrameTextCore(string frameText, string successTextWhenNotCorrected)
        {
            if (!TryParseFrameText(
                frameText,
                out byte slaveAddress,
                out byte functionCode,
                out ushort registerAddress,
                out ushort dataValue,
                out bool crcWasPresent,
                out bool crcWasCorrect,
                out string? errorMessage))
            {
                SetFrameImportStatus("无效输入", true);
                ShowValidationError($"原始帧导入失败：{errorMessage}");
                return;
            }

            _suppressAutoGenerate = true;
            SetFieldText("SlaveAddress", slaveAddress);
            SetFieldText("FunctionCode", functionCode);
            SetFieldText("RegisterAddress", registerAddress);
            SetFieldText("Data", dataValue);
            _suppressAutoGenerate = false;

            bool corrected = crcWasPresent && !crcWasCorrect;
            SetFrameImportStatus(corrected ? "已纠错" : null, corrected);

            string headerStatus = corrected ? "已纠正 CRC 并复制" : successTextWhenNotCorrected;

            ApplyFrameValues(slaveAddress, functionCode, registerAddress, dataValue, headerStatus);
        }

        private void RecycleBinButton_OnClick(object sender, RoutedEventArgs e)
        {
            Window recycleWindow = new()
            {
                Title = "回收站",
                Owner = this,
                Width = 720,
                Height = 480,
                MinWidth = 560,
                MinHeight = 360,
                WindowStartupLocation = WindowStartupLocation.CenterOwner,
                Background = System.Windows.Media.Brushes.White
            };

            TextBox contentBox = new()
            {
                Margin = new Thickness(16),
                FontFamily = new System.Windows.Media.FontFamily("Consolas"),
                FontSize = 15,
                IsReadOnly = true,
                TextWrapping = TextWrapping.Wrap,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                HorizontalScrollBarVisibility = ScrollBarVisibility.Auto,
                Text = _frameRecycleBin.Count == 0
                    ? "回收站为空。"
                    : string.Join(
                        Environment.NewLine + Environment.NewLine,
                        _frameRecycleBin
                            .Select((item, index) => $"{index + 1}. {item}"))
            };

            recycleWindow.Content = contentBox;
            recycleWindow.ShowDialog();
        }

        private bool TryGetCurrentFieldValues(out byte slaveAddress, out byte functionCode, out ushort registerAddress, out ushort dataValue, out string? errorMessage)
        {
            slaveAddress = 0;
            functionCode = 0;
            registerAddress = 0;
            dataValue = 0;

            if (!TryParseByte(SlaveAddressTextBox.Text, _slaveAddressBase, out byte parsedSlaveAddress, out string? slaveError))
            {
                errorMessage = $"从站地址错误：{slaveError}";
                return false;
            }

            if (!TryParseByte(FunctionCodeTextBox.Text, _functionCodeBase, out byte parsedFunctionCode, out string? functionError))
            {
                errorMessage = $"功能码错误：{functionError}";
                return false;
            }

            if (!TryParseUInt16(RegisterAddressTextBox.Text, _registerAddressBase, out ushort parsedRegisterAddress, out string? registerError))
            {
                errorMessage = $"寄存器地址错误：{registerError}";
                return false;
            }

            if (!TryParseUInt16(DataTextBox.Text, _dataBase, out ushort parsedDataValue, out string? dataError))
            {
                errorMessage = $"数据错误：{dataError}";
                return false;
            }

            slaveAddress = parsedSlaveAddress;
            functionCode = parsedFunctionCode;
            registerAddress = parsedRegisterAddress;
            dataValue = parsedDataValue;
            errorMessage = null;
            return true;
        }

        private void ApplyFrameValues(byte slaveAddress, byte functionCode, ushort registerAddress, ushort dataValue, string headerStatus)
        {
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
            string clipboardFrame = string.Concat(frame.Select(static b => b.ToString("X2")));

            RawFrameTextBox.Text = rawFrame;
            CrcTextBlock.Text = crc.ToString("X4");
            Clipboard.SetText(clipboardFrame);

            HeaderStatusText.Text = headerStatus;
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

        private void SetFrameImportStatus(string? text, bool isWarning = false)
        {
            if (string.IsNullOrWhiteSpace(text))
            {
                FrameImportStatusTextBlock.Visibility = Visibility.Collapsed;
                FrameImportStatusTextBlock.Text = string.Empty;
                return;
            }

            FrameImportStatusTextBlock.Text = text;
            FrameImportStatusTextBlock.Foreground = isWarning
                ? System.Windows.Media.Brushes.IndianRed
                : new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(0x0F, 0x76, 0x6E));
            FrameImportStatusTextBlock.Visibility = Visibility.Visible;
        }

        private void SetFieldText(string fieldKey, uint value)
        {
            TextBox textBox = GetFieldTextBox(fieldKey);
            NumberBase numberBase = GetFieldBase(fieldKey);
            textBox.Text = FormatValue(value, numberBase, GetFieldHexDigits(fieldKey));
        }

        private static bool TryParseFrameText(
            string input,
            out byte slaveAddress,
            out byte functionCode,
            out ushort registerAddress,
            out ushort dataValue,
            out bool crcWasPresent,
            out bool crcWasCorrect,
            out string? errorMessage)
        {
            slaveAddress = 0;
            functionCode = 0;
            registerAddress = 0;
            dataValue = 0;
            crcWasPresent = false;
            crcWasCorrect = false;

            string cleaned = new string(input
                .Where(static c => !char.IsWhiteSpace(c) && c != '-' && c != ',')
                .ToArray());

            if (cleaned.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
            {
                cleaned = cleaned[2..];
            }

            if (cleaned.Length == 0)
            {
                errorMessage = "原始帧为空。";
                return false;
            }

            if (cleaned.Any(static c => !Uri.IsHexDigit(c)))
            {
                errorMessage = "原始帧中包含非十六进制字符。";
                return false;
            }

            if (cleaned.Length != 12 && cleaned.Length != 16)
            {
                errorMessage = "当前仅支持 6 字节或 8 字节的单寄存器原始帧。";
                return false;
            }

            byte[] bytes = Enumerable.Range(0, cleaned.Length / 2)
                .Select(i => Convert.ToByte(cleaned.Substring(i * 2, 2), 16))
                .ToArray();

            byte[] frameWithoutCrc = bytes.Take(6).ToArray();

            slaveAddress = bytes[0];
            functionCode = bytes[1];
            registerAddress = (ushort)((bytes[2] << 8) | bytes[3]);
            dataValue = (ushort)((bytes[4] << 8) | bytes[5]);

            if (bytes.Length == 8)
            {
                crcWasPresent = true;
                ushort expectedCrc = ComputeModbusCrc(frameWithoutCrc);
                ushort inputCrc = (ushort)(bytes[6] | (bytes[7] << 8));
                crcWasCorrect = expectedCrc == inputCrc;
            }

            errorMessage = null;
            return true;
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
            SlaveAddressBaseButton.Content = _slaveAddressBase == NumberBase.Hex ? "0x" : "DEC";
            FunctionCodeBaseButton.Content = _functionCodeBase == NumberBase.Hex ? "0x" : "DEC";
            RegisterAddressBaseButton.Content = _registerAddressBase == NumberBase.Hex ? "0x" : "DEC";
            DataBaseButton.Content = _dataBase == NumberBase.Hex ? "0x" : "DEC";
        }

        private void NormalizeFieldTextInput(TextBox textBox)
        {
            string fieldKey = GetFieldKey(textBox);
            string originalText = textBox.Text;
            string cleaned = originalText.Trim().Replace(" ", string.Empty);

            if (cleaned.Length == 0)
            {
                if (cleaned != originalText)
                {
                    _suppressAutoGenerate = true;
                    textBox.Text = cleaned;
                    textBox.CaretIndex = cleaned.Length;
                    _suppressAutoGenerate = false;
                }

                return;
            }

            bool shouldSwitchToHex = cleaned.StartsWith("0x", StringComparison.OrdinalIgnoreCase);
            if (shouldSwitchToHex)
            {
                cleaned = cleaned[2..].ToUpperInvariant();
            }

            if (!shouldSwitchToHex && cleaned.StartsWith("DEC", StringComparison.OrdinalIgnoreCase))
            {
                cleaned = cleaned[3..];
            }
            else if (!shouldSwitchToHex && cleaned.StartsWith("0d", StringComparison.OrdinalIgnoreCase))
            {
                cleaned = cleaned[2..];
            }

            bool textChanged = cleaned != originalText;
            bool baseChanged = shouldSwitchToHex && GetFieldBase(fieldKey) != NumberBase.Hex;
            if (!textChanged && !baseChanged)
            {
                return;
            }

            _suppressAutoGenerate = true;
            if (baseChanged)
            {
                SetFieldBase(fieldKey, NumberBase.Hex);
                RefreshBaseIndicators();
            }

            textBox.Text = cleaned;
            textBox.CaretIndex = cleaned.Length;
            _suppressAutoGenerate = false;
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

        private string GetFieldKey(TextBox textBox)
        {
            if (ReferenceEquals(textBox, SlaveAddressTextBox))
            {
                return "SlaveAddress";
            }

            if (ReferenceEquals(textBox, FunctionCodeTextBox))
            {
                return "FunctionCode";
            }

            if (ReferenceEquals(textBox, RegisterAddressTextBox))
            {
                return "RegisterAddress";
            }

            if (ReferenceEquals(textBox, DataTextBox))
            {
                return "Data";
            }

            return "SlaveAddress";
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

                if (!uint.TryParse(cleaned, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out value))
                {
                    errorMessage = "十六进制数值无效或超出范围。";
                    return false;
                }
            }
            else
            {
                if (cleaned.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                {
                    cleaned = cleaned[2..];

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

                    if (!uint.TryParse(cleaned, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out value))
                    {
                        errorMessage = "十六进制数值无效或超出范围。";
                        return false;
                    }
                }
                else
                {
                    if (cleaned.StartsWith("DEC", StringComparison.OrdinalIgnoreCase))
                    {
                        cleaned = cleaned[3..];
                    }
                    else if (cleaned.StartsWith("0d", StringComparison.OrdinalIgnoreCase))
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
                        errorMessage = "十进制数值无效或超出范围。";
                        return false;
                    }
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
