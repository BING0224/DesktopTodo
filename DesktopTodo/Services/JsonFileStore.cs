using System.IO;
using System.Text.Json;

namespace DesktopTodo.Services;

internal static class JsonFileStore
{
    private static readonly JsonSerializerOptions SerializerOptions = new()
    {
        PropertyNameCaseInsensitive = true,
        WriteIndented = true
    };

    public static T Load<T>(string path, Func<T> fallbackFactory)
    {
        try
        {
            if (!File.Exists(path))
            {
                return fallbackFactory();
            }

            var json = File.ReadAllText(path);
            return JsonSerializer.Deserialize<T>(json, SerializerOptions) ?? fallbackFactory();
        }
        catch (JsonException)
        {
            BackupCorruptedFile(path);
            return fallbackFactory();
        }
        catch (IOException)
        {
            return fallbackFactory();
        }
        catch (UnauthorizedAccessException)
        {
            return fallbackFactory();
        }
    }

    public static bool Save<T>(string path, T value)
    {
        var temporaryPath = path + ".tmp";

        try
        {
            var directory = Path.GetDirectoryName(path);
            if (!string.IsNullOrWhiteSpace(directory))
            {
                Directory.CreateDirectory(directory);
            }

            var json = JsonSerializer.Serialize(value, SerializerOptions);
            File.WriteAllText(temporaryPath, json);
            File.Move(temporaryPath, path, true);
            return true;
        }
        catch (IOException)
        {
            TryDelete(temporaryPath);
            return false;
        }
        catch (UnauthorizedAccessException)
        {
            TryDelete(temporaryPath);
            return false;
        }
    }

    private static void BackupCorruptedFile(string path)
    {
        try
        {
            var backupPath = path + $".corrupted-{DateTime.Now:yyyyMMddHHmmss}";
            File.Move(path, backupPath, true);
        }
        catch (IOException)
        {
            // A broken settings file should never prevent the widget from opening.
        }
        catch (UnauthorizedAccessException)
        {
            // Keep running with defaults when the file cannot be moved.
        }
    }

    private static void TryDelete(string path)
    {
        try
        {
            if (File.Exists(path))
            {
                File.Delete(path);
            }
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }
}
