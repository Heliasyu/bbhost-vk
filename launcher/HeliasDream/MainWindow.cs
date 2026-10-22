using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Animation;
using Avalonia.Animation.Easings;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Platform.Storage;
using Avalonia.Threading;
using Avalonia.Automation;
using Avalonia.Controls.Primitives;
using Path = Avalonia.Controls.Shapes.Path;
using HeliasDream.Core;
using HeliasDream.Views;

namespace HeliasDream;

/// <summary>
/// The launcher's one window: the moonlit scene behind everything, the header with the page
/// pills, the page (gliding in on a change), and the footer with Play.
/// </summary>
public sealed class MainWindow : Window
{
    readonly LauncherSettings _settings = LauncherSettings.Load();
    readonly OptionsFile _options = new(Paths.OptionsFile);
    readonly Scene _scene;
    readonly TransitioningContentControl _page;
    readonly GlidePage _glide = new();
    readonly TextBlock _summary = Ui.Text("", 13, "soft");
    readonly Button _play;
    readonly List<Button> _tabs = new();
    readonly Border _bead;
    readonly TranslateTransform _beadMove = new();
    readonly Ctx _ctx;
    int _current = -1;
    string? _gameDir;

    public MainWindow()
    {
        Title = "Helia's Dream";
        Width = 1280; Height = 800; MinWidth = 1120; MinHeight = 720;
        WindowStartupLocation = WindowStartupLocation.CenterScreen;
        SystemDecorations = SystemDecorations.None;
        TransparencyLevelHint = new[] { WindowTransparencyLevel.Transparent };
        Background = Brushes.Transparent;
        CanResize = true;

        _gameDir = Paths.FindGameDir(_settings.GameDir);
        _ctx = new Ctx
        {
            Options = _options, Settings = _settings,
            Config = () => new BbhostToml(Paths.BbhostToml),
            GameDir = () => _gameDir = Paths.FindGameDir(_settings.GameDir),
            GoTo = i => GoTo(i),
            Changed = () => { UpdateSummary(); _scene!.ReduceMotion = _settings.ReduceMotion; _glide.Reduced = _settings.ReduceMotion; },
            PickFolder = PickFolder,
        };

        var cfg = new BbhostToml(Paths.BbhostToml);
        _scene = new Scene(_settings.GameArtBackground ? KeyArt(cfg.App0) : null) { ReduceMotion = _settings.ReduceMotion };
        _glide.Reduced = _settings.ReduceMotion;

        // Header
        var logo = new Grid { Width = 34, Height = 34 };
        logo.Children.Add(new Ellipse { Stroke = Brush.Parse("#C9D6EE"), StrokeThickness = 1.5 });
        logo.Children.Add(new Path { Data = Geometry.Parse("M16,5 C22,10 22,22 16,27 C20,21 20,11 16,5 Z"), Fill = Brush.Parse("#C9D6EE"), Width = 32, Height = 32, Stretch = Stretch.None });
        var name = Ui.Col(3, new TextBlock { Text = "Helia's Dream", FontSize = 28, Classes = { "display" }, LetterSpacing = 1.6 },
                             Ui.Text("Launcher 0.1 · v0.2.18-vk.2", 12, "muted"));
        var brand = Ui.Row(12, logo, name);
        brand.VerticalAlignment = VerticalAlignment.Center;

        _bead = new Border { Classes = { "bead" }, HorizontalAlignment = HorizontalAlignment.Left, RenderTransform = _beadMove, IsHitTestVisible = false, Opacity = 0 };
        _beadMove.Transitions = new Transitions { new DoubleTransition { Property = TranslateTransform.XProperty, Duration = TimeSpan.FromMilliseconds(480), Easing = new CubicEaseOut() } };
        _bead.Transitions = new Transitions { new DoubleTransition { Property = Layoutable.WidthProperty, Duration = TimeSpan.FromMilliseconds(480), Easing = new CubicEaseOut() } };
        var tabRow = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 4 };
        for (int i = 0; i < Pages.Names.Length; i++)
        {
            int idx = i;
            var b = new Button { Content = Pages.Names[i], Classes = { "flat" }, CornerRadius = new CornerRadius(999), Padding = new Thickness(16, 9), FontSize = 14, FontWeight = FontWeight.SemiBold };
            b.Click += (_, _) => GoTo(idx);
            _tabs.Add(b);
            tabRow.Children.Add(b);
        }
        var tabGrid = new Grid();
        tabGrid.Children.Add(_bead);
        tabGrid.Children.Add(tabRow);
        var tabs = new Border { Classes = { "track", "dark" }, Padding = new Thickness(5), Child = tabGrid, Margin = new Thickness(28, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center };
        tabRow.LayoutUpdated += (_, _) => { if (_bead.Opacity == 0 && _current >= 0) PlaceBead(false); };

        var min = IconButton("M3,8 L13,8", "Minimize", () => WindowState = WindowState.Minimized);
        var close = IconButton("M4,4 L12,12 M12,4 L4,12", "Close", Close);
        close.Classes.Add("close");
        var winButtons = Ui.Row(8, min, close);
        winButtons.VerticalAlignment = VerticalAlignment.Center;

        var header = new Grid { ColumnDefinitions = new ColumnDefinitions("Auto,Auto,*,Auto"), Background = Brushes.Transparent };
        header.Children.Add(brand);
        Grid.SetColumn(tabs, 1); header.Children.Add(tabs);
        Grid.SetColumn(winButtons, 3); header.Children.Add(winButtons);
        header.PointerPressed += (_, e) => { if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) BeginMoveDrag(e); };
        header.DoubleTapped += (_, _) => ToggleMaximized();

