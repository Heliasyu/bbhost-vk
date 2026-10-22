using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using Avalonia.Controls.Primitives;
using Path = Avalonia.Controls.Shapes.Path;
using HeliasDream.Core;

namespace HeliasDream.Views;

/// <summary>What every page gets: the files, and a way to ask the window for things.</summary>
public sealed class Ctx
{
    public required OptionsFile Options { get; init; }
    public required LauncherSettings Settings { get; init; }
    public required Func<BbhostToml> Config { get; init; }
    public required Func<string?> GameDir { get; init; }
    public required Action<int> GoTo { get; init; }
    public required Action Changed { get; init; }
    public required Func<string?, System.Threading.Tasks.Task<string?>> PickFolder { get; init; }
}

public static class Pages
{
    public const int PlayTab = 0, TrophiesTab = 6;

    public static readonly string[] Names = { "Play", "Display", "Upscaling", "Graphics", "Controller", "Online", "Trophies", "More" };

    public static Control Build(int index, Ctx ctx) => index switch
    {
        0 => Play(ctx),
        1 => Settings("Display", "Screen, frame rate and camera.", OptionSchema.Display, ctx),
        2 => Upscaling(ctx),
        3 => Settings("Graphics", "Effects and the look of the world. All apply live in the game too.", OptionSchema.Graphics, ctx),
        4 => Settings("Controller", "DualSense haptics and adaptive triggers, mouse and keyboard.", OptionSchema.Controller, ctx),
        5 => Online(ctx),
        6 => TrophiesPage(ctx),
        _ => More(ctx),
    };

    // ---- Play ----

