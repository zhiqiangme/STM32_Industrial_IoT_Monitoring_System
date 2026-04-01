// SerialOperationGate.cs
// 放串口操作全局互斥控制，避免升级流程和后台探测同时访问串口。

using System.Threading;

namespace Project;

/// <summary>
/// 串口操作全局互斥门。
/// 本工具既会在前台执行升级，也会在后台轮询读取当前运行槽位；
/// 如果两条路径同时打开同一个串口，很容易出现串口被占用、读写互相打断等问题。
/// 因此这里用一个全局信号量把所有串口关键操作串行化。
/// </summary>
internal static class SerialOperationGate
{
    private static readonly SemaphoreSlim Gate = new(1, 1);

    /// <summary>
    /// 在独占串口操作窗口内执行一个无返回值动作。
    /// </summary>
    public static void Run(Action action)
    {
        ArgumentNullException.ThrowIfNull(action);

        Gate.Wait();
        try
        {
            action();
        }
        finally
        {
            Gate.Release();
        }
    }

    /// <summary>
    /// 在独占串口操作窗口内执行一个带返回值动作。
    /// </summary>
    public static T Run<T>(Func<T> action)
    {
        ArgumentNullException.ThrowIfNull(action);

        Gate.Wait();
        try
        {
            return action();
        }
        finally
        {
            Gate.Release();
        }
    }
}
