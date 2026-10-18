# Testing bbhost-vk: `bbtest` sessions

Every phase of [PLAN.md](PLAN.md) ends with a short test on the player's PC.
`packaging/vk/bbtest` records it: the bbhost log, MangoHud frame times, a
screenshot of the game's monitor every few seconds, optional replay clips,
and the marks you drop when something looks wrong. One session is one
folder, which is all there is to hand over.

bbtest installs nothing and changes nothing outside its session folder. It
uses whatever of these it finds: `spectacle` or `gpu-screen-recorder`
(screenshots), `gpu-screen-recorder` (`--record`), MangoHud, `kscreen-doctor`
+ `python3` (to find the game's monitor), ImageMagick (to crop spectacle's
full-desktop shot to that monitor), `notify-send`.

## Setup (once)
1. Copy `packaging/vk/bbtest` somewhere on your PATH, or just beside the game:
   `cp packaging/vk/bbtest "$HOME/BloodBorne PC/bbhost-linux-v0.2.18/"`.
   Everything below writes `bbtest`; use the full path if it isn't on PATH.
2. The game folder defaults to `~/BloodBorne PC/bbhost-linux-v0.2.18`
   (where `./bbhost`, the wrapper, and `bbhost.bin` are). Set
   `BBTEST_GAME_DIR` for another.
3. The game's monitor is found as the enabled output with the most pixels
   (the 4K one). Set `BBTEST_MONITOR=DP-1` (a name from `kscreen-doctor -o`)
   to choose it yourself.

## Commands
| Command | What it does |
|---|---|
| `bbtest start <phase> [--record] [--capture] [--detach] [-- args]` | Starts a session in `~/.local/share/bbtest/sessions/<YYYYmmdd-HHMMSS>-<phase>/`, launches `./bbhost` from the game folder (the KDE/Wayland wrapper, exactly as you normally start it) with any `args`, and appends its output to `bbhost.log`. Runs until the game quits, then bundles the session. `--detach` returns at once (for starting from a shortcut). |
| `bbtest mark [note]` | Screenshot, the last 30 s clip (with `--record`), and a timestamped `MARK` line in `marks.log` and `bbhost.log`. With `--capture`, bbhost also writes the presented frame as a PNG into `frames/`. |
| `bbtest clip` | Saves the last 30 s of video into `clips/` (with `--record`). |
| `bbtest shot` | One screenshot now. |
| `bbtest status` | The session folder and what is running. |
| `bbtest stop` | Quits the game (SIGTERM; killed after 10 s), stops the helpers and bundles the session. Quitting the game normally does the same. |

`--record` keeps a `gpu-screen-recorder` replay buffer of the game's monitor
(`-r 30 -f 60 -c mp4`; `BBTEST_REPLAY_SECS`, `BBTEST_FPS`). It is held in RAM
and costs little on an RX 9070 XT; nothing is written until a clip is saved.

`--capture` sets `BBHOST_VK_CAPTURE=1` (below). Leave it off for tests whose
point is "identical to stock" (Phase 0): it adds one usage flag to the
swapchain images.

## What a session folder holds
- `bbhost.log`: bbhost's whole output, with the `MARK` lines.
- `marks.log`: START, MARK, CLIP, SHOT, GAME-EXIT and STOP with times (CDT/CST).
- `session.txt`: phase, times, monitor, `bbhost.bin`'s SHA-256, Vulkan
  driver, kernel; at the end, the error-line count and what was captured.
- `options-before.toml`, `options-after.toml`, `options.diff`: the F10
  settings file (`~/.config/bbhost/bbhost-options.toml`) at both ends. Its
  `[account]` section (your account token) is **removed** from these copies.
- `shots/`: `auto-*.png` every 10 s (`BBTEST_SHOT_EVERY`, 0 for none), `mark-*.png`.
- `clips/`: the replay clips. `frames/`: bbhost's own presented-frame PNGs.
- `mangohud/`: MangoHud's CSV (every frame's frame time).
- `game-logs/`: files bbhost wrote into the game folder's `logs/` during the session.

