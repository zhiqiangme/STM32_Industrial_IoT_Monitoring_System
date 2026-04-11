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

    /// <summary>
    /// 使用默认偏好文件路径初始化服务。
    /// </summary>
    public AppPreferencesService()
        : this(null)
    {
    }

    /// <summary>
    /// 使用指定偏好文件路径初始化服务，便于测试或自定义存储位置。
    /// </summary>
    public AppPreferencesService(string? filePath)
    {
        if (string.IsNullOrWhiteSpace(filePath))
        {
            var appDataDirectory = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "STM32_Mill",
                "OTA_32");

            _filePath = Path.Combine(appDataDirectory, "preferences.json");
        }
        else
        {
            _filePath = filePath;
        }

        _preferences = LoadFromDisk();
    }

    public string FilePath => _filePath;

    /// <summary>
    /// 读取本地升级页面的偏好副本。
    /// </summary>
    public LocalUpgradePreferences GetLocalUpgradePreferences()
    {
        lock (_sync)
        {
            return Clone(_preferences.LocalUpgrade);
        }
    }

    /// <summary>
    /// 读取远程升级页面的偏好副本。
    /// </summary>
    public RemoteUpgradePreferences GetRemoteUpgradePreferences()
    {
        lock (_sync)
        {
            return Clone(_preferences.RemoteUpgrade);
        }
    }

    /// <summary>
    /// 读取主窗口显示状态偏好副本。
    /// </summary>
    public WindowPreferences GetWindowPreferences()
    {
        lock (_sync)
        {
            return Clone(_preferences.Window);
        }
    }

    /// <summary>
    /// 保存本地升级页面最近使用的固件路径、串口和波特率。
    /// </summary>
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

    /// <summary>
    /// 保存远程升级页面最近使用的固件路径、串口和波特率。
    /// </summary>
    public void SaveRemoteUpgradePreferences(string? lastFirmwarePath, string? lastPortName, string? baudRateText)
    {
        lock (_sync)
        {
            _preferences.RemoteUpgrade = new RemoteUpgradePreferences
            {
                LastFirmwarePath = (lastFirmwarePath ?? string.Empty).Trim(),
                LastPortName = (lastPortName ?? string.Empty).Trim(),
                BaudRateText = (baudRateText ?? string.Empty).Trim()
            };

            SaveToDisk();
        }
    }

    /// <summary>
    /// 保存主窗口位置、尺寸和最大化状态。
    /// </summary>
    public void SaveWindowPreferences(WindowPreferences windowPreferences)
    {
        ArgumentNullException.ThrowIfNull(windowPreferences);

        lock (_sync)
        {
            _preferences.Window = Clone(windowPreferences);
            SaveToDisk();
        }
    }

    /// <summary>
    /// 从磁盘加载偏好文件，失败时回退到默认配置。
    /// </summary>
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

    /// <summary>
    /// 将当前偏好安全写回磁盘，写入失败时不影响主流程。
    /// </summary>
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

    /// <summary>
    /// 把反序列化结果补齐为完整偏好对象，避免空引用。
    /// </summary>
    private static AppPreferences Normalize(AppPreferences? preferences)
    {
        return new AppPreferences
        {
            LocalUpgrade = preferences?.LocalUpgrade is null
                ? new LocalUpgradePreferences()
                : Clone(preferences.LocalUpgrade),
            RemoteUpgrade = preferences?.RemoteUpgrade is null
                ? new RemoteUpgradePreferences()
                : Clone(preferences.RemoteUpgrade),
            Window = preferences?.Window is null
                ? new WindowPreferences()
                : Clone(preferences.Window)
        };
    }

    /// <summary>
    /// 复制本地升级偏好，避免外部直接修改内部状态。
    /// </summary>
    private static LocalUpgradePreferences Clone(LocalUpgradePreferences preferences)
    {
        return new LocalUpgradePreferences
        {
            LastFirmwarePath = preferences.LastFirmwarePath ?? string.Empty,
            LastPortName = preferences.LastPortName ?? string.Empty,
            BaudRateText = preferences.BaudRateText ?? string.Empty
        };
    }

    /// <summary>
    /// 复制远程升级偏好，避免外部直接修改内部状态。
    /// </summary>
    private static RemoteUpgradePreferences Clone(RemoteUpgradePreferences preferences)
    {
        return new RemoteUpgradePreferences
        {
            LastFirmwarePath = preferences.LastFirmwarePath ?? string.Empty,
            LastPortName = preferences.LastPortName ?? string.Empty,
            BaudRateText = preferences.BaudRateText ?? string.Empty
        };
    }

    /// <summary>
    /// 复制窗口偏好，避免外部直接修改内部状态。
    /// </summary>
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
