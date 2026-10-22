namespace HeliasDream.Core;

/// <summary>
/// One setting of bbhost-options.toml: its key, the values the engine accepts (the same lists as
/// src/host/options.cpp's g_set, which the F10 menu shows), the default, and its help line.
/// </summary>
public sealed record OptionDef(string Key, string Label, string[] Values, int Default, string Help, bool Restart = false)
{
    public string DefaultValue => Values[Default];
}

public sealed record OptionGroup(string Title, OptionDef[] Options);

public static class OptionSchema
{
    static readonly string[] OnOff = { "On", "Off" };
    static readonly string[] OffOn = { "Off", "On" };
    static readonly string[] Zero10 = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" };

    public static readonly OptionDef Resolution = new("resolution", "Resolution",
        new[] { "1280x720", "1600x900", "1920x1080", "2560x1440", "3200x1800", "3840x2160", "2560x1080", "3440x1440",
                "5120x2160", "3840x1080", "5120x1440", "1280x800", "960x600", "1024x640" }, 2,
        "What the game renders at. The largest sizes can need a restart.", true);
    public static readonly OptionDef FrameCap = new("frame_cap", "Frame rate", new[] { "30", "60", "90", "120", "Uncapped" }, 0,
        "30 is the game's own pace. 60 and 90 run its logic at that rate; 120 steps it by the frame time.", true);
    public static readonly OptionDef Upscaler = new("upscaler", "Upscaler", new[] { "Off", "FSR 1", "FSR 3.1", "FSR 4.1.1" }, 1,
        "FSR 4.1.1 replays AMD's upscaler from assets built once from your own DLL; without them it is FSR 3.1.", true);
    public static readonly OptionDef Quality = new("upscale_quality", "Quality",
        new[] { "Native AA", "Quality", "Balanced", "Performance", "Ultra Performance" }, 0,
        "The share of the resolution the 3D scene renders at. Menus and text stay sharp.");
    public static readonly OptionDef Sharpness = new("upscale_sharpness", "Sharpness",
        new[] { "0.0", "0.1", "0.2", "0.3", "0.4", "0.5", "0.6", "0.7", "0.8", "0.9", "1.0" }, 8,
        "How much FSR 3.1 and FSR 4.1.1 sharpen. 0 turns it off.");

    /// Render scale per quality preset (output / render), as the engine uses them.
    public static double ScaleOf(string quality) => quality switch
    {
        "Quality" => 1.5, "Balanced" => 1.7, "Performance" => 2.0, "Ultra Performance" => 3.0, _ => 1.0,
    };

