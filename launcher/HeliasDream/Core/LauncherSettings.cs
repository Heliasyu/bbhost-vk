using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace HeliasDream.Core;

/// <summary>The launcher's own settings, in launcher.json beside bbhost's config.</summary>
public sealed class LauncherSettings
{
    public string? GameDir { get; set; }
    public bool ReduceMotion { get; set; }
    /// Blur the game's own key art (sce_sys/pic1.png in the player's dump) behind the glass.
    public bool GameArtBackground { get; set; } = true;
    public bool CloseOnPlay { get; set; }

    public static LauncherSettings Load()
    {
        try
        {
            if (File.Exists(Paths.LauncherSettingsFile))
                return JsonSerializer.Deserialize(File.ReadAllText(Paths.LauncherSettingsFile), SettingsJson.Default.LauncherSettings) ?? new();
        }
        catch (Exception) { }
        return new();
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Paths.LauncherSettingsFile)!);
            File.WriteAllText(Paths.LauncherSettingsFile, JsonSerializer.Serialize(this, SettingsJson.Default.LauncherSettings));
        }
        catch (Exception) { }
    }
}

[JsonSourceGenerationOptions(WriteIndented = true)]
[JsonSerializable(typeof(LauncherSettings))]
internal partial class SettingsJson : JsonSerializerContext { }