    static Control Play(Ctx ctx)
    {
        var cfg = ctx.Config();
        var checks = ReadyCheck.Local(ctx.GameDir(), cfg, ctx.Options);
        var problems = checks.Count(c => c.State == CheckState.Problem);

        var list = Ui.Col(8);
        var head = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), Margin = new Thickness(6, 0, 6, 6) };
        head.Children.Add(Ui.Eyebrow("Ready check"));
        var summary = Ui.Text(problems == 0 ? $"{checks.Count} of {checks.Count} ready" : $"{problems} to fix", 13);
        summary.Foreground = Brush.Parse(problems == 0 ? "#9FD4A8" : "#F0A0A0");
        Grid.SetColumn(summary, 1);
        head.Children.Add(summary);
        list.Children.Add(head);
        foreach (var c in checks) list.Children.Add(CheckRow(c));
        var server = CheckRow(new CheckItem("Server", "checking…", CheckState.Note));
        list.Children.Add(server);
        _ = ReadyCheck.PingServer(cfg.OnlineHost).ContinueWith(t => Dispatcher.UIThread.Post(() =>
        {
            var ms = t.Result;
            var row = CheckRow(ms != null ? new CheckItem("Server reachable", $"{cfg.OnlineHost} · {ms} ms", CheckState.Ok)
                                          : new CheckItem("Server not answering", cfg.OnlineHost, CheckState.Note));
            int i = list.Children.IndexOf(server);
            if (i >= 0) list.Children[i] = row;
        }));

        var left = Ui.Col(18,
            Ui.Col(10, Ui.Title("Bloodborne Launcher", 54), Ui.Text("Everything checks out. Press Play, or Enter.", 15, "soft")),
            Ui.Glass(list, 18));
        left.Children[0].Margin = new Thickness(28, 26, 28, 8);
        if (problems > 0) ((TextBlock)((StackPanel)left.Children[0]).Children[1]).Text = "Fix the marked lines before playing.";

        // This hunt
        var res = ctx.Options.Get("resolution", "1920x1080");
        var upscaler = ctx.Options.Get("upscaler", "FSR 1");
        var quality = ctx.Options.Get("upscale_quality", "Native AA");
        var scene = SceneSize(res, upscaler, quality);
        var facts = new UniformGrid { Columns = 2 };
        foreach (var (k, v) in new[]
                 {
                     ("Output", $"{res.Replace("x", "×")} {ctx.Options.Get("window_mode", "Windowed").ToLowerInvariant()}"),
                     ("Scene renders at", scene),
                     ("Upscaler", upscaler == "Off" ? "Off" : $"{upscaler}"),
                     ("Frame cap", ctx.Options.Get("frame_cap", "30") is var f && f == "Uncapped" ? "Uncapped" : $"{f} fps"),
                 })
        {
            var w = Ui.Well(Ui.Col(4, Ui.Text(k, 12, "muted"), Ui.Text(v, 15, null, FontWeight.SemiBold)));
            w.Margin = new Thickness(0, 0, 10, 10);
            facts.Children.Add(w);
        }
        var quick = Ui.Col(10,
            QuickRow("Frame rate", Ui.Segmented(new[] { "60", "90", "120" }, ctx.Options.Get("frame_cap", "30"), v => { ctx.Options.Set("frame_cap", v); ctx.Changed(); })),
            QuickRow("Upscaling", Ui.Segmented(new[] { "Quality", "Balanced", "Performance" }, quality, v => { ctx.Options.Set("upscale_quality", v); ctx.Changed(); })));
        var hunt = Ui.Glass(Ui.Col(14, Ui.Eyebrow("This hunt"), facts, quick), 22);

        var trophies = Trophies.Load(cfg.DataDir);
        var earned = trophies.Count(t => t.Unlocked);
        var latest = trophies.Where(t => t.Unlocked).OrderByDescending(t => t.Earned).FirstOrDefault();
        var bar = new Grid { Height = 8 };
        bar.Children.Add(new Border { Background = Brush.Parse("#59000000"), CornerRadius = new CornerRadius(4) });
        bar.Children.Add(new Border
        {
            HorizontalAlignment = HorizontalAlignment.Left, CornerRadius = new CornerRadius(4),
            Width = 300.0 * earned / trophies.Count,
            Background = new LinearGradientBrush
            {
                StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative), EndPoint = new RelativePoint(1, 0, RelativeUnit.Relative),
                GradientStops = { new(Color.Parse("#A9BFE3"), 0), new(Color.Parse("#E9EEFA"), 1) },
            },
        });
        var cup = new Border
        {
            Width = 58, Height = 58, CornerRadius = new CornerRadius(18), Background = Ui.Medal(Grade.Gold),
            Child = new Path
            {
                Data = Geometry.Parse("M7,4 L17,4 L17,8 A5,5 0 0 1 7,8 Z M7,6 L4,6 A3,3 0 0 0 7,10 M17,6 L20,6 A3,3 0 0 1 17,10 M12,13 L12,17 M8,20 L16,20"),
                Stroke = Brush.Parse("#2B2010"), StrokeThickness = 1.7, Width = 24, Height = 24, Stretch = Stretch.None,
                HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center,
            },
        };
        var tg = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto"), ColumnSpacing = 18 };
        tg.Children.Add(cup);
        var mid = Ui.Col(8, Ui.Row(10, Ui.Text("Trophies", 15, null, FontWeight.SemiBold),
            Ui.Text($"{earned} of {trophies.Count}{(latest != null ? " · latest: " + latest.Trophy.Name : "")}", 13, "muted")), bar);
        mid.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(mid, 1); tg.Children.Add(mid);
        var pct = Ui.Text($"{earned * 100 / trophies.Count}%", 34); pct.Classes.Add("display"); pct.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(pct, 2); tg.Children.Add(pct);
        var trophyCard = new Button { Classes = { "flat" }, Padding = new Thickness(0), HorizontalAlignment = HorizontalAlignment.Stretch, HorizontalContentAlignment = HorizontalAlignment.Stretch, Content = Ui.Glass(tg, 18), Background = Brushes.Transparent };
        trophyCard.Click += (_, _) => ctx.GoTo(TrophiesTab);

        var right = Ui.Col(18, hunt, trophyCard);
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("1.15*,*"), ColumnSpacing = 18 };
        g.Children.Add(left);
        Grid.SetColumn(right, 1);
        g.Children.Add(right);
        return g;
    }

    static string SceneSize(string res, string upscaler, string quality)
    {
        var p = res.Split('x');
        if (p.Length != 2 || !int.TryParse(p[0], out var w) || !int.TryParse(p[1], out var h)) return res;
        var s = upscaler is "FSR 3.1" or "FSR 4.1.1" ? OptionSchema.ScaleOf(quality) : 1.0;
        return $"{(int)Math.Round(w / s)}×{(int)Math.Round(h / s)}";
    }

    static Control CheckRow(CheckItem c)
    {
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto"), ColumnSpacing = 12 };
        g.Children.Add(Ui.StatusDot(c.State));
        var l = Ui.Text(c.Label, 14, null, FontWeight.Medium); l.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(l, 1); g.Children.Add(l);
        var d = Ui.Text(c.Detail, 12, "muted"); d.VerticalAlignment = VerticalAlignment.Center; d.TextWrapping = TextWrapping.NoWrap;
        Grid.SetColumn(d, 2); g.Children.Add(d);
        return Ui.Well(g);
    }

    static Control QuickRow(string label, Control control)
    {
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto") };
        var t = Ui.Text(label, 14); t.VerticalAlignment = VerticalAlignment.Center;
        g.Children.Add(t);
        Grid.SetColumn(control, 1); g.Children.Add(control);
        return g;
    }

    // ---- Settings pages ----

    static Control Header(string title, string sub) =>
        Ui.Col(8, Ui.Title(title, 44), Ui.Text(sub, 14, "soft"));

    static Control Settings(string title, string sub, OptionGroup[] groups, Ctx ctx)
    {
        var col = Ui.Col(18, Header(title, sub));
        col.Children[0].Margin = new Thickness(8, 6, 8, 0);
        var two = new Grid { ColumnDefinitions = new ColumnDefinitions(groups.Length > 1 ? "*,*" : "*"), ColumnSpacing = 18 };
        for (int i = 0; i < groups.Length; i++)
        {
            var g = Ui.Group(groups[i], ctx.Options, ctx.Changed);
            g.VerticalAlignment = VerticalAlignment.Top;
            Grid.SetColumn(g, Math.Min(i, 1));
            if (i >= 2) { two.RowDefinitions.Add(new RowDefinition(GridLength.Auto)); }
            two.Children.Add(g);
        }
        col.Children.Add(two);
        return col;
    }

    static Control Upscaling(Ctx ctx)
    {
        var o = ctx.Options;
        var res = o.Get("resolution", "1920x1080");
        var sceneText = Ui.Text("", 13, "soft");
        void Refresh() => sceneText.Text = $"Scene {SceneSize(res, o.Get("upscaler", "FSR 1"), o.Get("upscale_quality", "Native AA"))} → {res.Replace("x", "×")}";
        Refresh();

        var upscaler = Ui.Segmented(OptionSchema.Upscaler.Values, o.Get("upscaler", "FSR 1"), v => { o.Set("upscaler", v); Refresh(); ctx.Changed(); }, fontSize: 14, stretch: true);

        var tiles = new UniformGrid { Columns = 5 };
        var tileButtons = new List<Button>();
        foreach (var q in OptionSchema.Quality.Values)
        {
            var pct = q switch { "Quality" => "67%", "Balanced" => "58%", "Performance" => "50%", "Ultra Performance" => "33%", _ => "100%" };
            var b = new Button
            {
                Classes = { "flat", "tile" }, Margin = new Thickness(0, 0, 8, 0), HorizontalAlignment = HorizontalAlignment.Stretch,
                Content = Ui.Col(4, Ui.Text(q == "Ultra Performance" ? "Ultra Perf." : q, 14, null, FontWeight.Bold), Ui.Text(pct, 12)),
                Tag = q,
            };
            b.Classes.Set("selected", q == o.Get("upscale_quality", "Native AA"));
            b.Click += (_, _) =>
            {
                o.Set("upscale_quality", q); Refresh(); ctx.Changed();
                foreach (var x in tileButtons) x.Classes.Set("selected", ReferenceEquals(x, b));
            };
            tileButtons.Add(b);
            tiles.Children.Add(b);
        }

        var sharpRow = Ui.OptionRow(OptionSchema.Sharpness, o, ctx.Changed);
        var left = Ui.Glass(Ui.Col(22,
            Header("Upscaling", "Changes apply live in the game too (F10)."),
            Ui.Col(10, Ui.Eyebrow("Upscaler"), upscaler),
            Ui.Col(10, QuickRowText(Ui.Eyebrow("Quality"), sceneText), tiles),
            sharpRow,
            Ui.Col(8, OptionSchema.UpscalingExtras[0].Options.Select(d => Ui.OptionRow(d, o, ctx.Changed)).ToArray())), 26);

        bool built = Directory.Exists(Paths.Fsr4Assets);
        var badge = new Border
        {
            CornerRadius = new CornerRadius(999), Padding = new Thickness(12, 5), HorizontalAlignment = HorizontalAlignment.Right,
            Background = Brush.Parse(built ? "#2978C88C" : "#29E0B060"), BorderBrush = Brush.Parse(built ? "#5978C88C" : "#59E0B060"), BorderThickness = new Thickness(1),
            Child = Ui.Text(built ? "Ready" : "Not built", 12, null, FontWeight.SemiBold),
        };
        var fsrHead = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto") };
        fsrHead.Children.Add(Ui.Text("FSR 4.1.1", 16, null, FontWeight.SemiBold));
        Grid.SetColumn(badge, 1); fsrHead.Children.Add(badge);
        var fsr = Ui.Glass(Ui.Col(14, fsrHead,
            Ui.Text("Built once from your own copy of AMD's DLL with build-fsr4.sh in the release folder. Nothing from AMD ships with Helia's Dream.", 13, "soft"),
            Ui.Well(Ui.Col(4, Ui.Text("Assets", 12, "muted"), Ui.Text(built ? Paths.Fsr4Assets : "Not found", 13, null, FontWeight.SemiBold)))), 22);

        var what = Ui.Glass(Ui.Col(14, Ui.Text("What you get", 16, null, FontWeight.SemiBold),
            Ui.Text("The HUD and menus always render at the full resolution; only the 3D scene is upscaled.", 13, "soft")), 22);

        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("1.3*,*"), ColumnSpacing = 18 };
        g.Children.Add(left);
        var right = Ui.Col(18, fsr, what);
        Grid.SetColumn(right, 1); g.Children.Add(right);
        return g;
    }

    static Control QuickRowText(Control a, Control b)
    {
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto") };
        g.Children.Add(a);
        b.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(b, 1); g.Children.Add(b);
        return g;
    }

    static Control Online(Ctx ctx)
    {
        var cfg = ctx.Config();
        var signed = ctx.Options.SignedIn;
        var card = Ui.Glass(Ui.Col(12,
            Ui.Eyebrow("Account"),
            Ui.Well(Ui.Col(4, Ui.Text(signed ? "Signed in" : "Not signed in", 15, null, FontWeight.SemiBold),
                Ui.Text(signed ? (ctx.Options.AccountName ?? "") : "Sign in from F10 in the game (Account section). The live server plays only with signed-in players.", 12, "muted"))),
            Ui.Well(Ui.Col(4, Ui.Text("Server", 15, null, FontWeight.SemiBold), Ui.Text(cfg.OnlineHost, 12, "muted")))), 18);
        var col = Ui.Col(18, Header("Online", "Co-op, invasions and messages through the community server."), card);
        col.Children[0].Margin = new Thickness(8, 6, 8, 0);
        card.MaxWidth = 640; card.HorizontalAlignment = HorizontalAlignment.Left;
        return col;
    }

    static Control More(Ctx ctx)
    {
        var s = ctx.Settings;
        var dirText = Ui.Text(ctx.GameDir() ?? "Not found", 12, "muted");
        var choose = Ui.Ghost("Choose…", async () =>
        {
            var d = await ctx.PickFolder(ctx.GameDir());
            if (d == null) return;
            if (Paths.EngineIn(d) == null) { dirText.Text = "No bbhost in that folder"; return; }
            s.GameDir = d; s.Save(); dirText.Text = d; ctx.Changed();
        });
        var folderRow = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), ColumnSpacing = 18 };
        folderRow.Children.Add(Ui.Col(2, Ui.Text("Game folder", 14, null, FontWeight.Medium), dirText));
        Grid.SetColumn(choose, 1); folderRow.Children.Add(choose);

        Control Toggle(string label, string help, bool value, Action<bool> set)
        {
            var sw = new ToggleSwitch { IsChecked = value };
            sw.IsCheckedChanged += (_, _) => { set(sw.IsChecked == true); s.Save(); ctx.Changed(); };
            var g = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto"), ColumnSpacing = 18 };
            g.Children.Add(Ui.Col(2, Ui.Text(label, 14, null, FontWeight.Medium), Ui.Text(help, 12, "muted")));
            Grid.SetColumn(sw, 1); g.Children.Add(sw);
            return Ui.Well(g);
        }

        var launcher = Ui.Glass(Ui.Col(8, Ui.Eyebrow("Launcher"), Ui.Well(folderRow),
            Toggle("Reduce motion", "No parallax or sliding pages.", s.ReduceMotion, v => s.ReduceMotion = v),
            Toggle("Game art background", "Your game's key art, blurred behind the glass (read from your dump).", s.GameArtBackground, v => s.GameArtBackground = v),
            Toggle("Close when the game starts", "Otherwise the launcher waits and comes back when you quit.", s.CloseOnPlay, v => s.CloseOnPlay = v)), 18);
        launcher.VerticalAlignment = VerticalAlignment.Top;

        var col = Ui.Col(18, Header("More", "Game options, debug switches and the launcher itself."));
        col.Children[0].Margin = new Thickness(8, 6, 8, 0);
        var g2 = new Grid { ColumnDefinitions = new ColumnDefinitions("*,*"), ColumnSpacing = 18 };
        var left = Ui.Col(18, OptionSchema.More.Select(gr => Ui.Group(gr, ctx.Options, ctx.Changed)).ToArray());
        g2.Children.Add(left);
        Grid.SetColumn(launcher, 1); g2.Children.Add(launcher);
        col.Children.Add(g2);
        return col;
    }

    // ---- Trophies ----

    static Control TrophiesPage(Ctx ctx)
    {
        var cfg = ctx.Config();
        var all = Trophies.Load(cfg.DataDir);
        var icons = TrophyPackage.ReadIcons(cfg.App0);
        int earned = all.Count(t => t.Unlocked);
        double frac = (double)earned / all.Count;

        var ring = new Grid { Width = 104, Height = 104 };
        ring.Children.Add(new Ellipse { Stroke = Brush.Parse("#59000000"), StrokeThickness = 9, Margin = new Thickness(4.5) });
        ring.Children.Add(new Arc { Stroke = Brush.Parse("#C9D6EE"), StrokeThickness = 9, StrokeLineCap = PenLineCap.Round, StartAngle = -90, SweepAngle = 360 * frac, Margin = new Thickness(4.5) });
        var center = Ui.Col(0, Ui.Text($"{(int)Math.Round(frac * 100)}%", 32), Ui.Text($"{earned} of {all.Count}", 11, "muted"));
        ((TextBlock)center.Children[0]).Classes.Add("display");
        foreach (var c in center.Children) c.HorizontalAlignment = HorizontalAlignment.Center;
        center.VerticalAlignment = VerticalAlignment.Center;
        ring.Children.Add(center);

        var last = all.Where(t => t.Unlocked).OrderByDescending(t => t.Earned).FirstOrDefault();
        var needPlat = all.Count(t => !t.Trophy.Dlc && t.Trophy.Id != Trophies.PlatinumId && !t.Unlocked);
        var tiers = Ui.Row(10, new[] { Grade.Platinum, Grade.Gold, Grade.Silver, Grade.Bronze }.Select(gr =>
        {
            var of = all.Where(t => t.Trophy.Grade == gr).ToList();
            var c = Ui.Col(6, new Ellipse { Width = 28, Height = 28, Fill = Ui.Medal(gr) }, Ui.Text($"{of.Count(t => t.Unlocked)} / {of.Count}", 15, null, FontWeight.Bold), Ui.Text(gr.ToString(), 12, "muted"));
            foreach (var x in c.Children) x.HorizontalAlignment = HorizontalAlignment.Center;
            return (Control)new Border { Classes = { "well" }, Width = 96, CornerRadius = new CornerRadius(16), Child = c };
        }).ToArray());
        var head = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*,Auto"), ColumnSpacing = 26 };
        head.Children.Add(ring);
        var words = Ui.Col(6, Ui.Title("Trophies", 40),
            Ui.Text((last != null ? $"Last earned {last.Earned:MMM d}" : "None earned yet") + (needPlat > 0 ? $" · {needPlat} left until the platinum" : " · Platinum earned"), 14, "soft"));
        words.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(words, 1); head.Children.Add(words);
        tiers.VerticalAlignment = VerticalAlignment.Center;
        Grid.SetColumn(tiers, 2); head.Children.Add(tiers);

        var grid = new UniformGrid { Columns = 3 };
        void Fill(string filter)
        {
            grid.Children.Clear();
            IEnumerable<TrophyState> shown = all;
            if (filter.StartsWith("Earned")) shown = all.Where(t => t.Unlocked).OrderByDescending(t => t.Earned);
            else if (filter.StartsWith("Locked")) shown = all.Where(t => !t.Unlocked);
            else shown = all.OrderBy(t => t.Unlocked ? 0 : 1).ThenByDescending(t => t.Earned).ThenBy(t => t.Trophy.Id);
            foreach (var t in shown) grid.Children.Add(TrophyCard(t, icons));
        }
        var filters = new[] { $"All {all.Count}", $"Earned {earned}", $"Locked {all.Count - earned}" };
        var bar = Ui.Segmented(filters, filters[0], Fill);
        bar.HorizontalAlignment = HorizontalAlignment.Left;
        Fill(filters[0]);

        return Ui.Col(16, Ui.Glass(head, 22), bar, grid);
    }

    static Control TrophyCard(TrophyState t, Dictionary<int, byte[]> icons)
    {
        Control pic;
        if (icons.TryGetValue(t.Trophy.Id, out var png))
        {
            using var ms = new MemoryStream(png);
            pic = new Image { Source = new Bitmap(ms), Stretch = Stretch.UniformToFill };
        }
        else pic = new Border { Background = Brush.Parse("#59000000") };
        var picFrame = new Grid { Width = 60, Height = 60 };
        picFrame.Children.Add(new Border { CornerRadius = new CornerRadius(14), ClipToBounds = true, Child = pic, BorderBrush = Brush.Parse("#1FFFFFFF"), BorderThickness = new Thickness(1) });
        picFrame.Children.Add(new Ellipse
        {
            Width = 20, Height = 20, Fill = Ui.Medal(t.Trophy.Grade), Stroke = Brush.Parse("#11141C"), StrokeThickness = 2,
            HorizontalAlignment = HorizontalAlignment.Right, VerticalAlignment = VerticalAlignment.Bottom, Margin = new Thickness(0, 0, -5, -5),
        });

        var when = t.Unlocked ? $"Earned {t.Earned:MMM d, yyyy}" : "Locked";
        var text = Ui.Col(3, Ui.Text(t.Trophy.Name, 14, null, FontWeight.SemiBold), Ui.Text(t.Trophy.Description, 12, "muted"), Ui.Text(when + (t.Trophy.Dlc ? " · The Old Hunters" : ""), 11, "muted"));
        text.VerticalAlignment = VerticalAlignment.Center;
        var g = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,*"), ColumnSpacing = 14 };
        g.Children.Add(picFrame);
        Grid.SetColumn(text, 1); g.Children.Add(text);
        return new Border
        {
            Classes = { "glass" }, CornerRadius = new CornerRadius(18), Padding = new Thickness(14), Margin = new Thickness(0, 0, 12, 12),
            Background = Brush.Parse(t.Unlocked ? "#16FFFFFF" : "#08FFFFFF"), Opacity = t.Unlocked ? 1 : 0.55, Child = g,
        };
    }
}
