using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using OTA.Core;
using OTA.ViewModels.Messages;

namespace OTA.ViewModels;

/// <summary>
/// 远程维护页面的状态与交互入口。
/// 负责 Modbus 原始帧生成、导入纠错与回收站记录。
/// </summary>
public sealed class RemoteMaintenanceViewModel : ObservableObject
{
    private readonly RemoteMaintenanceService _service;
    private readonly List<string> _frameRecycleBin = [];

    private bool _suppressAutoGenerate;
    private NumberBase _slaveAddressBase = NumberBase.Dec;
    private NumberBase _functionCodeBase = NumberBase.Hex;
    private NumberBase _registerAddressBase = NumberBase.Hex;
    private NumberBase _dataBase = NumberBase.Dec;

    private string _slaveAddressText = "10";
    private string _functionCodeText = "06";
    private string _registerAddressText = "0030";
    private string _dataText = "42330";
    private string _frameImportText = string.Empty;
    private string _frameImportStatusText = string.Empty;
    private bool _isFrameImportWarning;
    private string _headerStatusText = "已复制到剪贴板";
    private string _rawFrameText = string.Empty;
    private string _crcText = "--";
    private string _copyStatusText = "复制成功";
    private string _nextStepText = "下一步：打开有人云平台的“网络调试”，将原始帧直接粘贴发送。";

    public RemoteMaintenanceViewModel(RemoteMaintenanceService service)
    {
        _service = service;

        GenerateCommand = new RelayCommand(GenerateAndCopyFrame);
        FillExampleCommand = new RelayCommand(FillExampleAndGenerate);
        ImportFrameCommand = new RelayCommand(ImportFrameFromTextBox);
        ToggleFieldBaseCommand = new RelayCommand<string>(ToggleFieldBase);

        GenerateAndCopyFrame();
    }

    public event EventHandler<ClipboardTextRequest>? ClipboardTextRequested;

    public string ModeTitle => "STM32 远程维护";

    public string ModeDescription => "输入 Modbus 参数后自动生成原始帧，便于复制到网络调试或云平台维护界面。";

    public string SlaveAddressBaseText => _slaveAddressBase == NumberBase.Hex ? "0x" : "DEC";

    public string FunctionCodeBaseText => _functionCodeBase == NumberBase.Hex ? "0x" : "DEC";

    public string RegisterAddressBaseText => _registerAddressBase == NumberBase.Hex ? "0x" : "DEC";

    public string DataBaseText => _dataBase == NumberBase.Hex ? "0x" : "DEC";

    public string SlaveAddressText
    {
        get => _slaveAddressText;
        set
        {
            var normalized = NormalizeFieldTextInput(nameof(SlaveAddressText), value);
            if (SetProperty(ref _slaveAddressText, normalized))
            {
                TriggerAutoGenerate();
            }
        }
    }

    public string FunctionCodeText
    {
        get => _functionCodeText;
        set
        {
            var normalized = NormalizeFieldTextInput(nameof(FunctionCodeText), value);
            if (SetProperty(ref _functionCodeText, normalized))
            {
                TriggerAutoGenerate();
            }
        }
    }

    public string RegisterAddressText
    {
        get => _registerAddressText;
        set
        {
            var normalized = NormalizeFieldTextInput(nameof(RegisterAddressText), value);
            if (SetProperty(ref _registerAddressText, normalized))
            {
                TriggerAutoGenerate();
            }
        }
    }

    public string DataText
    {
        get => _dataText;
        set
        {
            var normalized = NormalizeFieldTextInput(nameof(DataText), value);
            if (SetProperty(ref _dataText, normalized))
            {
                TriggerAutoGenerate();
            }
        }
    }

    public string FrameImportText
    {
        get => _frameImportText;
        set => SetProperty(ref _frameImportText, _service.SanitizeFrameText(value));
    }

