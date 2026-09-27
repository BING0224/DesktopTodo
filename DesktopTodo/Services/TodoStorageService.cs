using System.IO;
using DesktopTodo.Models;

namespace DesktopTodo.Services;

public sealed class TodoStorageService
{
    private readonly string _filePath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "DesktopTodo",
        "todos.json");

    public IReadOnlyList<TodoItem> Load()
    {
        var items = JsonFileStore.Load(_filePath, () => new List<TodoItem>());

        return items
            .OfType<TodoItem>()
            .Where(item => !string.IsNullOrWhiteSpace(item.Content))
            .OrderBy(item => item.SortOrder)
            .ThenBy(item => item.CreatedAt)
            .ToList();
    }

    public bool Save(IEnumerable<TodoItem> items)
    {
        return JsonFileStore.Save(_filePath, items.ToList());
    }
}
