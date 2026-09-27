using System.IO;
using System.Security;
using Microsoft.Win32;

namespace DesktopTodo.Services;

public sealed class StartupService
{
    private const string RunKeyPath = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "DesktopTodo";

    public bool IsEnabled()
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(RunKeyPath, false);
            var configuredCommand = key?.GetValue(ValueName) as string;
            return string.Equals(configuredCommand, GetStartupCommand(), StringComparison.OrdinalIgnoreCase);
        }
        catch (SecurityException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
    }

    public bool SetEnabled(bool enabled)
    {
        try
        {
            using var key = Registry.CurrentUser.CreateSubKey(RunKeyPath, true);
            if (enabled)
            {
                key.SetValue(ValueName, GetStartupCommand(), RegistryValueKind.String);
            }
            else
            {
                key.DeleteValue(ValueName, false);
            }

            return true;
        }
        catch (SecurityException)
        {
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            return false;
        }
        catch (IOException)
        {
            return false;
        }
    }

    private static string GetStartupCommand()
    {
        var executablePath = Environment.ProcessPath;
        if (string.IsNullOrWhiteSpace(executablePath))
        {
            throw new InvalidOperationException("Cannot determine the executable path.");
        }

        return $"\"{executablePath}\"";
    }
}