        // Page
        _page = new TransitioningContentControl { PageTransition = _glide, ClipToBounds = false };

        // Footer
        _play = new Button { Content = new TextBlock { Text = "PLAY", LetterSpacing = 5, Foreground = Brush.Parse("#FFF4F2") }, Classes = { "flat", "play" } };
        _play.Click += (_, _) => Play();
        var footerRow = new Grid { ColumnDefinitions = new ColumnDefinitions("*,Auto,Auto,Auto"), ColumnSpacing = 12 };
        _summary.VerticalAlignment = VerticalAlignment.Center;
        footerRow.Children.Add(_summary);
        var settingsBtn = Ui.Ghost("Settings", () => GoTo(1));
        Grid.SetColumn(settingsBtn, 1); footerRow.Children.Add(settingsBtn);
        var quit = Ui.Ghost("Quit", Close);
        Grid.SetColumn(quit, 2); footerRow.Children.Add(quit);
        Grid.SetColumn(_play, 3); footerRow.Children.Add(_play);
        var footer = new Border { Classes = { "dark" }, CornerRadius = new CornerRadius(22), Padding = new Thickness(22, 12, 12, 12), Child = footerRow };

        var layout = new Grid { RowDefinitions = new RowDefinitions("Auto,*,Auto"), RowSpacing = 18, Margin = new Thickness(26, 22) };
        layout.Children.Add(header);
        Grid.SetRow(_page, 1); layout.Children.Add(_page);
        Grid.SetRow(footer, 2); layout.Children.Add(footer);

        var root = new Grid();
        root.Children.Add(_scene);
        root.Children.Add(layout);
        root.Children.Add(Handles());
        Content = new Border { CornerRadius = new CornerRadius(22), ClipToBounds = true, Child = root, Background = Brush.Parse("#07080C") };

        KeyDown += (_, e) =>
        {
            if (e.Key == Key.Enter && _play.IsEnabled) Play();
            else if (e.Key == Key.Escape) WindowState = WindowState.Minimized;
        };