    /// The longer explanation each setting shows on hover, after the engine's own help
    /// (src/host/options.cpp), in plain words.
    public static readonly System.Collections.Generic.Dictionary<string, string> Tips = new()
    {
        ["resolution"] = "What the game renders at, before any upscaling. 1920x1080 is what it shipped with. Wide (21:9, 32:9) and 16:10 screens are filled, with the HUD kept 16:9. The largest sizes can need a restart, because their memory is set aside when the game starts.",
        ["window_mode"] = "Fullscreen is a borderless window over the whole display at the desktop resolution: no mode switch when you alt-tab, and overlays and pop-ups (trophies, the FPS counter, Steam, Discord) show over it. Windowed keeps the title bar.",
        ["vsync"] = "On waits for the display before showing a frame (no tearing). Off shows frames as soon as they are ready, which can tear. With a variable refresh rate (FreeSync, G-Sync) monitor either works well; the Presentation mode setting decides exactly how frames are delivered.",
        ["frame_cap"] = "The game's pace. 30 is how it shipped. 60 and 90 run the game's logic at that fixed rate. 120 and Uncapped step it by the real frame time, so the game keeps real speed if the frame rate dips. 120 never goes above 120 fps; Uncapped follows the display (up to 240 with V-Sync off). Physics and timings are made for these rates; above 120 is less tested.",
        ["fps_counter"] = "Frames per second actually shown on screen, in the top right corner.",
        ["fov"] = "How wide the camera sees. +0% is the game's own (about 43 degrees up and down); each step widens it 5%. +50% is about 97 degrees across.",
        ["camera_distance"] = "How far behind your character the camera sits. 5 is the game's own; each step is 10% nearer or farther.",
        ["camera_height"] = "How high above your character the camera looks. 5 is the game's own.",
        ["upscaler"] = "FSR 4.1.1: AMD's newest upscaler, the sharpest; it needs assets built once from your own copy of AMD's DLL, and falls back to FSR 3.1 without them. FSR 3.1: temporal, uses motion vectors, works on any GPU. FSR 1: simple and light, no motion vectors. Off renders at the full resolution.",
        ["upscale_quality"] = "How much smaller the 3D scene renders before it is upscaled: Native AA 100% (just anti-aliasing), Quality 67%, Balanced 58%, Performance 50%, Ultra Performance 33%. Lower is faster. Menus, text and the HUD always stay at the full resolution. Changes apply live.",
        ["upscale_sharpness"] = "How much FSR 3.1 and FSR 4.1.1 sharpen the upscaled picture. 0 turns sharpening off; 0.8 matches FSR 1's default. FSR 1 keeps its own sharpening.",
        ["upscale_reactive"] = "Marks particles, fog, Blood Echoes, the lantern's glow and other transparent effects for the upscaler, so they do not smear or leave ghost trails. Leave on; off is for comparison.",
        ["upscale_jitter"] = "The 3D scene shifts by a fraction of a pixel each frame so the upscaler can rebuild fine detail. Off gives a softer, more aliased picture; it is for comparison only.",
        ["anti_alias"] = "The game's own anti-aliasing pass. Auto turns it off while FSR 4.1.1 upscales (it smooths edges itself, and the pass costs about 0.35 ms at 4K) and on otherwise.",
        ["ssao"] = "Screen-space ambient occlusion: soft shadows in corners and where objects meet. Off is faster and flatter.",
        ["motion_blur"] = "The blur on fast camera and character movement. Off also saves a little GPU time.",
        ["depth_of_field"] = "Blurs distant scenery, as the game does in some views. Indoors there is rarely anything far enough to blur.",
        ["chromatic_aberration"] = "The colour fringing at the edges of the frame, like an old lens.",
        ["vignette"] = "The darkened frame edges some areas use. Most areas have none, so Off often changes nothing.",
        ["bloom"] = "The glow around bright light. 10 is the area's own; lower dims it, 0 removes it.",
        ["ao_strength"] = "How dark ambient occlusion makes corners and contact shadows. 5 is the game's own; 0 none, 10 twice as dark.",
        ["saturation"] = "Colour intensity. 5 is the game's own; 0 is black and white, 10 twice as vivid.",
        ["fog"] = "How thick distance and height fog are. 10 is the area's own; 0 clears it.",
        ["shadow_distance"] = "How far the sun and moon cast shadows. Farther spreads each shadow map wider, so shadows nearby get a little softer.",
        ["model_detail"] = "Normal is the game's own. Low and Lowest swap in simpler models sooner (faster); Full always uses the finest models.",
        ["haptics"] = "HD: a DualSense or DualSense Edge plugged in by USB plays the game's rumble and its sound effects (hits, gunshots, footsteps, doors) on its haptic motors, with music and ambience filtered out. Rumble: classic vibration. Over Bluetooth, or with another controller, HD is Rumble.",
        ["haptics_strength"] = "How strong HD haptics are. 5 is the default, 0 nothing, 10 twice as strong.",
        ["adaptive_triggers"] = "DualSense and DualSense Edge: L2 (your gun) has a trigger break that goes slack when you are out of Quicksilver Bullets; R2 resists on a heavy attack's wind-up, rumbles as it charges and kicks on release. Relaxed in menus and loading screens.",
        ["trigger_strength"] = "How hard the adaptive triggers push back.",
        ["menu_haptics"] = "HD haptics: a light tap as the cursor moves through the title menu, pause menu, inventory and System menu, and a firmer one on confirm and back.",
        ["mouse_camera"] = "The mouse turns the camera whenever no menu is open. Off leaves the camera to the controller.",
        ["mouse_sens"] = "How far a given mouse movement turns the camera. 5 is the default.",
        ["mouse_invert_x"] = "Reverses the mouse's horizontal camera movement.",
        ["mouse_invert_y"] = "Reverses the mouse's vertical camera movement.",
        ["mouse_auto_rotation"] = "Off: while you use the mouse, the camera stays where you put it instead of swinging behind you as you move. A controller always gets the game's own camera.",
        ["mouse_menu"] = "In menus: click confirms, right-click goes back, the wheel moves the cursor. A click only reaches menus while one is open, so it never swings a weapon.",
        ["button_prompts"] = "What the game's own button prompts and tutorial notes show. Auto follows the device you used last, keyboard or controller.",
        ["skip_logos"] = "Skips the three company logos before the title screen, and the warning shown when the last session did not end with Exit Game.",
        ["change_appearance"] = "The Hunter's Dream mirror opens the character appearance editor (the game's unused \"Put on Disguise\").",
        ["rebirth"] = "With the Yharnam Stone, the Altar of Despair resets your level and attributes so you can rebuild your character.",
        ["five_players"] = "Allows two invaders in Mensis, Mergo's Loft, the Frontier and the DLC. Every player in the session needs it on.",
        ["debug_camera"] = "Lance McDonald's restored debug camera: hold Interact (E) and press L3 (Left Ctrl) to cycle the free camera's modes. The debug menu's load tests crash while it is on.",
        ["debug_motion_vectors"] = "Draws the scene's motion vectors over the picture: red and green for sideways and up-down movement, blue where a moving object moved a pixel. For checking the upscaler.",
        ["debug_upscaler_time"] = "Shows the upscaler's and the presenter's GPU time under the FPS counter, and logs it every 10 seconds.",
    };

