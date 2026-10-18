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

## Phase 0 test (5 min)
1. Install the Phase 0 `bbhost.bin` into the game folder (keep the old one:
   `mv bbhost.bin bbhost.bin.vk0` first).
2. `bbtest start p0`. In the setup window: set **Upscaler**, **Upscaling
   quality**, **Upscaler sharpness** and the two debug checkboxes to
   non-default values, press Play.
3. In game, F10: the **UPSCALING** rows - flip each one (Upscaler through all
   four, Quality through all five, Sharpness, both debug toggles). Turn on
   **Show Upscaler GPU Time** once and check the GPU line under the FPS
   counter (turn on the FPS counter too).
4. Esc -> System: **PC Upscaling** and **Upscaling Quality** - flip each row
   there too.
5. Load your save, look around for a minute, quit from the menu.
6. `bbtest stop` if the session did not end by itself.

Pass: every option is in `options-after.toml` with the value last set; the
log has `upscaler:` lines for each change and no new errors; the picture looks
identical to stock (Phase 0 changes nothing on screen).

## Pending combined test session (Phase 0 to Phase 3, online, FaceGen, resolution menu, ~75 min)
The Phase 0, 1, 2 and 3 checks and the online check in one sitting, with
the Phase 3 binary (`/workspace/bbhost-vk-builds/phase3/bbhost.bin` on the
build box; check its SHA-256 against `bbhost.bin.sha256`). The Phase 2
binary's parts still apply to it; with FSR 3.1 the Phase 2 shimmer of part C
is now resolved by FSR (part D). Install it over `bbhost.bin` (keep the
old one: `mv bbhost.bin bbhost.bin.vk0` first). No `--capture` for part A.

### A. Phase 0 checks (5 min): `bbtest start p0`
1. In the setup window: set **Upscaler**, **Upscaling quality**,
   **Upscaler sharpness** and the two debug checkboxes to non-default
   values, then back to **Upscaler FSR 1** (or Off) and **Native AA**; Play.
2. In game, F10: flip every **UPSCALING** row (Upscaler through all four,
   Quality through all five, Sharpness, both debug toggles), turning on
   **Show Upscaler GPU Time** once with the FPS counter on. End on
   **FSR 1** (then once on **Off**) with **Native AA**.
3. Load your save, look around a minute, quit from the menu.

Pass: every option is in `options-after.toml` with its last value; `upscaler:`
lines for each change, no new errors; with Off / FSR 1 at Native AA the
picture looks exactly like stock (the log says `scene-split: render size ... (not
split - the game's own bytes at every site)`, no `scale pass` lines).

### B. Phase 1 checks (15 min): `bbtest start p1` (add `--record` if you can)
1. Setup window: **Upscaler FSR 1**, **Upscaling quality Quality (67%)**,
   your usual Resolution. Play, load your save.