        UpdateSummary();
        GoTo(0);
    }

    void ToggleMaximized() => WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;

    /// The frameless window's own edges: the very top drags it (double-click maximizes), the
    /// corners and the other three sides resize it.
    Control Handles()
    {
        const double edge = 7, corner = 18;
        var g = new Grid { IsHitTestVisible = true, Background = null };
        Border Zone(HorizontalAlignment h, VerticalAlignment v, double w, double ht, StandardCursorType cursor, WindowEdge? resize)
        {
            var b = new Border
            {
                HorizontalAlignment = h, VerticalAlignment = v, Background = Brushes.Transparent, Cursor = new Cursor(cursor),
                Width = double.IsNaN(w) ? double.NaN : w, Height = double.IsNaN(ht) ? double.NaN : ht,
            };
            b.PointerPressed += (_, e) =>
            {
                if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed) return;
                if (resize is { } r) { if (WindowState == WindowState.Normal) BeginResizeDrag(r, e); }
                else BeginMoveDrag(e);
                e.Handled = true;
            };
            if (resize == null) b.DoubleTapped += (_, _) => ToggleMaximized();
            g.Children.Add(b);
            return b;
        }
        var top = Zone(HorizontalAlignment.Stretch, VerticalAlignment.Top, double.NaN, 14, StandardCursorType.Arrow, null);
        top.Margin = new Thickness(corner, 0, corner, 0);
        Zone(HorizontalAlignment.Left, VerticalAlignment.Stretch, edge, double.NaN, StandardCursorType.LeftSide, WindowEdge.West).Margin = new Thickness(0, corner, 0, corner);
        Zone(HorizontalAlignment.Right, VerticalAlignment.Stretch, edge, double.NaN, StandardCursorType.RightSide, WindowEdge.East).Margin = new Thickness(0, corner, 0, corner);
        Zone(HorizontalAlignment.Stretch, VerticalAlignment.Bottom, double.NaN, edge, StandardCursorType.BottomSide, WindowEdge.South).Margin = new Thickness(corner, 0, corner, 0);
        Zone(HorizontalAlignment.Left, VerticalAlignment.Top, corner, corner, StandardCursorType.TopLeftCorner, WindowEdge.NorthWest);
        Zone(HorizontalAlignment.Right, VerticalAlignment.Top, corner, corner, StandardCursorType.TopRightCorner, WindowEdge.NorthEast);
        Zone(HorizontalAlignment.Left, VerticalAlignment.Bottom, corner, corner, StandardCursorType.BottomLeftCorner, WindowEdge.SouthWest);
        Zone(HorizontalAlignment.Right, VerticalAlignment.Bottom, corner, corner, StandardCursorType.BottomRightCorner, WindowEdge.SouthEast);
        return g;
    }

    static string? KeyArt(string? app0)
    {
        if (app0 == null) return null;
        foreach (var d in new[] { app0 + "-patch", app0 + "-UPDATE", app0 })
        {
            var p = System.IO.Path.Combine(d, "sce_sys", "pic1.png");
            if (File.Exists(p)) return p;
        }
        return null;
    }

    static Button IconButton(string data, string label, Action click)
    {
        var b = new Button
        {
            Classes = { "flat", "icon" }, Padding = new Thickness(0),
            Content = new Path { Data = Geometry.Parse(data), Stroke = Brush.Parse("#D5DBE6"), StrokeThickness = 1.6, StrokeLineCap = PenLineCap.Round, Width = 16, Height = 16, Stretch = Stretch.None },
        };
        AutomationProperties.SetName(b, label);
        b.Click += (_, _) => click();
        return b;
    }

    public void ShowPage(int index) => GoTo(index);

    void GoTo(int index)
    {
        if (index == _current) return;
        bool forward = index > _current;
        _current = index;
        _options.Reload();
        var page = Pages.Build(index, _ctx);
        page.Margin = new Thickness(0, 0, 14, 6);
        var scroll = new ScrollViewer { Content = page, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, RenderTransform = new TranslateTransform() };
        scroll.ScrollChanged += (_, _) => _scene.Scrolled(scroll.Offset.Y);
        _scene.Scrolled(0);
        _page.IsTransitionReversed = !forward;
        _page.Content = scroll;
        _scene.ShowPage(index);
        PlaceBead(true);
    }

    void PlaceBead(bool animate)
    {
        if (_current < 0 || _current >= _tabs.Count) return;
        var b = _tabs[_current];
        if (b.Bounds.Width <= 0) return;
        if (!animate || _settings.ReduceMotion)
        {
            var t = _beadMove.Transitions; _beadMove.Transitions = null; _beadMove.X = b.Bounds.X; _beadMove.Transitions = t;
            var w = _bead.Transitions; _bead.Transitions = null; _bead.Width = b.Bounds.Width; _bead.Transitions = w;
        }
        else { _beadMove.X = b.Bounds.X; _bead.Width = b.Bounds.Width; }
        _bead.Height = b.Bounds.Height;
        _bead.Opacity = 1;
        for (int i = 0; i < _tabs.Count; i++) _tabs[i].Classes.Set("selected", i == _current);
    }

    void UpdateSummary()
    {
        var o = _options;
        var parts = new List<string>();
        var up = o.Get("upscaler", "FSR 1");
        parts.Add(up is "FSR 3.1" or "FSR 4.1.1" ? $"{up} {o.Get("upscale_quality", "Native AA")}" : $"Upscaler {up}");
        parts.Add(o.Get("resolution", "1920x1080").Replace("x", "×"));
        var cap = o.Get("frame_cap", "30");
        parts.Add(cap == "Uncapped" ? "Uncapped" : cap + " fps");
        parts.Add(o.SignedIn ? "Online" : "Offline");
        if (o.Get("haptics", "HD") == "HD" && ReadyCheck.DualSenseOnUsb()) parts.Add("DualSense HD");
        _summary.Text = string.Join(" · ", parts);
        _play.IsEnabled = _gameDir != null && !GameRunner.Running();
    }

    void Play()
    {
        _gameDir = Paths.FindGameDir(_settings.GameDir);
        if (_gameDir == null) { GoTo(7); return; }
        var p = GameRunner.Start(_gameDir, _settings.PresentMode, () => Dispatcher.UIThread.Post(GameEnded));
        if (p == null) return;
        if (_settings.CloseOnPlay) { Close(); return; }
        _play.IsEnabled = false;
        _play.Content = new TextBlock { Text = "RUNNING", LetterSpacing = 5, Foreground = Brush.Parse("#C8B4B4") };
        WindowState = WindowState.Minimized;
    }

    void GameEnded()
    {
        _play.Content = new TextBlock { Text = "PLAY", LetterSpacing = 5, Foreground = Brush.Parse("#FFF4F2") };
        WindowState = WindowState.Normal;
        Activate();
        int keep = _current;
        _current = -1;
        UpdateSummary();
        GoTo(keep);
    }

    async Task<string?> PickFolder(string? start)
    {
        var opts = new FolderPickerOpenOptions { Title = "The folder with bbhost in it", AllowMultiple = false };
        if (start != null) opts.SuggestedStartLocation = await StorageProvider.TryGetFolderFromPathAsync(start);
        var r = await StorageProvider.OpenFolderPickerAsync(opts);
        return r.FirstOrDefault()?.TryGetLocalPath();
    }
}
