namespace DesktopTodo.Models;

public sealed class AppSettings
{
    public bool HasWindowPlacement { get; set; }

    public double Left { get; set; }

    public double Top { get; set; }

    public double Width { get; set; } = 426;

    public double Height { get; set; } = 460;
}
