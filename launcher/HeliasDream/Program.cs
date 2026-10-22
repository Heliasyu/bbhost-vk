using Avalonia;

namespace HeliasDream;

internal static class Program
{
    [System.STAThread]
    public static void Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--shot") { Shot.Run(args); return; }
        if (args.Length == 3 && args[0] == "--steam-check") { HeliasDream.Core.Steam.RoundTrip(args[1], args[2]); return; }
        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    public static AppBuilder BuildAvaloniaApp() =>
        AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .LogToTrace();
}
