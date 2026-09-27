using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;

namespace DesktopTodo.Services;

// Keeps the widget directly above Explorer's desktop, without parenting the WPF
// window to Explorer or leaving it above ordinary application windows.
internal sealed class DesktopWindowService : IDisposable
{
    private const uint EventSystemForeground = 0x0003;
    private const uint WinEventOutOfContext = 0x0000;
    private const uint WinEventSkipOwnProcess = 0x0002;
    private const uint GwHwndPrev = 3;
    private const uint GaRoot = 2;
    private const uint SwpNoSize = 0x0001;
    private const uint SwpNoMove = 0x0002;
    private const uint SwpNoActivate = 0x0010;
    private const uint SwpShowWindow = 0x0040;
    private const uint SwpNoOwnerZOrder = 0x0200;
    private static readonly IntPtr HwndTopmost = new(-1);
    private static readonly IntPtr HwndNotTopmost = new(-2);
    private const uint PlacementFlags =
        SwpNoSize | SwpNoMove | SwpNoActivate | SwpShowWindow | SwpNoOwnerZOrder;

    private readonly Window _window;
    private readonly IntPtr _handle;
    private readonly WinEventCallback _callback;
    private readonly DispatcherTimer _settleTimer;
    private IntPtr _hook;
    private bool _disposed;

    public DesktopWindowService(Window window)
    {
        _window = window;
        _handle = new WindowInteropHelper(window).Handle;
        _callback = OnForegroundChanged;
        _settleTimer = new DispatcherTimer(DispatcherPriority.Background, window.Dispatcher)
        {
            Interval = TimeSpan.FromMilliseconds(160)
        };
        _settleTimer.Tick += OnSettleTimerTick;

        // Out-of-context events are delivered on the WPF UI thread.
        _hook = SetWinEventHook(
            EventSystemForeground,
            EventSystemForeground,
            IntPtr.Zero,
            _callback,
            0,
            0,
            WinEventOutOfContext | WinEventSkipOwnProcess);
    }

    public void Arrange(bool allowOwnForeground = false)
    {
        if (_disposed || _handle == IntPtr.Zero)
        {
            return;
        }

        var shell = GetShellWindow();
        if (shell == IntPtr.Zero)
        {
            return;
        }

        GetWindowThreadProcessId(shell, out var shellProcessId);
        var foreground = GetForegroundWindow();
        if (!allowOwnForeground && foreground != IntPtr.Zero)
        {
            GetWindowThreadProcessId(foreground, out var foregroundProcessId);
            if (foregroundProcessId == (uint)Environment.ProcessId)
            {
                return;
            }
        }

        var desktop = FindDesktopHost(shell, shellProcessId);
        var desktopIsForeground = IsDesktopForeground(foreground, shell, desktop, shellProcessId);
        // Show Desktop can put Explorer's desktop above ordinary windows. Raise
        // the widget only for that state; lower it again when an app is selected.
        if (_window.WindowState == WindowState.Minimized)
        {
            _window.WindowState = WindowState.Normal;
        }

        if (desktopIsForeground)
        {
            SetWindowPos(_handle, HwndTopmost, 0, 0, 0, 0, PlacementFlags);
            return;
        }

        SetWindowPos(_handle, HwndNotTopmost, 0, 0, 0, 0, PlacementFlags);
        var previous = GetWindow(desktop, GwHwndPrev);
        if (previous != IntPtr.Zero && previous != _handle)
        {
            SetWindowPos(_handle, previous, 0, 0, 0, 0, PlacementFlags);
        }
    }

    private void OnForegroundChanged(
        IntPtr hook, uint eventType, IntPtr window, int objectId,
        int childId, uint threadId, uint eventTime)
    {
        if (_disposed || _window.Dispatcher.HasShutdownStarted)
        {
            return;
        }

        _window.Dispatcher.BeginInvoke(DispatcherPriority.Background, new Action(() =>
        {
            if (_disposed)
            {
                return;
            }

            Arrange();
            var shell = GetShellWindow();
            if (shell == IntPtr.Zero)
            {
                return;
            }

            GetWindowThreadProcessId(shell, out var shellProcessId);
            if (IsDesktopForeground(GetForegroundWindow(), shell,
                    FindDesktopHost(shell, shellProcessId), shellProcessId))
            {
                // Explorer may finish changing its Z order just after the event.
                _settleTimer.Stop();
                _settleTimer.Start();
            }
        }));
    }

    private void OnSettleTimerTick(object? sender, EventArgs e)
    {
        _settleTimer.Stop();
        Arrange();
    }

    private static IntPtr FindDesktopHost(IntPtr shell, uint shellProcessId)
    {
        if (FindWindowEx(shell, IntPtr.Zero, "SHELLDLL_DefView", null) != IntPtr.Zero)
        {
            return shell;
        }

        var host = IntPtr.Zero;
        EnumWindows((candidate, _) =>
        {
            GetWindowThreadProcessId(candidate, out var processId);
            if (processId == shellProcessId &&
                FindWindowEx(candidate, IntPtr.Zero, "SHELLDLL_DefView", null) != IntPtr.Zero)
            {
                host = candidate;
                return false;
            }

            return true;
        }, IntPtr.Zero);

        return host != IntPtr.Zero ? host : shell;
    }

    private static bool IsDesktopForeground(
        IntPtr foreground, IntPtr shell, IntPtr desktop, uint shellProcessId)
    {
        if (foreground == IntPtr.Zero)
        {
            return false;
        }

        var root = GetAncestor(foreground, GaRoot);
        if (root == shell || root == desktop)
        {
            return true;
        }

        GetWindowThreadProcessId(root, out var processId);
        if (processId != shellProcessId)
        {
            return false;
        }

        var className = new StringBuilder(32);
        GetClassName(root, className, className.Capacity);
        return className.ToString() is "WorkerW" or "Progman";
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        _settleTimer.Stop();
        _settleTimer.Tick -= OnSettleTimerTick;
        if (_hook != IntPtr.Zero)
        {
            UnhookWinEvent(_hook);
            _hook = IntPtr.Zero;
        }
    }

    private delegate void WinEventCallback(
        IntPtr hook, uint eventType, IntPtr window, int objectId,
        int childId, uint threadId, uint eventTime);

    private delegate bool EnumWindowsCallback(IntPtr window, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern IntPtr SetWinEventHook(
        uint eventMin, uint eventMax, IntPtr module, WinEventCallback callback,
        uint processId, uint threadId, uint flags);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool UnhookWinEvent(IntPtr hook);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern IntPtr GetShellWindow();

    [DllImport("user32.dll")]
    private static extern IntPtr GetForegroundWindow();

    [DllImport("user32.dll")]
    private static extern IntPtr GetAncestor(IntPtr window, uint flags);

    [DllImport("user32.dll")]
    private static extern IntPtr GetWindow(IntPtr window, uint command);

    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindWindowEx(
        IntPtr parent, IntPtr childAfter, string? className, string? windowName);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int GetClassName(IntPtr window, StringBuilder className, int maxCount);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetWindowPos(
        IntPtr window, IntPtr insertAfter, int x, int y,
        int width, int height, uint flags);
}