## MangoHud and goverlay
bbtest starts the game with `MANGOHUD=1` and
`MANGOHUD_CONFIG=read_cfg,output_folder=<session>/mangohud,autostart_log=1,log_interval=0`.
`read_cfg` keeps your own `~/.config/MangoHud/MangoHud.conf` (goverlay's
layout) and only adds the logging; any `MANGOHUD_CONFIG` you already export
is appended. `BBTEST_MANGOHUD=keep` leaves MangoHud's variables alone (no
CSV unless your config logs), `BBTEST_MANGOHUD=0` turns it off.

## A KDE shortcut for `bbtest mark`
System Settings -> Keyboard -> Shortcuts -> **Add New** -> **Command or Script...**
- Command: `"$HOME/BloodBorne PC/bbhost-linux-v0.2.18/bbtest" mark`
  (or `bbtest mark` if it is on PATH; a shortcut does not read your shell's
  PATH changes from `.bashrc`, so the full path is safest)
- Name: `bbtest mark`; then set the shortcut, e.g. **Meta+Shift+M** (pick one
  the game does not use; F9-F12 are bbhost's).

The same way, `bbtest clip` on another key is handy with `--record`. Marks
show a short notification when `notify-send` exists.

## bbhost's own debug switches (bbhost-vk)
- **Show Upscaler GPU Time** (F10 -> UPSCALING, System -> PC Upscaling, or the
  setup window): the presenter's passes timed with Vulkan timestamps
  (`present`, and `upscale` or `one pass`), shown under the FPS counter and
  logged every 300 frames as `present-timer: GPU ms a frame ...`. Stock
  bbhost's FSR 1 also logs its own `fsr:` timing.
- **Show Motion Vectors**: a placeholder; logs once that there are none yet (Phase 2).
- **Presented-frame capture**: `BBHOST_VK_CAPTURE=1` (bbtest `--capture`).
  Then Shift+F12, a file `capture.request` appearing in the capture folder
  (what `bbtest mark` does), or `BBHOST_VK_CAPTURE_EVERY=<seconds>` writes
  the swapchain image as it goes to the screen - after the upscale, with any
  overlay - to `BBHOST_VK_CAPTURE_DIR` (default `logs/captures`) as
  `frame-<date>-<time>-<ms>-<upscaler>.png`. Without the variable Shift+F12
  is plain F12 (the game's render targets, pre-upscale), as in stock bbhost.
- **Scene split** (Phase 1): `BBHOST_SCENE_SPLIT=0` never splits the scene
  from the output (Upscaling quality then changes nothing, as in Phase 0).
  The log's `scene-split:` lines say the render size, the scale pass and, at
  start, every patched site's original bytes; `heap:` lines come before and
  after each size change, `yebis: restart N` with each YEBIS restart.

## Test build session (test1, ~90 min)
This session covers the first test build from start to finish. The most important checks come
first, so if time runs out, stop after part 4. The build is
`/workspace/bbhost-vk-builds/test1/bbhost-vk-linux-<version>.tar.gz` on the build box; check it
against its `.sha256`. Install it as the README's "Testing bbhost-vk" section describes:
`cp bbhost.bin bbhost.bin.upstream` first, then copy the tarball's `bbhost` over `bbhost.bin`,
and keep your own `plugins/`. Resolution is 3840x2160 (4K) unless a part says otherwise. Keep
**Show FPS** on throughout, and MangoHud/goverlay as usual.

### 1. Launches (A, 5 min): `bbtest start t1-launch`
1. The setup window opens. Under the upscaling options: **Upscaler** has Off / FSR 1 / FSR 3.1 /
   FSR 4.1.1, and **Upscaling quality**, **Upscaling sharpness**, **Upscaler reactive mask**,
   **Upscaler jitter**, the two debug checkboxes and the **FSR 4.1.1 DLL** path row are all
   there. Frame rate offers **120**.
2. Set them all to non-default values, then back to **FSR 1** / **Native AA**, and press Play.
3. In the game: F10 > UPSCALING, flip every row once (Upscaler through all four, Quality
   through all five, Sharpness, the toggles). Then Esc > System > **PC Upscaling** and
   **Upscaling Quality**, and flip their rows too. End on **FSR 1** / **Native AA**.
4. Load your save, look around for a minute.

Pass:
- It starts.
- Every option is in `options-after.toml` with its last value.
- The log has `upscaler:` lines for each change and no new errors.
- With FSR 1 / Off at Native AA the picture looks exactly like stock (`scene-split: ... (not
  split ...)`).

### 2. Online (E, 5 min), FSR 3.1 at Quality
Online must work exactly as with upstream bbhost: same servers, sign-in and networking.
1. Sign in as usual. The log shows sign-in succeeding and the user agent `bbhost/v0.2.18-vk...`.
2. In an online area: other players' messages and bloodstains are visible (read one, touch one).
3. Put down a summon sign or ring a bell (Beckoning or Small Resonant Bell) and check it is
   accepted. If someone answers, do a summon round trip.

Pass: as with upstream. If not, run once with `bbhost.bin.upstream` to compare.

### 3. FSR 3.1 picture (D, 20 min): `bbtest start t1-fsr3 --record`
Turn on **Show Upscaler GPU Time**. The GPU line now also shows `scene: MV x  reactive x
opaque x  FSR 3.1 x  copy x ms`.
1. Setup: **FSR 3.1**, **Quality**, default sharpness, reactive mask **On**, jitter **On**.
   Load your save (Central Yharnam or anywhere with enemies).
2. Spend ~10 s on each of the following. Run `bbtest clip` after each, and `bbtest mark <what>`
   when something looks wrong.
   - **Stand still at a detailed spot** (fences, railings, roof tiles, hair, coat fringes):
     stable, no crawling, sharper than FSR 1 at the same preset.
   - **Walk, strafe, spin the camera:** no smearing, no wobble or jelly. If the whole picture
     wobbles while still, run once with `BBHOST_FSR_JITTER_SIGN=xy`, then `=x`, then `=y`, and
     note which one fixes it.
   - **Fight a group, roll around them:** look for ghost trails behind enemies, weapons, cloth
     and your coat.
   - **Particles** (lantern, Beast Blood pellet or fire paper, fog, Blood Echoes): look for
     trails behind them.
   - **A lamp warp and a cutscene:** the first frame after the cut is clean.
3. F10: **Performance**, then repeat the still spot and the fight briefly. Turn **reactive
   mask** Off and look at the particles again (more trails expected), then back On. Turn
   **Upscaler jitter** Off once (softer and more aliased expected), then back On.

Pass:
- No crash, a sharp and stable picture at Quality, little ghosting.
- Log lines to look for:
  - `fsr3: FSR 3.1.5 context for scene 2560x1440 -> output 3840x2160 ...`
  - `fsr3: first FSR 3.1 frame ...`
  - `motion: first reactive mask ...`
  - `fsr3: history reset ... a camera cut` near warps and cutscenes only
- The `fsr3:` summaries show `bilinear frames:` only for menus and loads.
- If FSR 3.1 is never used, `fsr3: the scale pass is drawn bilinear this frame: <reason>`
  says why.

### 4. FSR 4.1.1 build and run (new, 15 min)
1. Build the assets once:
   - Get `amd_fidelityfx_upscaler_dx12.dll` from the latest OptiScaler release.
   - From the unpacked tarball, run `./build-fsr4.sh /path/to/amd_fidelityfx_upscaler_dx12.dll`.
   - Note how long it took and its last lines. The exit codes are in
     `tools/fsr4cap/build_assets.sh`'s header: 3 means no Proton, 5 means the DLL's FSR 4.1
     did not run under Proton.
   - The setup window then says "FSR 4.1.1 assets built in ~/.local/share/bbhost/fsr4_411".
2. `bbtest start t1-fsr4 --record`. Choose **FSR 4.1.1** at **Quality**, load your save, and
   repeat part 3's still spot, the fight, the particles and a warp. Then **Performance**.
3. Note the GPU line (`FSR 4.1.1 x.xx ms`) and the FPS, compared with FSR 3.1.

Pass:
- No crash.
- The log has `fsr4: FSR 4.1.1 replay, <int8|fp8> ...` and no `fsr4: ...: FSR 3.1
  upscales instead`.
- The picture is at least as stable as FSR 3.1, with less ghosting.
- If it falls back, the `fsr4:` line says why. Keep the log.

### 5. Where the frame time goes, 1080p -> 4K (new, 10 min)
Stand at one busy, repeatable spot (the same view each time) with **Show Upscaler GPU Time**
on. For each row, wait ~10 s and write down the FPS, `GPU %`, and the whole GPU line (the
presenter's passes plus `scene: ...`). The log's `pass-timer:` line every 10 s has the same
numbers.

| Upscaler / quality (4K output) | FPS | GPU % | scene: MV / reactive / opaque / upscaler / copy (ms) |
|---|---|---|---|
| FSR 1, Native AA (native 4K) | | | |
| FSR 3.1, Quality (1440p scene) | | | |
| FSR 3.1, Performance (1080p scene) | | | |
| FSR 4.1.1, Quality | | | |
| FSR 4.1.1, Performance (1080p scene) | | | |

Frame cap **120**. This shows whether 120 fps is lost in the game's own rendering (GPU % high,
small pass times), in the upscaler (a large upscaler time), or on the CPU (GPU % well under 100).

### 6. Scene/UI split (B, 10 min), FSR 1 at Quality
1. Visit the Hunter's Dream, a lamp menu, the inventory, a cutscene, and an area with messages.
2. At each: F10 > **Native AA**, wait ~2 s, look; then back to **Quality**, look again.
3. `bbtest mark <what>` on: blurry UI at 67%, a black or grey band or border, garbage in a menu
   or cutscene, blocky effects, wrong-sized icons, a hitch on the toggle.

Pass:
- The scene is softer at 67% but the UI and text are sharp, and nothing is corrupt.
- In the log: one `scene-split: render size` line per toggle, and `yebis: restart` only next
  to size changes.
- If you see a band or corruption, run once with `BBHOST_SCENE_SPLIT=0`.

### 7. Motion vectors (C, 10 min), FSR 3.1 at Quality, **Show motion vectors** on
1. Walk and strafe: red/green, even across the screen. Standing still: dark.
2. Spin fast: strong red, no holes.
3. Roll and fight: you and moving enemies show blue.
4. One boss entry.
5. Turn the overlay off.

Pass:
- `motion: first motion pass ... camera valid`, and `camera-cut:` lines only at warps, loads
  and cutscenes.
- If blue never appears, run once with `BBHOST_OBJECT_MOTION=0`.

### 8. FaceGen / vertex explosion (F, 5 min), FSR 3.1 at Quality
shadPS4's "exploding vertices" came from FaceGen GPU writes the CPU never read back. bbhost
imports the game's GPU-visible memory into Vulkan, so it should not happen here. This part
checks that bbhost-vk's changes kept it that way. Optional: `BBHOST_GLITCH=1`.
1. PC enhancements: **Hunter's Dream mirror** on (restart if it was off).
2. Use the mirror and change face shape, hair and the sliders rapidly for ~30 s while rotating
   the preview. `bbtest clip`.
3. Leave, die once, rest at a lamp.

Pass: no exploded or stretched vertices, no spikes, and no "veil" over faces.

### 9. Resolution in the game's own Settings menu (G, 5 min)
The game's own menu (System > PC Graphics > Resolution) used to freeze at the "keep this
resolution?" question, also upstream. F10 always worked. This build waits for the pick list to
close before it changes the size (docs/vk/KNOWN_ISSUES.md).
1. System > PC Graphics > Resolution: pick another size. Try once quickly, and once holding
   the confirm key a moment. The question should appear and take the keyboard, the pad and the
   mouse. Answer **Yes** once and **No** once. `bbtest mark resolution-question`.
2. Pick a size and leave PC Graphics before the question appears: nothing should pop up later,
   and the size goes back after ~13 s.
3. If it still freezes: wait 15 s without input and note whether it recovers. Repeat once with
   `BBHOST_SCENE_SPLIT=0` and once with **Upscaler Off**.

Pass:
- The question works every time.
- The log shows `resolution: waiting for the menu's pick list to close before the change` when
  the gate held the change, then `resolution: asking to keep it` and `answered over PC
  Graphics`.
- A freeze has `resolution:` and `pc-options:` lines that show how far it got. Keep that log.
