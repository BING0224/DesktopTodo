using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Text.Json.Serialization;

namespace DesktopTodo.Models;

public sealed class TodoItem : INotifyPropertyChanged
{
    private string _content = string.Empty;
    private bool _isCompleted;
    private bool _isEditing;

    public Guid Id { get; set; } = Guid.NewGuid();

    public string Content
    {
        get => _content;
        set => SetField(ref _content, value);
    }

    public bool IsCompleted
    {
        get => _isCompleted;
        set => SetField(ref _isCompleted, value);
    }

    public DateTime CreatedAt { get; set; } = DateTime.Now;

    public int SortOrder { get; set; }

    [JsonIgnore]
    public bool IsEditing
    {
        get => _isEditing;
        set => SetField(ref _isEditing, value);
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    private void SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return;
        }

        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}
