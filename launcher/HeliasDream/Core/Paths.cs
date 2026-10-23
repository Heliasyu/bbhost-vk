using System;
using System.Collections.Generic;
using System.IO;

namespace HeliasDream.Core;

/// <summary>
/// Where things are: the engine's per-user files (the same ones the engine reads), the game folder
/// with the engine in it, and this launcher's own settings.
/// </summary>
public static class Paths
{
    public static bool IsWindows => OperatingSystem.IsWindows();

    /// ~/.config/HeliasDream (Linux), %APPDATA%\HeliasDream (Windows): HeliasDream.toml,
    /// HeliasDream-options.toml. Copied once from the engine's old bbhost folders (Migrate).
    public static string BbhostConfigDir =>
        IsWindows
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "HeliasDream")
            : Path.Combine(XdgConfigHome, "HeliasDream");

    /// ~/.local/share/HeliasDream (Linux), %LOCALAPPDATA%\HeliasDream (Windows): data, FSR 4 assets.
    public static string BbhostDataDir =>
        IsWindows
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "HeliasDream")
            : Path.Combine(XdgDataHome, "HeliasDream");

    public static string OptionsFile => Path.Combine(BbhostConfigDir, "HeliasDream-options.toml");
    public static string BbhostToml => Path.Combine(BbhostConfigDir, "HeliasDream.toml");
    public static string Fsr4Assets => Path.Combine(BbhostDataDir, "fsr4_411");

    /// This launcher's own settings (the game folder, motion).
    public static string LauncherSettingsFile =>
        IsWindows
            ? Path.Combine(BbhostConfigDir, "launcher.json")
            : Path.Combine(BbhostConfigDir, "launcher.json");

    static string Home => Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
    static string XdgConfigHome => Env("XDG_CONFIG_HOME") ?? Path.Combine(Home, ".config");
    static string XdgDataHome => Env("XDG_DATA_HOME") ?? Path.Combine(Home, ".local", "share");
    static string? Env(string name) => Environment.GetEnvironmentVariable(name) is { Length: > 0 } v ? v : null;

    /// The engine in a folder: HeliasDream.bin (Linux), HeliasDream-engine.exe (Windows), or an
    /// older package's bbhost / bbhost.bin / bbhost.exe.
    public static string? EngineIn(string? dir)
    {
        if (string.IsNullOrEmpty(dir) || !Directory.Exists(dir)) return null;
        foreach (var name in IsWindows ? new[] { "HeliasDream-engine.exe", "bbhost.exe" } : new[] { "HeliasDream.bin", "bbhost", "bbhost.bin" })
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
            Path.Combine(Home, "BloodBorne PC", "HeliasDream"),
            Path.Combine(Home, "BloodBorne PC", "bbhost-linux-v0.2.18"),
        };
        foreach (var d in tries)
            if (EngineIn(d) != null) return Path.GetFullPath(d!);
        return null;
    }

    /// Once: the engine's old bbhost folders copied to HeliasDream ones (the config files
    /// renamed, as the engine does it: src/core/config.cpp), and the launcher's own settings
    /// from ~/.config/helias-dream. The old folders are kept.
    public static void Migrate()
    {
        try
        {
            var oldCfg = IsWindows ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "bbhost") : Path.Combine(XdgConfigHome, "bbhost");
            if (Directory.Exists(oldCfg) && !Directory.Exists(BbhostConfigDir))
            {
                CopyDir(oldCfg, BbhostConfigDir);
                foreach (var f in Directory.GetFiles(BbhostConfigDir))
                {
                    var n = Path.GetFileName(f);
                    if (n == "bbhost.toml") File.Move(f, Path.Combine(BbhostConfigDir, "HeliasDream.toml"));
                    else if (n.StartsWith("bbhost-options")) File.Move(f, Path.Combine(BbhostConfigDir, "HeliasDream" + n[6..]));
                }
            }
            var oldData = IsWindows ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "bbhost") : Path.Combine(XdgDataHome, "bbhost");
            if (Directory.Exists(oldData) && !Directory.Exists(BbhostDataDir)) CopyDir(oldData, BbhostDataDir);
            var oldLauncher = IsWindows ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "helias-dream", "launcher.json")
                                        : Path.Combine(XdgConfigHome, "helias-dream", "launcher.json");
            if (File.Exists(oldLauncher) && !File.Exists(LauncherSettingsFile))
            {
                Directory.CreateDirectory(BbhostConfigDir);
                File.Copy(oldLauncher, LauncherSettingsFile);
            }
        }
        catch (Exception) { }
    }

    static void CopyDir(string from, string to)
    {
        Directory.CreateDirectory(to);
        foreach (var f in Directory.GetFiles(from)) File.Copy(f, Path.Combine(to, Path.GetFileName(f)), true);
        foreach (var d in Directory.GetDirectories(from)) CopyDir(d, Path.Combine(to, Path.GetFileName(d)));
    }
}
