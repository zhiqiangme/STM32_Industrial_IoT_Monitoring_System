using System.Text.Json;
using OTA.Models;

namespace OTA.Core;

/// <summary>
/// 使用 LocalAppData 下的 JSON 文件持久化用户偏好。
/// </summary>
public sealed class AppPreferencesService
{
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true
    };

    private readonly object _sync = new();
    private readonly string _filePath;
    private AppPreferences _preferences;

    public AppPreferencesService()
    {
        var appDataDirectory = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "STM32_Mill",
            "OTA_32");

        _filePath = Path.Combine(appDataDirectory, "preferences.json");
        _preferences = LoadFromDisk();
    }

    public string FilePath => _filePath;

    public LocalUpgradePreferences GetLocalUpgradePreferences()
    {
        lock (_sync)
        {
            return Clone(_preferences.LocalUpgrade);
        }
    }

    public WindowPreferences GetWindowPreferences()
    {
        lock (_sync)
        {
            return Clone(_preferences.Window);
        }
    }

    public void SaveLocalUpgradePreferences(string? lastFirmwarePath, string? lastPortName, string? baudRateText)
    {
        lock (_sync)
        {
            _preferences.LocalUpgrade = new LocalUpgradePreferences
            {
                LastFirmwarePath = (lastFirmwarePath ?? string.Empty).Trim(),
                LastPortName = (lastPortName ?? string.Empty).Trim(),
                BaudRateText = (baudRateText ?? string.Empty).Trim()
            };

            SaveToDisk();
        }
    }

    public void SaveWindowPreferences(WindowPreferences windowPreferences)
    {
        ArgumentNullException.ThrowIfNull(windowPreferences);

        lock (_sync)
        {
            _preferences.Window = Clone(windowPreferences);
            SaveToDisk();
        }
    }

    private AppPreferences LoadFromDisk()
    {
        try
        {
            if (!File.Exists(_filePath))
            {
                return new AppPreferences();
            }

            var json = File.ReadAllText(_filePath);
            var preferences = JsonSerializer.Deserialize<AppPreferences>(json, JsonOptions);
            return Normalize(preferences);
        }
        catch
        {
            return new AppPreferences();
        }
    }

    private void SaveToDisk()
    {
        try
        {
            var directory = Path.GetDirectoryName(_filePath);
            if (!string.IsNullOrWhiteSpace(directory))
            {
                Directory.CreateDirectory(directory);
            }

            var tempFilePath = _filePath + ".tmp";
            var json = JsonSerializer.Serialize(_preferences, JsonOptions);
            File.WriteAllText(tempFilePath, json);
            File.Move(tempFilePath, _filePath, overwrite: true);
        }
        catch
        {
            // 偏好写入失败不影响主流程。
        }
    }

    private static AppPreferences Normalize(AppPreferences? preferences)
    {
        return new AppPreferences
        {
            LocalUpgrade = preferences?.LocalUpgrade is null
                ? new LocalUpgradePreferences()
                : Clone(preferences.LocalUpgrade),
            Window = preferences?.Window is null
                ? new WindowPreferences()
                : Clone(preferences.Window)
        };
    }

    private static LocalUpgradePreferences Clone(LocalUpgradePreferences preferences)
    {
        return new LocalUpgradePreferences
        {
            LastFirmwarePath = preferences.LastFirmwarePath ?? string.Empty,
            LastPortName = preferences.LastPortName ?? string.Empty,
            BaudRateText = preferences.BaudRateText ?? string.Empty
        };
    }

    private static WindowPreferences Clone(WindowPreferences preferences)
    {
        return new WindowPreferences
        {
            Left = preferences.Left,
            Top = preferences.Top,
            Width = preferences.Width,
            Height = preferences.Height,
            IsMaximized = preferences.IsMaximized
        };
    }
}