    public static readonly OptionGroup[] Display =
    {
        new("Screen", new[]
        {
            Resolution,
            new OptionDef("window_mode", "Window mode", new[] { "Windowed", "Fullscreen" }, 0, "Fullscreen is borderless: no mode switch."),
            new OptionDef("vsync", "V-Sync", OnOff, 0, "Off presents as soon as a frame is ready, and can tear."),
            FrameCap,
            new OptionDef("fps_counter", "FPS counter", OffOn, 0, "Frames per second actually presented, top right."),
        }),
        new("Camera", new[]
        {
            new OptionDef("fov", "Field of view", new[] { "+0%", "+5%", "+10%", "+15%", "+20%", "+25%", "+30%", "+35%", "+40%", "+45%", "+50%" }, 0,
                "How wide the camera sees. +0% is the game's own."),
            new OptionDef("camera_distance", "Camera distance", Zero10, 5, "5 is the game's own."),
            new OptionDef("camera_height", "Camera height", Zero10, 5, "5 is the game's own."),
        }),
    };

    public static readonly OptionGroup[] UpscalingExtras =
    {
        new("Fine tuning", new[]
        {
            new OptionDef("upscale_reactive", "Reactive mask", OffOn, 1, "Keeps fog, particles and the lantern's glow from smearing."),
            new OptionDef("upscale_jitter", "Camera jitter", OffOn, 1, "Lets the upscaler rebuild fine detail."),
            new OptionDef("anti_alias", "Game anti-aliasing", new[] { "On", "Off", "Auto" }, 2, "Auto: off while FSR 4.1.1 upscales, on otherwise."),
        }),
    };