2. Visit each spot below. At each: look for a few seconds, then F10 ->
   Upscaling quality **Native AA**, wait ~2 s (the change waits for the menu
   to settle), close F10, look; then back to **Quality (67%)**, look again.
   - the Hunter's Dream (outdoors, the doll, the workshop);
   - a lamp menu (the lamp's own menu and its sub-menus);
   - the inventory / item screens (item icons, descriptions);
   - a cutscene (any that you can trigger: a boss intro, or the Dream's
     first visit);
   - an area with messages and bloodstains (read one of each).
3. `bbtest mark <what>` whenever something looks wrong: UI blurry at 67%,
   a black or grey band or border, garbage in a menu or cutscene, blocky
   effects (fog, bloom, depth of field), icons missing or wrong-sized,
   message text unreadable, a hitch on the toggle.
4. Quit from the menu.

Pass: at 67% the 3D scene is softer but the UI, text and icons are as sharp as
at Native; nothing is corrupt; no crash. In the log: one `scene-split: render
size` line per toggle, `the graphics manager's scene is ...` without
`not the ... asked for`, `scale pass ... drawn`, `heap:` with free space left,
and `yebis: restart` lines only next to size changes (none while just playing). If 67% shows a band or corruption,
run once with `BBHOST_SCENE_SPLIT=0` to confirm it is the split.

### C. Phase 2 checks (15 min): `bbtest start p2 --record`
1. Setup window: **Upscaler FSR 3.1**, **Upscaling quality Quality (67%)**,
   **Show motion vectors** on. Play, load your save (somewhere with enemies,
   e.g. Central Yharnam). The 3D scene may shimmer slightly: the jitter is
   on and nothing resolves it until Phase 3 (expected).
2. With the motion-vector overlay on, do each for ~10 s and `bbtest clip`
   after each (or `bbtest mark <what>` if it looks wrong):
   - walk forward, then strafe: the world turns red/green evenly, stronger
     at the sides and near the floor; standing still: dark/black;
   - spin the camera fast both ways: strong red, no blocky holes;
   - roll and quickstep: the character shows blue (object motion);
   - light/close the lantern; use a Beast Blood pellet (particles, glow);
   - fight a group of enemies: moving enemies and weapons blue;
   - one boss fight (its arena entry and camera).
3. F10: Show Motion Vectors off; look ~10 s at a detailed area; then quit
   from the menu. Optional: one run with `BBHOST_JITTER=0` for comparison of
   the shimmer.

Pass: no crash, no change at FSR 1 / Native (part A); the overlay follows the
camera and shows dark when still. In the log: `motion: scene constants
found ...` (with the render size, not `the output's`), `motion: the scene
depth is ...`, `motion: first motion pass ... camera valid`, `jitter:
Halton(2,3), 18 phases`, a handful of `camera-cut:` lines (lamp warps,
loading, cutscenes; not one per roll), and the `motion:` summaries
(velocity draws per frame, `after the scale pass` should be 0). If blue
never appears on enemies, run once with `BBHOST_OBJECT_MOTION=0` and
compare.

### D. Phase 3 checks (20 min): `bbtest start p3 --record`
Resolution 3840x2160 (4K), **Show FPS** and **Show Upscaler GPU Time** on,
MangoHud/goverlay as usual (it should keep working).
1. Setup window: **Upscaler FSR 3.1**, **Upscaling quality Quality (67%)**,
   **Upscaler sharpness** at its default, **Upscaler reactive mask** On.
   Play, load your save (Central Yharnam or anywhere with enemies).
2. Look for ~10 s each, `bbtest clip` after each, `bbtest mark <what>` when
   something looks wrong:
   - stand still at a detailed spot (fences, railings, roof tiles, hair,
     coat fringes): stable, no crawling or shimmer, sharper than FSR 1 at
     the same preset;
   - walk and strafe, spin the camera: no smearing of the world, no
     wobble/jelly (if the whole picture wobbles or doubles when still, run
     once with `BBHOST_FSR_JITTER_SIGN=xy`, then `=x`, `=y`, and note which
     fixes it);
   - fight a group of enemies, roll around them: ghost trails behind
     enemies, weapons, cloth and the character's coat;
   - particles: the lantern on/off, a Beast Blood pellet or fire paper,
     fog, Blood Echoes collecting: trails or smears behind them;
   - a lamp warp and a cutscene: the first frame after the cut is clean
     (no blend of the old picture).
3. F10: **Upscaling quality Performance (50%)**: repeat the still spot and
   the fight briefly. Then **Upscaler reactive mask** Off: the particles
   again (more trails expected), back On.
4. GPU time and frame rate: note the FPS and the `FSR 3.1 x.xx ms` on the
   GPU line at Quality, Performance, and with **Upscaler FSR 1** at
   **Native AA** (the native comparison). Target: 120 fps at Quality or
   Performance where native does not reach it.
5. Once **Upscaler FSR 4.1.1**: it should look like FSR 3.1 (the log says
   it uses FSR 3.1 until Phase 4). Back to FSR 3.1; quit from the menu.

Pass: no crash; a sharp, stable picture at Quality; little ghosting; FSR 1
and Native unchanged from stock. In the log: `fsr3: FSR 3.1.5 context for
scene 2560x1440 -> output 3840x2160 ... MiB, made in ... ms`, `fsr3: first
FSR 3.1 frame ...`, `motion: first reactive mask ...`, `fsr3: history
reset ... a camera cut` near warps/cutscenes (not every roll), the `fsr3:`
summaries (`bilinear frames:` should be only menus/loads), and `fsr3: the
dispatch takes x.xx ms`. If FSR is never used, the `fsr3: the scale pass
is drawn bilinear this frame: <reason>` line says why; `BBHOST_FSR3=0`
gives the bilinear copy for comparison.

### E. Online check (5 min), with FSR 3.1 at Quality
Online must work exactly as with upstream bbhost (same servers, sign-in and
networking; bbhost-vk does not touch them).
1. Start as usual and sign in (the same account flow as upstream); the log
   shows the sign-in succeeding and the user agent `bbhost/v0.2.18-vk...`.
2. In an online area: other players' messages and bloodstains are visible
   (read one, touch one).
3. Either put down a summon sign (Beckoning Bell or Small Resonant Bell) or
   ring a bell, and see it is accepted (a co-op/summon round trip if
   someone answers).

Pass: sign-in, messages, bloodstains and the bell/sign behave as with
upstream bbhost. If not, note it and run once with upstream's binary to
compare.

### F. FaceGen / vertex-explosion check (5 min), with FSR 3.1 at Quality
shadPS4's "exploding vertices" on faces came from GPU writes (FaceGen)
the CPU never read back. bbhost imports the game's GPU-visible memory into
Vulkan, so it should not happen; this checks bbhost-vk's changes kept that.
Optional: start with `BBHOST_GLITCH=1`.
1. PC enhancements: **Hunter's Dream mirror** on (restart if it was off).
2. In the Hunter's Dream workshop, use the mirror (Put on Disguise) to open
   the appearance editor. For ~30 s change face shape, hair, and other face
   sliders rapidly, rotating the preview; `bbtest clip`.
3. Leave the editor, die once (anywhere), then rest at a lamp.

Pass: no exploded or stretched vertices, no spikes, and no "veil" over
faces (yours or NPCs') in the editor, after the death or at the lamp.

### G. Resolution change in the game's own Settings menu (5 min)
A known issue (docs/vk/KNOWN_ISSUES.md), also seen on upstream bbhost. This is
Bloodborne's own menu (System > PC Graphics > Resolution), not F10.
1. In the game: System > PC Graphics > Resolution, pick another size,
   confirm it. When the "keep this resolution?" question appears, try the
   keyboard (arrows, Enter), the pad, and a mouse click; `bbtest mark
   resolution-question`.
2. If the menus stop responding, wait 15 s without touching anything: does
   the question close by itself (the size goes back) and do the menus respond
   again? Note which.
3. Quit, start again with `BBHOST_SCENE_SPLIT=0`, and repeat steps 1-2.
4. Repeat once with **Upscaler Off**, and once picking the size *slowly*
   (wait 3 s on the pick list before confirming).

Pass/notes: whether input works on the question, whether it recovers after
~13 s, and whether `BBHOST_SCENE_SPLIT=0` or Upscaler Off changes anything.
The log's `resolution:` lines (asking, answered, no answer in 10 s) and
`yebis: restart` show how far it got.
