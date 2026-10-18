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
