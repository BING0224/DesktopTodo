using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;
using DesktopTodo.Models;
using DesktopTodo.Services;
using DesktopTodo.ViewModels;

namespace DesktopTodo.Views;

public partial class MainWindow : Window
{
    private const int GwlExStyle = -20;
    private const long WsExToolWindow = 0x00000080L;
    private const long WsExAppWindow = 0x00040000L;
    private const uint SwpNoSize = 0x0001;
    private const uint SwpNoMove = 0x0002;
    private const uint SwpNoActivate = 0x0010;
    private const uint SwpFrameChanged = 0x0020;
    private const double DefaultWidth = 426;
    private const double DefaultHeight = 460;
    private const double EdgeMargin = 16;
    private const double SnapDistance = 36;

    private readonly MainViewModel _viewModel;
    private readonly SettingsService _settingsService = new();
    private readonly StartupService _startupService = new();
    private DesktopWindowService? _desktopWindowService;
    private bool _isLoaded;

    public MainWindow()
    {
        InitializeComponent();

        _viewModel = new MainViewModel();
        _viewModel.AddEditorRequested += ViewModel_AddEditorRequested;
        DataContext = _viewModel;
    }

    private void MainWindow_SourceInitialized(object? sender, EventArgs e)
    {
        var windowHandle = new WindowInteropHelper(this).Handle;
        var extendedStyle = GetWindowLongPointer(windowHandle, GwlExStyle).ToInt64();

        extendedStyle |= WsExToolWindow;
        extendedStyle &= ~WsExAppWindow;
        SetWindowLongPointer(windowHandle, GwlExStyle, new IntPtr(extendedStyle));

        SetWindowPos(
            windowHandle,
            IntPtr.Zero,
            0,
            0,
            0,
            0,
            SwpNoMove | SwpNoSize | SwpNoActivate | SwpFrameChanged);
    }

    private void MainWindow_Loaded(object sender, RoutedEventArgs e)
    {
        RestoreWindowPlacement();
        _isLoaded = true;
        Opacity = 1;
        _desktopWindowService ??= new DesktopWindowService(this);
        _desktopWindowService.Arrange(allowOwnForeground: true);
    }

    private void MainWindow_Closing(object? sender, CancelEventArgs e)
    {
        _desktopWindowService?.Dispose();
        SaveWindowPlacement();
        _viewModel.Save();
    }

