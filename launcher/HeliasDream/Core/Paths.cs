using System;
using System.Collections.Generic;
using System.IO;

namespace HeliasDream.Core;

/// <summary>
/// Where things are: bbhost's per-user files (the same ones the engine reads), the game folder
/// with the engine in it, and this launcher's own settings.
/// </summary>
public static class Paths
{
    public static bool IsWindows => OperatingSystem.IsWindows();

    /// ~/.config/bbhost (Linux), %APPDATA%\bbhost (Windows): bbhost.toml, bbhost-options.toml.
    public static string BbhostConfigDir =>
        IsWindows
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "bbhost")
            : Path.Combine(XdgConfigHome, "bbhost");

    /// ~/.local/share/bbhost (Linux), %LOCALAPPDATA%\bbhost (Windows): data, FSR 4.1.1 assets.
    public static string BbhostDataDir =>
        IsWindows
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "bbhost")
            : Path.Combine(XdgDataHome, "bbhost");

    public static string OptionsFile => Path.Combine(BbhostConfigDir, "bbhost-options.toml");
    public static string BbhostToml => Path.Combine(BbhostConfigDir, "bbhost.toml");
    public static string Fsr4Assets => Path.Combine(BbhostDataDir, "fsr4_411");

    /// This launcher's own settings (the game folder, motion).
    public static string LauncherSettingsFile =>
        IsWindows
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "helias-dream", "launcher.json")
            : Path.Combine(XdgConfigHome, "helias-dream", "launcher.json");

    static string Home => Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
    static string XdgConfigHome => Env("XDG_CONFIG_HOME") ?? Path.Combine(Home, ".config");
    static string XdgDataHome => Env("XDG_DATA_HOME") ?? Path.Combine(Home, ".local", "share");
    static string? Env(string name) => Environment.GetEnvironmentVariable(name) is { Length: > 0 } v ? v : null;

    /// The engine in a folder: the wrapper or binary `bbhost` (Linux), bbhost.exe (Windows).
    public static string? EngineIn(string? dir)
    {
        if (string.IsNullOrEmpty(dir) || !Directory.Exists(dir)) return null;
        foreach (var name in IsWindows ? new[] { "bbhost.exe" } : new[] { "bbhost", "bbhost.bin" })
        {
            var p = Path.Combine(dir, name);
            if (File.Exists(p)) return p;
        }
        return null;
    }

    /// The game folder: the saved one, else beside this launcher, else the usual places.
    public static string? FindGameDir(string? saved)
    {
        var tries = new List<string?>
        {
            Env("HELIAS_GAME_DIR"),
            saved,
            AppContext.BaseDirectory,
            Path.GetDirectoryName(AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar)),
            Path.Combine(Home, "BloodBorne PC", "bbhost-linux-v0.2.18"),
        };
        foreach (var d in tries)
            if (EngineIn(d) != null) return Path.GetFullPath(d!);
        return null;
    }
}
