using System.Diagnostics;
using System.IO.Ports;
using System.Text;
using System.Text.RegularExpressions;
using OTA.Models;

namespace OTA.Core;

/// <summary>
/// 负责发现当前可用串口，并尽量补齐人类可读的设备描述。
/// </summary>
public sealed class PortDiscoveryService
{
    public async Task<IReadOnlyList<PortOption>> GetPortOptionsAsync()
    {
        var portNames = SerialPort.GetPortNames()
            .OrderBy(GetPortSortKey)
            .ThenBy(name => name, StringComparer.OrdinalIgnoreCase)
            .ToList();

        var descriptions = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        try
        {
            var outputLines = await RunPowerShellAsync(
                """
                $pattern = '\((COM\d+)\)\s*$'
                Get-CimInstance Win32_PnPEntity |
                    Where-Object { $_.Name } |
                    ForEach-Object {
                        if ($_.Name -match $pattern) {
                            $portName = $matches[1].ToUpperInvariant()
                            $displayName = ($_.Name -replace "\s*\($portName\)\s*$", '').Trim()
                            "$portName`t$displayName"
                        }
                    }
                """);

            foreach (var line in outputLines)
            {
                var option = ParsePortOption(line);
                if (option is null || string.IsNullOrWhiteSpace(option.DisplayName))
                {
                    continue;
                }

                descriptions[option.PortName] = NormalizePortDisplayName(option.DisplayName);
            }
        }
        catch
        {
            // Device descriptions are optional. Fall back to plain COM names.
        }

        return portNames
            .Select(name =>
            {
                var portName = name.ToUpperInvariant();
                descriptions.TryGetValue(portName, out var displayName);
                return new PortOption(portName, displayName ?? string.Empty);
            })
            .Where(option => !option.IsBluetooth)
            .ToList();
    }

    private static async Task<IReadOnlyList<string>> RunPowerShellAsync(string script)
    {
        var wrappedScript = $$"""
            $ProgressPreference = 'SilentlyContinue'
            $ErrorActionPreference = 'Stop'
            try {
            {{script}}
            }
            catch {
                [Console]::Error.WriteLine($_.Exception.Message)
                exit 1
            }
            """;

        var encodedCommand = Convert.ToBase64String(Encoding.Unicode.GetBytes(wrappedScript));
        var startInfo = new ProcessStartInfo
        {
            FileName = "powershell.exe",
            Arguments = $"-NoProfile -NonInteractive -OutputFormat Text -ExecutionPolicy Bypass -EncodedCommand {encodedCommand}",
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };

        using var process = new Process { StartInfo = startInfo };
        var outputLines = new List<string>();
        var errorLines = new List<string>();

        process.OutputDataReceived += (_, args) =>
        {
            if (!string.IsNullOrWhiteSpace(args.Data))
            {
                outputLines.Add(args.Data);
            }
        };

        process.ErrorDataReceived += (_, args) =>
        {
            if (!string.IsNullOrWhiteSpace(args.Data))
            {
                errorLines.Add(args.Data);
            }
        };

        if (!process.Start())
        {
            throw new InvalidOperationException("PowerShell 进程启动失败。");
        }

        process.BeginOutputReadLine();
        process.BeginErrorReadLine();
        await process.WaitForExitAsync();

        if (process.ExitCode != 0)
        {
            var detail = errorLines.Count > 0 ? string.Join(Environment.NewLine, errorLines) : $"退出码 {process.ExitCode}";
            throw new InvalidOperationException(detail);
        }

        return outputLines;
    }

    private static PortOption? ParsePortOption(string line)
    {
        if (string.IsNullOrWhiteSpace(line))
        {
            return null;
        }

        var parts = line.Split('\t', 2, StringSplitOptions.TrimEntries);
        if (parts.Length == 0 || string.IsNullOrWhiteSpace(parts[0]))
        {
            return null;
        }

        var portName = parts[0].ToUpperInvariant();
        var displayName = parts.Length > 1 && !string.IsNullOrWhiteSpace(parts[1]) ? parts[1] : portName;
        return new PortOption(portName, displayName);
    }

    private static string NormalizePortDisplayName(string displayName)
    {
        if (string.IsNullOrWhiteSpace(displayName))
        {
            return string.Empty;
        }

        var normalized = displayName.Trim();
        if (normalized.Contains("USB", StringComparison.OrdinalIgnoreCase) &&
            (normalized.Contains("SERIAL", StringComparison.OrdinalIgnoreCase) ||
             normalized.Contains("串行", StringComparison.OrdinalIgnoreCase) ||
             normalized.Contains("CH340", StringComparison.OrdinalIgnoreCase)))
        {
            return "USB串行设备";
        }

        return normalized;
    }

    private static int GetPortSortKey(string? portName)
    {
        if (string.IsNullOrWhiteSpace(portName))
        {
            return int.MaxValue;
        }

        var match = Regex.Match(portName, @"COM(\d+)", RegexOptions.IgnoreCase);
        return match.Success && int.TryParse(match.Groups[1].Value, out var portNumber)
            ? portNumber
            : int.MaxValue;
    }
}