    private void DragHandle_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ButtonState != MouseButtonState.Pressed || IsInsideButton(e.OriginalSource as DependencyObject))
        {
            return;
        }

        try
        {
            DragMove();
            SnapToBottomRight();
            SaveWindowPlacement();
        }
        catch (InvalidOperationException)
        {
            // DragMove can throw if the mouse button is released before it starts.
        }
    }

    private void NewTodoTextBox_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter && _viewModel.AddTodoCommand.CanExecute(null))
        {
            _viewModel.AddTodoCommand.Execute(null);
            e.Handled = true;
        }
        else if (e.Key == Key.Escape)
        {
            _viewModel.CancelAddingCommand.Execute(null);
            e.Handled = true;
        }
    }

    private void TodoRow_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ClickCount != 2 ||
            sender is not Border { DataContext: TodoItem item } row ||
            IsInsideInteractiveElement(e.OriginalSource as DependencyObject))
        {
            return;
        }

        item.IsEditing = true;
        e.Handled = true;

        Dispatcher.BeginInvoke(
            DispatcherPriority.Input,
            new Action(() =>
            {
                var editor = FindVisualChild<TextBox>(row, "TodoEditTextBox");
                if (editor is null)
                {
                    return;
                }

                editor.Focus();
                Keyboard.Focus(editor);
                editor.SelectAll();
            }));
    }

    private void TodoEditTextBox_KeyDown(object sender, KeyEventArgs e)
    {
        if (sender is not TextBox editor)
        {
            return;
        }

        if (e.Key == Key.Enter)
        {
            CommitTodoEdit(editor);
            e.Handled = true;
        }
        else if (e.Key == Key.Escape)
        {
            CancelTodoEdit(editor);
            e.Handled = true;
        }
    }

    private void TodoEditTextBox_LostKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e)
    {
        if (sender is TextBox editor && editor.DataContext is TodoItem { IsEditing: true })
        {
            CommitTodoEdit(editor);
        }
    }

    private void ViewModel_AddEditorRequested(object? sender, EventArgs e)
    {
        Dispatcher.BeginInvoke(
            DispatcherPriority.Input,
            new Action(() =>
            {
                NewTodoTextBox.Focus();
                Keyboard.Focus(NewTodoTextBox);
            }));
    }

    private void CloseButton_Click(object sender, RoutedEventArgs e)
    {
        Application.Current.Shutdown();
    }

    private void ResetWindowButton_Click(object sender, RoutedEventArgs e)
    {
        SettingsPopup.IsOpen = false;
        var workArea = SystemParameters.WorkArea;

        Width = Math.Min(DefaultWidth, workArea.Width);
        Height = Math.Min(DefaultHeight, workArea.Height);
        Left = workArea.Right - Width - EdgeMargin;
        Top = workArea.Bottom - Height - EdgeMargin;

        Dispatcher.BeginInvoke(
            DispatcherPriority.Background,
            new Action(SaveWindowPlacement));
    }

    private void SettingsButton_Click(object sender, RoutedEventArgs e)
    {
        SettingsPopup.IsOpen = !SettingsPopup.IsOpen;
    }

    private void SettingsPopup_Opened(object sender, EventArgs e)
    {
        StartupMenuItem.IsChecked = _startupService.IsEnabled();
    }

    private void StartupMenuItem_Click(object sender, RoutedEventArgs e)
    {
        var requestedState = StartupMenuItem.IsChecked == true;
        if (_startupService.SetEnabled(requestedState))
        {
            return;
        }

        StartupMenuItem.IsChecked = _startupService.IsEnabled();
        MessageBox.Show(
            this,
            "无法修改开机自启设置，请检查当前用户权限。",
            "DesktopTodo",
            MessageBoxButton.OK,
            MessageBoxImage.Warning);
    }

    private void OpenDataFolderButton_Click(object sender, RoutedEventArgs e)
    {
        SettingsPopup.IsOpen = false;
        var dataDirectory = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "DesktopTodo");

        Directory.CreateDirectory(dataDirectory);
        Process.Start(new ProcessStartInfo
        {
            FileName = dataDirectory,
            UseShellExecute = true
        });
    }

    private void ResizeThumb_DragDelta(object sender, DragDeltaEventArgs e)
    {
        if (sender is not Thumb { Tag: string direction })
        {
            return;
        }

        var width = ActualWidth;
        var height = ActualHeight;

        if (direction.Contains("Left", StringComparison.Ordinal))
        {
            var newWidth = Math.Max(MinWidth, width - e.HorizontalChange);
            Left += width - newWidth;
            Width = newWidth;
        }
        else if (direction.Contains("Right", StringComparison.Ordinal))
        {
            Width = Math.Max(MinWidth, width + e.HorizontalChange);
        }

        if (direction.Contains("Top", StringComparison.Ordinal))
        {
            var newHeight = Math.Max(MinHeight, height - e.VerticalChange);
            Top += height - newHeight;
            Height = newHeight;
        }
        else if (direction.Contains("Bottom", StringComparison.Ordinal))
        {
            Height = Math.Max(MinHeight, height + e.VerticalChange);
        }
    }

    private void ResizeThumb_DragCompleted(object sender, DragCompletedEventArgs e)
    {
        SnapToBottomRight();
        SaveWindowPlacement();
    }

    private void RestoreWindowPlacement()
    {
        var settings = _settingsService.Load();
        var workArea = SystemParameters.WorkArea;

        Width = ClampFinite(settings.Width, MinWidth, Math.Max(MinWidth, workArea.Width));
        Height = ClampFinite(settings.Height, MinHeight, Math.Max(MinHeight, workArea.Height));

        if (settings.HasWindowPlacement && double.IsFinite(settings.Left) && double.IsFinite(settings.Top))
        {
            Left = Math.Clamp(settings.Left, workArea.Left, Math.Max(workArea.Left, workArea.Right - Width));
            Top = Math.Clamp(settings.Top, workArea.Top, Math.Max(workArea.Top, workArea.Bottom - Height));
            SnapToBottomRight();
            return;
        }

        Left = workArea.Right - Width - EdgeMargin;
        Top = workArea.Bottom - Height - EdgeMargin;
    }

    private void SnapToBottomRight()
    {
        var workArea = SystemParameters.WorkArea;
        var right = workArea.Right - EdgeMargin;
        var bottom = workArea.Bottom - EdgeMargin;

        if (Math.Abs(Left + ActualWidth - right) <= SnapDistance)
        {
            Left = right - ActualWidth;
        }

        if (Math.Abs(Top + ActualHeight - bottom) <= SnapDistance)
        {
            Top = bottom - ActualHeight;
        }
    }

    private void SaveWindowPlacement()
    {
        if (!_isLoaded || WindowState != WindowState.Normal)
        {
            return;
        }

        _settingsService.Save(new AppSettings
        {
            HasWindowPlacement = true,
            Left = Left,
            Top = Top,
            Width = ActualWidth,
            Height = ActualHeight
        });
    }

    private static double ClampFinite(double value, double minimum, double maximum)
    {
        return double.IsFinite(value) ? Math.Clamp(value, minimum, maximum) : minimum;
    }

    private static bool IsInsideButton(DependencyObject? element)
    {
        while (element is not null)
        {
            if (element is Button)
            {
                return true;
            }

            element = VisualTreeHelper.GetParent(element);
        }

        return false;
    }

    private static bool IsInsideInteractiveElement(DependencyObject? element)
    {
        while (element is not null)
        {
            if (element is ButtonBase or TextBox)
            {
                return true;
            }

            element = VisualTreeHelper.GetParent(element);
        }

        return false;
    }

    private static T? FindVisualChild<T>(DependencyObject parent, string name) where T : FrameworkElement
    {
        for (var index = 0; index < VisualTreeHelper.GetChildrenCount(parent); index++)
        {
            var child = VisualTreeHelper.GetChild(parent, index);
            if (child is T match && match.Name == name)
            {
                return match;
            }

            var nestedMatch = FindVisualChild<T>(child, name);
            if (nestedMatch is not null)
            {
                return nestedMatch;
            }
        }

        return null;
    }

    private static void CommitTodoEdit(TextBox editor)
    {
        if (editor.DataContext is not TodoItem item)
        {
            return;
        }

        var content = editor.Text.Trim();
        if (content.Length > 0)
        {
            item.Content = content;
            editor.Text = content;
        }
        else
        {
            editor.Text = item.Content;
        }

        item.IsEditing = false;
    }

    private static void CancelTodoEdit(TextBox editor)
    {
        if (editor.DataContext is TodoItem item)
        {
            editor.Text = item.Content;
            item.IsEditing = false;
        }
    }

    private static IntPtr GetWindowLongPointer(IntPtr windowHandle, int index)
    {
        return IntPtr.Size == 8
            ? GetWindowLongPtr64(windowHandle, index)
            : new IntPtr(GetWindowLong32(windowHandle, index));
    }

    private static IntPtr SetWindowLongPointer(IntPtr windowHandle, int index, IntPtr newValue)
    {
        return IntPtr.Size == 8
            ? SetWindowLongPtr64(windowHandle, index, newValue)
            : new IntPtr(SetWindowLong32(windowHandle, index, newValue.ToInt32()));
    }

    [DllImport("user32.dll", EntryPoint = "GetWindowLongW")]
    private static extern int GetWindowLong32(IntPtr windowHandle, int index);

    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    private static extern IntPtr GetWindowLongPtr64(IntPtr windowHandle, int index);

    [DllImport("user32.dll", EntryPoint = "SetWindowLongW")]
    private static extern int SetWindowLong32(IntPtr windowHandle, int index, int newValue);

    [DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")]
    private static extern IntPtr SetWindowLongPtr64(IntPtr windowHandle, int index, IntPtr newValue);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetWindowPos(
        IntPtr windowHandle,
        IntPtr insertAfter,
        int x,
        int y,
        int width,
        int height,
        uint flags);
}