    public string FrameImportStatusText
    {
        get => _frameImportStatusText;
        private set
        {
            if (SetProperty(ref _frameImportStatusText, value))
            {
                OnPropertyChanged(nameof(HasFrameImportStatus));
            }
        }
    }

    public bool HasFrameImportStatus => !string.IsNullOrWhiteSpace(FrameImportStatusText);

    public bool IsFrameImportWarning
    {
        get => _isFrameImportWarning;
        private set => SetProperty(ref _isFrameImportWarning, value);
    }

    public string HeaderStatusText
    {
        get => _headerStatusText;
        private set => SetProperty(ref _headerStatusText, value);
    }

    public string RawFrameText
    {
        get => _rawFrameText;
        private set => SetProperty(ref _rawFrameText, value);
    }

    public string CrcText
    {
        get => _crcText;
        private set => SetProperty(ref _crcText, value);
    }

    public string CopyStatusText
    {
        get => _copyStatusText;
        private set => SetProperty(ref _copyStatusText, value);
    }

    public string NextStepText
    {
        get => _nextStepText;
        private set => SetProperty(ref _nextStepText, value);
    }

    public IRelayCommand GenerateCommand { get; }

    public IRelayCommand FillExampleCommand { get; }

    public IRelayCommand ImportFrameCommand { get; }

    public IRelayCommand<string> ToggleFieldBaseCommand { get; }

    public void ImportFrameFromClipboardText(string? clipboardText)
    {
        var existingText = FrameImportText.Trim();
        if (!string.IsNullOrWhiteSpace(existingText) &&
            (_frameRecycleBin.Count == 0 || !string.Equals(_frameRecycleBin[^1], existingText, StringComparison.OrdinalIgnoreCase)))
        {
            _frameRecycleBin.Add(existingText);
        }

        FrameImportText = clipboardText ?? string.Empty;
        if (string.IsNullOrWhiteSpace(FrameImportText))
        {
            ShowValidationError("剪贴板为空，无法导入原始帧。");
            SetFrameImportStatus("无可导入内容", isWarning: true);
            return;
        }

        ImportFrameTextCore(FrameImportText, "已导入并复制");
    }

    public IReadOnlyList<string> GetRecycleBinItems()
    {
        return _frameRecycleBin.ToArray();
    }

    private void TriggerAutoGenerate()
    {
        if (_suppressAutoGenerate)
        {
            return;
        }

        GenerateAndCopyFrame();
    }

    private void GenerateAndCopyFrame()
    {
        SetFrameImportStatus(null);

        var frameResult = _service.GenerateFrame(
                SlaveAddressText,
                _slaveAddressBase,
                FunctionCodeText,
                _functionCodeBase,
                RegisterAddressText,
                _registerAddressBase,
                DataText,
                _dataBase);
        if (!frameResult.IsSuccess)
        {
            ShowValidationError(frameResult.ErrorMessage);
            return;
        }

        ApplyFrameValues(frameResult.Value, "已复制到剪贴板");
    }

    private void FillExampleAndGenerate()
    {
        _suppressAutoGenerate = true;
        _slaveAddressBase = NumberBase.Dec;
        _functionCodeBase = NumberBase.Hex;
        _registerAddressBase = NumberBase.Hex;
        _dataBase = NumberBase.Dec;

        OnPropertyChanged(nameof(SlaveAddressBaseText));
        OnPropertyChanged(nameof(FunctionCodeBaseText));
        OnPropertyChanged(nameof(RegisterAddressBaseText));
        OnPropertyChanged(nameof(DataBaseText));

        SlaveAddressText = "10";
        FunctionCodeText = "06";
        RegisterAddressText = "0030";
        DataText = "42330";
        _suppressAutoGenerate = false;

        GenerateAndCopyFrame();
    }

    private void ImportFrameFromTextBox()
    {
        if (string.IsNullOrWhiteSpace(FrameImportText))
        {
            ShowValidationError("原始帧输入框为空。");
            SetFrameImportStatus("无效输入", isWarning: true);
            return;
        }

        ImportFrameTextCore(FrameImportText, "已复制");
    }

