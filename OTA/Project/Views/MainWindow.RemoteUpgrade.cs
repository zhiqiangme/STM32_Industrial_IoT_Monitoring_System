// MainWindow.RemoteUpgrade.cs
// 放主窗口到远程升级模块的桥接逻辑，负责把现有串口/本地刷写能力暴露给远程工作流。

using System.IO;
using System.Windows;

namespace Project;

public partial class MainWindow : Window
{
    /// <summary>
    /// 初始化远程升级模块。
    /// </summary>
    private void InitializeRemoteUpgradeModule()
    {
        RemoteUpgradeViewControl.Initialize(new MainWindowRemoteUpgradeDeviceBridge(this));
    }

    private sealed class MainWindowRemoteUpgradeDeviceBridge : IRemoteUpgradeDeviceBridge
    {
        private readonly MainWindow _owner;

        public MainWindowRemoteUpgradeDeviceBridge(MainWindow owner)
        {
            _owner = owner;
        }

        public async Task<RemoteDeviceContext> GetDeviceContextAsync(bool refreshRunningSlot, CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var serialSettings = await _owner.Dispatcher.InvokeAsync(() =>
            {
                var success = _owner.TryGetLocalSerialSettingsForRead(out var portName, out var baudRate, out var timeoutSeconds);
                return new
                {
                    Success = success,
                    PortName = portName,
                    BaudRate = baudRate,
                    TimeoutSeconds = timeoutSeconds
                };
            });

            if (!serialSettings.Success)
            {
                return new RemoteDeviceContext(
                    false,
                    string.Empty,
                    serialSettings.BaudRate,
                    serialSettings.TimeoutSeconds,
                    FirmwareSlot.Unknown,
                    "请先在本地升级页选择有效串口。");
            }

            FirmwareSlot runningSlot;
            if (refreshRunningSlot)
            {
                runningSlot = await _owner.Dispatcher.InvokeAsync(() =>
                    _owner.RefreshRunningSlotAsync(
                        serialSettings.PortName,
                        serialSettings.BaudRate,
                        serialSettings.TimeoutSeconds,
                        logSuccess: false,
                        logFailure: false,
                        allowAutoSuggestPath: false)).Task.Unwrap();
            }
            else
            {
                runningSlot = await _owner.Dispatcher.InvokeAsync(() => _owner._runningSlot);
            }

            return new RemoteDeviceContext(
                true,
                serialSettings.PortName,
                serialSettings.BaudRate,
                serialSettings.TimeoutSeconds,
                runningSlot,
                null);
        }

        public async Task RunUpgradeAsync(RemotePreparedPackage package, Action<string> log, CancellationToken cancellationToken = default)
        {
            ArgumentNullException.ThrowIfNull(log);
            cancellationToken.ThrowIfCancellationRequested();

            var request = await _owner.Dispatcher.InvokeAsync(async () =>
            {
                if (!_owner.TryReadLocalSerialSettings(out var portName, out var baudRate, out var timeoutSeconds, out var errorMessage))
                {
                    throw new InvalidOperationException(errorMessage);
                }

                await _owner.RefreshRunningSlotAsync(
                    portName,
                    baudRate,
                    timeoutSeconds,
                    logSuccess: false,
                    logFailure: false,
                    allowAutoSuggestPath: false);

                if (string.IsNullOrWhiteSpace(package.FilePath) || !File.Exists(package.FilePath))
                {
                    throw new InvalidOperationException($"远程固件文件不存在：{package.FilePath}");
                }

                if (!_owner.TryReadImageInfo(package.FilePath, out var imageInfo, out var errorMessageForImage, logFailure: false))
                {
                    throw new InvalidOperationException(errorMessageForImage);
                }

                if (_owner._runningSlot != FirmwareSlot.Unknown && imageInfo.DetectedSlot == _owner._runningSlot)
                {
                    var recommendedFile = UpgradeAbSupport.GetRecommendedFileName(_owner._runningSlot);
                    throw new InvalidOperationException(
                        $"设备当前运行槽位为 {_owner._runningSlot.ToDisplayText()}，不能继续发送同槽镜像。请改选 {recommendedFile}。");
                }

                var options = new LocalUpgradeOptions(portName, baudRate, timeoutSeconds, package.FilePath);
                return (Options: options, ImageInfo: imageInfo);
            }).Task.Unwrap();

            await _owner.Dispatcher.InvokeAsync(() => _owner.SetBusyState(true, "执行中"));
            try
            {
                log("准备升级，模式 远程升级。");
                if (_owner._runningSlot == FirmwareSlot.Unknown)
                {
                    log("警告: 未读取到当前运行槽位，将按所选镜像继续升级。");
                }
                else
                {
                    log($"当前运行槽：{_owner._runningSlot.ToDisplayText()}。");
                    log($"推荐镜像：{UpgradeAbSupport.GetRecommendedFileName(_owner._runningSlot)}。");
                }

                log($"所选镜像槽：{request.ImageInfo.DetectedSlot.ToDisplayText()}。");
                log($"镜像复位向量：0x{request.ImageInfo.ResetHandler:X8}。");

                await LocalUpgradeService.RunAsync(request.Options, log);
                log("流程结束。");
                await _owner.Dispatcher.InvokeAsync(() => _owner.StatusTextBlock.Text = "完成");
            }
            catch
            {
                await _owner.Dispatcher.InvokeAsync(() => _owner.StatusTextBlock.Text = "失败");
                throw;
            }
            finally
            {
                await _owner.Dispatcher.InvokeAsync(() => _owner.SetBusyState(false, _owner.StatusTextBlock.Text));
            }
        }
    }
}