    public static readonly OptionGroup[] Graphics =
    {
        new("Effects", new[]
        {
            new OptionDef("ssao", "Ambient occlusion", OnOff, 0, "Off is faster."),
            new OptionDef("motion_blur", "Motion blur", OnOff, 0, "The blur on fast camera and character movement."),
            new OptionDef("depth_of_field", "Depth of field", OnOff, 0, "Blurs distant scenery."),
            new OptionDef("chromatic_aberration", "Chromatic aberration", OnOff, 0, "The colour fringing at the frame's edges."),
            new OptionDef("vignette", "Vignette", OnOff, 0, "Darkened edges, in the few areas that use one."),
        }),
        new("Look", new[]
        {
            new OptionDef("bloom", "Bloom", Zero10, 10, "The glow around bright light. 10 is the game's own."),
            new OptionDef("ao_strength", "AO strength", Zero10, 5, "5 is the game's own."),
            new OptionDef("saturation", "Saturation", Zero10, 5, "5 is the game's own; 0 is black and white."),
            new OptionDef("fog", "Fog", Zero10, 10, "10 is the game's own; 0 clears it."),
            new OptionDef("shadow_distance", "Shadow distance",
                new[] { "x1.0", "x1.2", "x1.4", "x1.6", "x1.8", "x2.0", "x2.2", "x2.4", "x2.6", "x2.8", "x3.0" }, 0, "How far shadows reach."),
            new OptionDef("model_detail", "Model detail", new[] { "Full", "Normal", "Low", "Lowest" }, 1, "Normal is the game's own."),
        }),
    };

    public static readonly OptionGroup[] Controller =
    {
        new("DualSense", new[]
        {
            new OptionDef("haptics", "Haptics", new[] { "Off", "Rumble", "HD" }, 2, "HD: a DualSense on USB feels the game's rumble and sound effects."),
            new OptionDef("haptics_strength", "Haptics strength", Zero10, 5, "5 is the default; 10 is twice as strong."),
            new OptionDef("adaptive_triggers", "Adaptive triggers", OnOff, 0, "L2's trigger break and R2's charge resistance."),
            new OptionDef("trigger_strength", "Trigger strength", new[] { "Light", "Medium", "Strong" }, 1, "How hard the triggers push back."),
            new OptionDef("menu_haptics", "Menu haptics", OnOff, 0, "A light tap as the cursor moves through menus."),
        }),
        new("Mouse and keyboard", new[]
        {
            new OptionDef("mouse_camera", "Mouse camera", OnOff, 0, "The mouse turns the camera outside menus."),
            new OptionDef("mouse_sens", "Mouse sensitivity", Zero10, 5, "How far a mouse movement turns the camera."),
            new OptionDef("mouse_invert_x", "Invert X", OffOn, 0, "Reverses horizontal camera movement."),
            new OptionDef("mouse_invert_y", "Invert Y", OffOn, 0, "Reverses vertical camera movement."),
            new OptionDef("mouse_auto_rotation", "Auto-rotation", OffOn, 0, "Off: the camera stays where you put it."),
            new OptionDef("mouse_menu", "Mouse in menus", OffOn, 1, "Click confirms, right-click goes back."),
            new OptionDef("button_prompts", "Button prompts", new[] { "Auto", "Keyboard", "Controller" }, 0, "Auto follows the device you last used."),
        }),
    };

    public static readonly OptionGroup[] More =
    {
        new("Game", new[]
        {
            new OptionDef("skip_logos", "Skip company logos", OffOn, 0, "And the warning after a session that did not end with Exit Game.", true),
            new OptionDef("change_appearance", "Hunter's Dream mirror", OnOff, 0, "The Dream's mirror opens the appearance editor.", true),
            new OptionDef("rebirth", "Rebirth at the Altar", OnOff, 0, "With the Yharnam Stone, the Altar of Despair resets your level.", true),
            new OptionDef("five_players", "Five players", OnOff, 0, "Two invaders in some areas; all players need it.", true),
        }),
        new("Debug", new[]
        {
            new OptionDef("debug_camera", "Debug camera", OffOn, 0, "Hold Interact and press L3 to cycle the free camera.", true),
            new OptionDef("debug_motion_vectors", "Show motion vectors", OffOn, 0, "Draws the scene's motion vectors over the picture."),
            new OptionDef("debug_upscaler_time", "Show upscaler GPU time", OffOn, 0, "Under the FPS counter, and in the log."),
        }),
    };
}
