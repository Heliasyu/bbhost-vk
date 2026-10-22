using Avalonia;

namespace HeliasDream;

internal static class Program
{
    [System.STAThread]
    public static void Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--shot") { Shot.Run(args); return; }
        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    public static AppBuilder BuildAvaloniaApp() =>
        AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .LogToTrace();
}
