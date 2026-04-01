// RunningSlotService.cs
// 放运行槽位读取流程，负责收口超时和后台执行。

namespace Project;

internal sealed class RunningSlotService
{
    public Task<RunningSlotReadResult> ReadAsync(string portName, int baudRate, double timeoutSeconds, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();

        return Task.Run(() =>
        {
            cancellationToken.ThrowIfCancellationRequested();
            var timeout = TimeSpan.FromSeconds(Math.Clamp(timeoutSeconds, 1d, 10d));
            var success = UpgradeAbSupport.TryReadRunningSlot(portName, baudRate, timeout, out var slot, out var errorMessage);
            return new RunningSlotReadResult(success, slot, errorMessage);
        }, cancellationToken);
    }
}
