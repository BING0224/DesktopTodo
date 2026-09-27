using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using System.Runtime.CompilerServices;
using DesktopTodo.Models;
using DesktopTodo.Services;

namespace DesktopTodo.ViewModels;

public sealed class MainViewModel : INotifyPropertyChanged
{
    private readonly TodoStorageService _storageService;
    private string _newTodoText = string.Empty;
    private bool _isAdding;

    public MainViewModel(TodoStorageService? storageService = null)
    {
        _storageService = storageService ?? new TodoStorageService();
        Todos = new ObservableCollection<TodoItem>(_storageService.Load());

        foreach (var item in Todos)
        {
            item.PropertyChanged += TodoItem_PropertyChanged;
        }

        Todos.CollectionChanged += Todos_CollectionChanged;

        StartAddingCommand = new RelayCommand(_ => StartAdding());
        AddTodoCommand = new RelayCommand(_ => AddTodo(), _ => !string.IsNullOrWhiteSpace(NewTodoText));
        CancelAddingCommand = new RelayCommand(_ => CancelAdding());
        DeleteTodoCommand = new RelayCommand(DeleteTodo, item => item is TodoItem);
    }

    public ObservableCollection<TodoItem> Todos { get; }

    public bool HasTodos => Todos.Count > 0;

    public string TodoSummary
    {
        get
        {
            if (Todos.Count == 0)
            {
                return "记下下一件事";
            }

            var remaining = Todos.Count(item => !item.IsCompleted);
            return $"{remaining} 项待完成 · {Todos.Count - remaining} 项已完成";
        }
    }

    public string NewTodoText
    {
        get => _newTodoText;
        set
        {
            if (!SetField(ref _newTodoText, value))
            {
                return;
            }

            AddTodoCommand.RaiseCanExecuteChanged();
        }
    }

    public bool IsAdding
    {
        get => _isAdding;
        private set => SetField(ref _isAdding, value);
    }

    public RelayCommand StartAddingCommand { get; }

    public RelayCommand AddTodoCommand { get; }

    public RelayCommand CancelAddingCommand { get; }

    public RelayCommand DeleteTodoCommand { get; }

    public event EventHandler? AddEditorRequested;

    public event PropertyChangedEventHandler? PropertyChanged;

    public void Save()
    {
        _storageService.Save(Todos);
    }

    private void StartAdding()
    {
        IsAdding = true;
        AddEditorRequested?.Invoke(this, EventArgs.Empty);
    }

    private void AddTodo()
    {
        var content = NewTodoText.Trim();
        if (content.Length == 0)
        {
            return;
        }

        Todos.Add(new TodoItem
        {
            Content = content,
            CreatedAt = DateTime.Now,
            SortOrder = Todos.Count
        });

        NewTodoText = string.Empty;
        IsAdding = false;
    }

    private void CancelAdding()
    {
        NewTodoText = string.Empty;
        IsAdding = false;
    }

    private void DeleteTodo(object? parameter)
    {
        if (parameter is TodoItem item)
        {
            Todos.Remove(item);
        }
    }

    private void Todos_CollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (e.OldItems is not null)
        {
            foreach (TodoItem item in e.OldItems)
            {
                item.PropertyChanged -= TodoItem_PropertyChanged;
            }
        }

        if (e.NewItems is not null)
        {
            foreach (TodoItem item in e.NewItems)
            {
                item.PropertyChanged += TodoItem_PropertyChanged;
            }
        }

        for (var index = 0; index < Todos.Count; index++)
        {
            Todos[index].SortOrder = index;
        }

        OnPropertyChanged(nameof(HasTodos));
        OnPropertyChanged(nameof(TodoSummary));
        Save();
    }

    private void TodoItem_PropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName is nameof(TodoItem.Content) or nameof(TodoItem.IsCompleted))
        {
            if (e.PropertyName == nameof(TodoItem.IsCompleted))
            {
                OnPropertyChanged(nameof(TodoSummary));
            }

            Save();
        }
    }

    private bool SetField<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        OnPropertyChanged(propertyName);
        return true;
    }

    private void OnPropertyChanged([CallerMemberName] string? propertyName = null)
    {
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
    }
}
