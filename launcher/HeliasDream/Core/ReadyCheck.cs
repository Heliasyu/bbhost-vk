using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Threading.Tasks;

namespace HeliasDream.Core;

public enum CheckState { Ok, Note, Problem }

public sealed record CheckItem(string Label, string Detail, CheckState State);

/// <summary>What the Play page checks each time it opens.</summary>
public static class ReadyCheck
{
    public static List<CheckItem> Local(string? gameDir, BbhostToml cfg, OptionsFile opts)
    {
        var items = new List<CheckItem>();

        items.Add(gameDir != null
            ? new("Engine found", Path.GetFileName(gameDir.TrimEnd('/', '\\')), CheckState.Ok)
            : new("Engine not found", "Choose the folder with bbhost in More", CheckState.Problem));

        var app0 = cfg.App0;
        bool dump = app0 != null && File.Exists(Path.Combine(app0, "eboot.bin"));
        var version = dump ? GameVersion(app0!) : null;
        items.Add(dump
            ? new("Game files found", $"CUSA03173{(version != null ? " · " + version : "")}",
                  version == null || version == "1.09" ? CheckState.Ok : CheckState.Note)
            : new("Game files missing", "Set the game folder in bbhost's setup", CheckState.Problem));

        bool eboot = cfg.Eboot != null && File.Exists(cfg.Eboot);
        items.Add(eboot ? new("Decrypted eboot", "eboot.elf", CheckState.Ok)
                        : new("Decrypted eboot missing", "Needed to start the game", CheckState.Problem));

        var upscaler = opts.Get("upscaler", "FSR 1");
        if (upscaler == "FSR 4.1.1")
        {
            bool built = Directory.Exists(Path.Combine(Paths.Fsr4Assets, "t2160_m0")) || Directory.Exists(Paths.Fsr4Assets);
            items.Add(built ? new("FSR 4.1.1 ready", "Assets built", CheckState.Ok)
                            : new("FSR 4.1.1 not built", "Runs as FSR 3.1 until built", CheckState.Note));
        }
        else items.Add(new("Upscaler", upscaler, CheckState.Ok));

        items.Add(DualSenseOnUsb()
            ? new("DualSense on USB", "HD haptics", CheckState.Ok)
            : new("No DualSense on USB", "Classic rumble", CheckState.Note));

        items.Add(opts.SignedIn
            ? new("Signed in", opts.AccountName ?? "The Hunter's Dream", CheckState.Ok)
            : new("Not signed in", "Online needs an account (F10 in game)", CheckState.Note));
        return items;
    }

    /// The server's round trip, or null when it does not answer within a few seconds.
    public static async Task<int?> PingServer(string host)
    {
        try
        {
            using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
            var sw = Stopwatch.StartNew();
            using var req = new HttpRequestMessage(HttpMethod.Head, $"https://{host}/");
            using var resp = await http.SendAsync(req);
            return (int)sw.ElapsedMilliseconds;
        }
        catch (Exception) { return null; }
    }

    /// APP_VER from the update's param.sfo (or the game's), "1.09".
    static string? GameVersion(string app0)
    {
        foreach (var dir in new[] { app0 + "-patch", app0 + "-UPDATE", app0 })
        {
            var sfo = Path.Combine(dir, "sce_sys", "param.sfo");
            if (!File.Exists(sfo)) continue;
            var v = SfoString(File.ReadAllBytes(sfo), "APP_VER");
            if (v != null) return v.Length > 2 && v[0] == '0' && v[1] != '.' ? v[1..] : v;
        }
        return null;
    }

    static string? SfoString(byte[] d, string key)
    {
        if (d.Length < 20 || d[0] != 0 || d[1] != (byte)'P') return null;
        int keys = BitConverter.ToInt32(d, 8), data = BitConverter.ToInt32(d, 12), n = BitConverter.ToInt32(d, 16);
        for (int i = 0; i < n; i++)
        {
            int e = 20 + i * 16;
            int ko = BitConverter.ToUInt16(d, e), len = BitConverter.ToInt32(d, e + 4), off = BitConverter.ToInt32(d, e + 12);
            int end = Array.IndexOf(d, (byte)0, keys + ko);
            if (end < 0) continue;
            if (Encoding.ASCII.GetString(d, keys + ko, end - keys - ko) == key)
                return Encoding.UTF8.GetString(d, data + off, len).TrimEnd('\0');
        }
        return null;
    }

    /// A DualSense (054c:0ce6) or DualSense Edge (054c:0df2) on the USB bus.
    public static bool DualSenseOnUsb()
    {
        if (!OperatingSystem.IsLinux()) return false;
        try
        {
            foreach (var dev in Directory.EnumerateDirectories("/sys/bus/usb/devices"))
            {
                var v = Path.Combine(dev, "idVendor");
                if (!File.Exists(v) || File.ReadAllText(v).Trim() != "054c") continue;
                var p = File.ReadAllText(Path.Combine(dev, "idProduct")).Trim();
                if (p is "0ce6" or "0df2") return true;
            }
        }
        catch (Exception) { }
        return false;
    }
}

/// <summary>Starts the game and tells when it ends.</summary>
public static class GameRunner
{
    public static Process? Start(string gameDir, string presentMode, Action onExit)
    {
        var engine = Paths.EngineIn(gameDir);
        if (engine == null) return null;
        var psi = new ProcessStartInfo(engine) { WorkingDirectory = gameDir, UseShellExecute = false };
        // The launcher has already shown everything bbhost's own setup window would.
        psi.Environment["BBHOST_SETUP_WINDOW"] = "0";
        if (presentMode is "mailbox" or "fifo" or "relaxed" or "immediate" or "latest")
            psi.Environment["BBHOST_PRESENT_MODE"] = presentMode;
        var p = Process.Start(psi);
        if (p == null) return null;
        p.EnableRaisingEvents = true;
        p.Exited += (_, _) => onExit();
        return p;
    }

    public static bool Running() =>
        Process.GetProcessesByName("bbhost.bin").Any() || Process.GetProcessesByName("bbhost").Any();
}