    private void ImportFrameTextCore(string frameText, string successTextWhenNotCorrected)
    {
        var importResult = _service.ImportFrame(frameText);
        if (!importResult.IsSuccess)
        {
            SetFrameImportStatus("无效输入", isWarning: true);
            ShowValidationError($"原始帧导入失败：{importResult.ErrorMessage}");
            return;
        }

        var result = importResult.Value;
        _suppressAutoGenerate = true;
        SetFieldText(nameof(SlaveAddressText), result.Frame.SlaveAddress);
        SetFieldText(nameof(FunctionCodeText), result.Frame.FunctionCode);
        SetFieldText(nameof(RegisterAddressText), result.Frame.RegisterAddress);
        SetFieldText(nameof(DataText), result.Frame.DataValue);
        _suppressAutoGenerate = false;

        var corrected = !result.CrcMatchedOriginal;
        SetFrameImportStatus(corrected ? "纠错成功" : "复制成功");
        ApplyFrameValues(result.Frame, corrected ? "已纠错并复制" : successTextWhenNotCorrected);
    }

    private void ToggleFieldBase(string? fieldKey)
    {
        if (string.IsNullOrWhiteSpace(fieldKey))
        {
            return;
        }

        var currentBase = GetFieldBase(fieldKey);
        var targetBase = currentBase == NumberBase.Hex ? NumberBase.Dec : NumberBase.Hex;
        var currentText = GetFieldText(fieldKey);

        if (!TryParseFieldValue(fieldKey, currentText, currentBase, out var value))
        {
            _suppressAutoGenerate = true;
            SetFieldBase(fieldKey, targetBase);
            SetFieldTextRaw(fieldKey, string.Empty);
            _suppressAutoGenerate = false;
            NotifyBaseTextChanged(fieldKey);
            GenerateAndCopyFrame();
            return;
        }

        _suppressAutoGenerate = true;
        SetFieldBase(fieldKey, targetBase);
        SetFieldTextRaw(fieldKey, _service.FormatValue(value, targetBase, GetFieldHexDigits(fieldKey)));
        _suppressAutoGenerate = false;
        NotifyBaseTextChanged(fieldKey);
        GenerateAndCopyFrame();
    }

    private bool TryParseFieldValue(string fieldKey, string input, NumberBase numberBase, out uint value)
    {
        value = 0;

        switch (fieldKey)
        {
            case nameof(SlaveAddressText):
            case nameof(FunctionCodeText):
            {
                var parseResult = _service.ParseByte(input, numberBase);
                if (!parseResult.IsSuccess)
                {
                    return false;
                }

                value = parseResult.Value;
                return true;
            }
            case nameof(RegisterAddressText):
            case nameof(DataText):
            {
                var parseResult = _service.ParseUInt16(input, numberBase);
                if (!parseResult.IsSuccess)
                {
                    return false;
                }

                value = parseResult.Value;
                return true;
            }
            default:
                return false;
        }
    }

    private void ApplyFrameValues(RemoteMaintenanceFrameResult result, string headerStatus)
    {
        RawFrameText = result.RawFrame;
        CrcText = result.Crc.ToString("X4");
        HeaderStatusText = headerStatus;
        CopyStatusText = "复制成功";
        NextStepText = "下一步：打开有人云平台的“网络调试”，将原始帧直接粘贴发送。发送后根据返回帧确认写入是否成功。";
        ClipboardTextRequested?.Invoke(this, new ClipboardTextRequest(result.ClipboardFrame));
    }

    private void ShowValidationError(string message)
    {
        HeaderStatusText = "等待有效输入";
        RawFrameText = string.Empty;
        CrcText = "--";
        CopyStatusText = "未复制";
        NextStepText = message;
    }

    private void SetFrameImportStatus(string? text, bool isWarning = false)
    {
        FrameImportStatusText = text ?? string.Empty;
        IsFrameImportWarning = isWarning;
    }

