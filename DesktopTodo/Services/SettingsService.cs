using System.IO;
using DesktopTodo.Models;

namespace DesktopTodo.Services;

public sealed class SettingsService
{
    private readonly string _filePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "DesktopTodo",
        "settings.json");

    public AppSettings Load()
    {
        return JsonFileStore.Load(_filePath, () => new AppSettings());
    }

    public bool Save(AppSettings settings)
    {
        return JsonFileStore.Save(_filePath, settings);
    }
}