    private string NormalizeFieldTextInput(string fieldKey, string? value)
    {
        var cleaned = (value ?? string.Empty).Trim().Replace(" ", string.Empty, StringComparison.Ordinal);
        if (cleaned.Length == 0)
        {
            return cleaned;
        }

        var shouldSwitchToHex = cleaned.StartsWith("0x", StringComparison.OrdinalIgnoreCase);
        if (shouldSwitchToHex)
        {
            cleaned = cleaned[2..].ToUpperInvariant();
        }
        else if (cleaned.StartsWith("DEC", StringComparison.OrdinalIgnoreCase))
        {
            cleaned = cleaned[3..];
        }
        else if (cleaned.StartsWith("0d", StringComparison.OrdinalIgnoreCase))
        {
            cleaned = cleaned[2..];
        }

        if (shouldSwitchToHex && GetFieldBase(fieldKey) != NumberBase.Hex)
        {
            SetFieldBase(fieldKey, NumberBase.Hex);
            NotifyBaseTextChanged(fieldKey);
        }

        return cleaned;
    }

    private NumberBase GetFieldBase(string fieldKey)
    {
        return fieldKey switch
        {
            nameof(SlaveAddressText) => _slaveAddressBase,
            nameof(FunctionCodeText) => _functionCodeBase,
            nameof(RegisterAddressText) => _registerAddressBase,
            nameof(DataText) => _dataBase,
            _ => NumberBase.Hex
        };
    }

    private void SetFieldBase(string fieldKey, NumberBase numberBase)
    {
        switch (fieldKey)
        {
            case nameof(SlaveAddressText):
                _slaveAddressBase = numberBase;
                break;
            case nameof(FunctionCodeText):
                _functionCodeBase = numberBase;
                break;
            case nameof(RegisterAddressText):
                _registerAddressBase = numberBase;
                break;
            case nameof(DataText):
                _dataBase = numberBase;
                break;
        }
    }

    private void NotifyBaseTextChanged(string fieldKey)
    {
        switch (fieldKey)
        {
            case nameof(SlaveAddressText):
                OnPropertyChanged(nameof(SlaveAddressBaseText));
                break;
            case nameof(FunctionCodeText):
                OnPropertyChanged(nameof(FunctionCodeBaseText));
                break;
            case nameof(RegisterAddressText):
                OnPropertyChanged(nameof(RegisterAddressBaseText));
                break;
            case nameof(DataText):
                OnPropertyChanged(nameof(DataBaseText));
                break;
        }
    }

    private string GetFieldText(string fieldKey)
    {
        return fieldKey switch
        {
            nameof(SlaveAddressText) => SlaveAddressText,
            nameof(FunctionCodeText) => FunctionCodeText,
            nameof(RegisterAddressText) => RegisterAddressText,
            nameof(DataText) => DataText,
            _ => string.Empty
        };
    }

    private void SetFieldText(string fieldKey, uint value)
    {
        SetFieldTextRaw(fieldKey, _service.FormatValue(value, GetFieldBase(fieldKey), GetFieldHexDigits(fieldKey)));
    }

    private void SetFieldTextRaw(string fieldKey, string value)
    {
        switch (fieldKey)
        {
            case nameof(SlaveAddressText):
                SlaveAddressText = value;
                break;
            case nameof(FunctionCodeText):
                FunctionCodeText = value;
                break;
            case nameof(RegisterAddressText):
                RegisterAddressText = value;
                break;
            case nameof(DataText):
                DataText = value;
                break;
        }
    }

    private static int GetFieldHexDigits(string fieldKey)
    {
        return fieldKey switch
        {
            nameof(SlaveAddressText) => 2,
            nameof(FunctionCodeText) => 2,
            nameof(RegisterAddressText) => 4,
            nameof(DataText) => 4,
            _ => 4
        };
    }
}
