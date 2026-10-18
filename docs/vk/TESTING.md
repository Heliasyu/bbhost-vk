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

## test9 checks (one run, ~5 min)

Same as test8 (keep test8's `bbhost.bin` as `bbhost.bin.test8`): FSR 4.1.1 Performance,
4K, 120, sharpness 5/10, AA Auto, motion blur off, at the Cathedral lamp with the camera
still.

1. `BBHOST_AB_SECONDS=30 ./bbtest start --capture t11-lamp-ab`. Stay 3 minutes, and run
   `./bbtest mark "lamp"` about every 30 s.
2. Turn motion blur on for 30 s and move the camera: `./bbtest mark "blur on"`. It must blur.
   Turn it off again.
3. `./bbtest stop`, then send session.txt and bbhost.log. In the log:
   - `motion blur skip on: its two draws copy ... now one copy` should appear about a second
     into the world;
   - the `ab:` lines should show pairs skipped going up;
   - if a `motion blur skip learning ...` line appears instead, it says why.

## test8 checks (one run, ~6 min)

Install test8 (keep test7c's `bbhost.bin` as `bbhost.bin.test7c`) and `cd` into the folder.
Use the same settings as t9: FSR 4.1.1 Performance, 4K, 120, sharpness 5/10, AA Auto,
**motion blur off**. Stand at the spawn lamp with the lamp and the fog in view, and keep the
camera still.

1. `BBHOST_AB_SECONDS=30 ./bbtest start --capture t10-lamp-ab`. Stay 3 minutes (6 halves).
   About every 30 s, run `./bbtest mark "lamp"`: it takes a presented-frame capture.
2. Turn motion blur **on** in the game's settings for 30 s, moving the camera, then
   `./bbtest mark "blur on"`. It must blur as in test7c. Turn it off again.
3. `./bbtest stop`. Send:
   - session.txt (`120 LOCK:`, `a/b (BBHOST_AB_SECONDS): skip on ... skip off ...`);
   - the `render: motion blur skip ...` and `post-chain:` lines from bbhost.log, or the
     whole log.
   - Compare the captures from the on and off halves. They must look identical apart from
     the fog's movement. `ab:` lines in the log say which half each capture fell in.

If anything looks wrong, `BBHOST_MOTION_BLUR_SKIP=0` brings back test7c's behaviour.

## test7c checks (~20 min)

Install test7c the same way as test7b (keep the test7b `bbhost.bin` as a backup, for example
`bbhost.bin.test7b`), then `cd` into the folder. Use the same settings as test7: FSR 4.1.1
Performance, 3840x2160, 120, sharpness 5/10, Anti-aliasing Auto, **Show upscaler GPU time**
on, motion blur off. Stand at the spawn lamp with the lamp and the fog in view and the
camera still. Do not use `--record`.

A. **Batching off (the baseline):** `./bbtest start t9-lamp` for 60 s,
   `./bbtest mark "lamp batch off"`, `./bbtest stop`.
   - In the log: `gpu: submit batching off`, and `render: motion blur 7d668276+e0305cef
     draws into ...` (two or more lines) plus `texture: 7d668276+e0305cef b..` lines. These
     are only data for the next build.
B. **Batching on, same spot:** `BBHOST_SUBMIT_BATCH=1 ./bbtest start t9-lamp-batch` for
   60 s, `./bbtest mark "lamp batch on"`, `./bbtest stop`.
   - In the log: `gpu: submit batching on`.
   - Watch for flashing, black or stale frames, or stutter. Stop at once if you see any
     (`./bbtest mark "flash"` first).
C. **Compare A and B:**
   - session.txt: average fps, 1% and 0.1% lows, `N of M seconds under 120`.
   - The idle split of each, with gpu_gaps.py:
     `for s in t9-lamp t9-lamp-batch; do python3 gpu_gaps.py --idle "$(ls -d ~/.local/share/bbtest/sessions/*-$s | tail -1)/bbhost.log"; done`.
     The `queued` ms/s should fall with batching on (test7: ~60 ms/s). The `gpu idle:`
     line's `batched N jobs/s into M submits/s` shows how much was merged.
   - Also walk once through the Cathedral fog with batching on and look at the fps there.

## test7 checks (~30 min)

Install test7 (the tarball and the `bbtest` beside it), then `cd` into the folder. Use FSR
4.1.1 Performance, Resolution 3840x2160, frame rate 120 and sharpness 5/10. Turn on **Show
upscaler GPU time** in the launcher. In the launcher, set **Anti-aliasing** to **Auto (off
with FSR 4.1.1)**: your saved Off stays Off until you change it. Stand at the spawn lamp.
Do not use `--record`.

A. **The lamp, profiled per pass:** `BBHOST_GPU_PROFILE=3 ./bbtest start t8-lamp-p3`.
   - Look at the lamp and the fog for 30 s with the camera still: `./bbtest mark "lamp"`.
   - Turn away from the lamp for 30 s: `./bbtest mark "away"`.
   - `./bbtest stop`.
   - In the log, check:
     - `graphics: the game's anti-aliasing off (Auto: FSR 4.1.1 upscales)`;
     - `fsr4: sharpening in one tiled pass`;
     - `pass-timer: ... FSR 4.1.1 X ... sharpen Y ms` (expected: sharpen ~0.2 ms, against
       the replay's 0.315-0.35);
     - the `gpu profile:` per-pass lines for the lamp (which post pass grows);
     - `gpu idle:` and `gpu idle late after:` lines.
   - session.txt has `in-game frames (MangoHud): ... 1% low ..., 0.1% low ..., N of M
     seconds under 120`.
B. **Sharpening, A/B (same spot, same sharpness 5/10, the lamp in view):**
   - `./bbtest start t8-lamp-sharp` for 60 s, `./bbtest mark "lamp sharp ours"`,
     `./bbtest stop`.
   - Then the same with the old pass: `BBHOST_FSR4_SHARPEN=amd ./bbtest start
     t8-lamp-sharp-amd`, `./bbtest mark "lamp sharp amd"`, `./bbtest stop`.
   - Compare the FPS, the FSR 4.1.1 time and how sharp it looks.
   - Check whether the lamp flicker at 0.5 is gone with ours.
C. **Idle gaps:**
   - `./bbtest start t8-gaps` for 60 s looking forward, `./bbtest mark "forward"`,
     `./bbtest stop`, then (gpu_gaps.py ships beside bbtest)
     `python3 gpu_gaps.py --idle "$(ls -d ~/.local/share/bbtest/sessions/*-t8-gaps | tail -1)/bbhost.log" --exe "$HOME/BloodBorne PC/bbhost-linux-v0.2.18/bbhost"`. Read which kind dominates: late, queued or before present.
   - Then the experimental split: `BBHOST_SUBMIT_SPLIT=32 ./bbtest start t8-split`, the same
     view, `./bbtest mark "forward split"`, `./bbtest stop`. Watch for flashing or black
     frames and stop at once if you see any. Compare the `gpu idle:` ms/s, the `splits N/s`
     and the FPS.

## test6 checks (~20 min)

From the test6 folder (`cd` into it); FSR 4.1.1 Performance, Resolution 3840x2160, the
same spot as t6 (spawn), the camera untouched looking forward for 30 s, then at the ground
for 30 s. Mark each (`./bbtest mark "forward"`, `./bbtest mark "ground"`). No `--record`
(the recorder takes GPU time).

A. **The FPS lines:** `./bbtest start t7-fsr4p`.
   - `present: 300 frames in N ms (X fps shown; ...)` agrees with MangoHud's on-screen FPS.
   - `pass-timer: GPU per pass over the last 10.0 s (N upscaled frames, X a second - not
     the FPS shown)`.
   - The line has no `reactive` or `opaque`: FSR 4.1.1 no longer gets them (expected:
     about 0.1 ms less than test5's).
   - An `fsr3: 3000 dispatches (3000 FSR 4.1.1, 3000 written directly, ...)` line every
     3000 frames.
   - After `./bbtest stop`: session.txt `in-game fps shown:` and the MangoHud CSV covering
     the whole session.
B. **Sharpening's cost (0.315 ms in t6):** the same with sharpness 0:
   `./bbtest start t7-fsr4p-sharp0`. Note the FPS looking forward.
C. **Game anti-aliasing (t6's "noaa" run still had it on: options-before and -after say
   `anti_alias = "On"`):** in the game's settings set Anti-aliasing Off, check
   `~/.config/bbhost/bbhost-options.toml` says `anti_alias = "Off"`, then
   `BBHOST_GPU_PROFILE=3 ./bbtest start t7-fsr4p-noaa-p3`. The profile shows which 4K
   pass goes (dc609f83+da1faca9 is the candidate) and what it cost. Set it back On after.

## test5 checks (~40 min)

As before: set the upscaler, quality and Resolution (3840x2160) **before** launching. Use
test4's spot, plus a spot with your character and an NPC or enemy moving in view.

A. **FSR 3.1 Quality, shimmer check:** `bbtest start t5-fsr3 --record`.
   - Look at your character (hair, coat, cloth) standing still and walking.
   - In the log: the `motion:` summaries say `scale 8.88889 -5` (or another scale with
     the scene's aspect), not `1 1`. Any `motion: velocity draw PS ... not a packing scale`
     lines are reads that were skipped.
   - If the shimmer is still there, narrow it down one switch per run (in this order):
     1. Upscaler settings: **Reactive mask off** (FSR 3.1 uses it, FSR 4.1.1 does not).
     2. **Sharpness 0**.
     3. `BBHOST_FSR_DIRECT_TARGET=0 bbtest start t5-copy`.
     4. `BBHOST_OBJECT_MOTION=0 bbtest start t5-noobj` (camera motion only).
     5. `BBHOST_OBJECT_MOTION_MIN=3 bbtest start t5-min3`.
     Note which one makes it go away. For a still character, a `bbtest mark` with
     `BBHOST_FSR_DUMP=1` writes the reactive mask (`4-reactive.png`). If the character
     lights up there, the mask is the cause.
B. **Motion-vector view, characters:** Upscaler settings > **Show motion vectors** on (FSR
   3.1 or 4.1.1 chosen). Red and green are the motion (8 render pixels a frame is full).
   - Bright blue: the game's object velocity is used (a moving enemy or your character
     walking while the camera holds still).
   - Dim blue: covered by the game's velocity target, but the camera's motion is kept (a
     still object).
   - Darkened: no camera motion.
   - Expected: a character who moves on screen against the camera is bright blue inside
     and has no blue halo around it. Still scenery has no bright blue. Screenshot it with
     `bbtest mark`.
C. **FSR 4.1.1 Quality** at 3840x2160: `bbtest start t5-fsr4 --capture`.
   - `fsr4: FSR 4.1.1 writes the scale pass's 3840x2160 target directly`.
   - The `pass-timer:` line has no `copy`, and the `fsr3:` summary says `N written directly`.
D. **FSR 4.1.1 Performance** (1920x1080 -> 3840x2160, the goal): `bbtest start t5-fsr4p`.
   - `fsr4: FSR 4.1.1 replay, FP8 t2160_m0 output 3840x2160 (scene 1920x1080 -> 3840x2160)`.
   - Note FPS (goal: a 120 hold), 1% low, `gpu busy:` and `FSR 4.1.1 x.xx ms`. FSR 4.1.1's
     cost is mostly at the output size, so it costs about the same as at Quality (~2.1 ms).
     The saving is in the scene.
   - If 120 is not held, also try sharpness 0: that skips FSR 4.1.1's RCAS pass and its
     extra 3840x2160 image.

Not to use: `BBHOST_SUBMIT_EVERY_JOB=0` (test4 run C: 64 fps, flashing effects).

## test4 checks (~40 min)

Same as test3: set the upscaler and the Resolution **before** launching. Stand at the same
busy, repeatable spot for every run (test3's spot), wait ~20 s, then `bbtest mark`. Keep
**Show Upscaler GPU Time** on. For each run, note MangoHud's FPS and the log's last
`gpu busy:`, `pass-timer:` and `fsr3:` lines.

A. **FSR 3.1 Quality, 3840x2160** (test4 defaults):
   `BBHOST_MOTION_PROBE=1 bbtest start t4-fsr3 --capture`
   - `fsr3: FSR 3.1 writes the scale pass's 3840x2160 target directly`, and in the
     summaries `N written directly, 0 copied`. The `pass-timer:` line has no `copy` any more.
   - The picture is as correct as in test3 (no band, normal brightness).
   - Camera: `motion: scene constants found at ...` early in the world, then the `fsr3:`
     summaries show `resets` near 0 outside warps and loads (test3: 1582 of 3000). Few or no
     `jitter: paused` / `camera-cut: no scene camera` lines while you play.
B. **The same with the copy** (the direct write's A/B):
   `BBHOST_FSR_DIRECT_TARGET=0 bbtest start t4-copy`. Expected: about 0.2 ms more GPU
   time a frame, and the `copy` figure is back.
C. **Merged submissions** (the gaps between command buffers, ~9% of the GPU's time):
   `BBHOST_SUBMIT_EVERY_JOB=0 bbtest start t4-merge`. Result: REJECTED (64 fps, 1% low
   14.5, flashing effects). The default stays a submit per job.
D. **The cheaper FSR inputs:** in A's setup, turn the reactive mask off, then sharpening
   to 0 (Upscaler settings). Note the FPS and the `FSR 3.1` / `reactive` figures for each.
E. **Where the GPU time goes** (the game's passes at 4K; FPS here is not representative):
   `BBHOST_GPU_PROFILE=3 bbtest start t4-profile` at A's settings, ~30 s at the spot.
   The log's `graphics=... compute=... <pass>=<ms>(<count>)` lines name the heaviest
   passes (YEBIS post-processing and UI at 3840x2160, the scene at 2560x1440).
   Then `BBHOST_GPU_PROFILE=2 bbtest start t4-gaps` for the time between draws, charged to
   what was recorded there (barriers, copies, fills).
F. **FSR 4.1.1** (see "FSR 4.1.1 assets" below), then FSR 4.1.1 Quality at 3840x2160:
   `bbtest start t4-fsr4 --capture`. Look for `fsr4: FSR 4.1.1 assets in ...`, then
   `fsr4: FSR 4.1.1 replay, fp8 ...` on the RX 9070 XT. The `fsr3:` summary counts
   `N FSR 4.1.1`, and `pass-timer:` shows `FSR 4.1.1 x.xx ms`.

### FSR 4.1.1 assets (once, ~10-20 min, on the PC)
Nothing of AMD's ships with bbhost-vk. The assets are built on your PC from your own DLL.
1. Get OptiScaler **v0.9.4** (github.com/optiscaler/OptiScaler/releases/tag/v0.9.4) and
   unpack it. Use its `amd_fidelityfx_upscaler_dx12.dll`: version 4.1.1.2740, about
   27 MB. A >30 MB file with the same version number has been reported not to work. Keep
   `amd_fidelityfx_loader_dx12.dll` (2.3.0.2740) next to it. This DLL exports the API itself,
   so the loader is only a fallback.
2. On the PC you need: python3, `spirv-tools` (`sudo pacman -S spirv-tools python` on
   CachyOS), and a Proton that starts FSR 4.1: GE-Proton 10-34 or newer, Proton-CachyOS 11,
   or Proton - Experimental, installed in Steam (`compatibilitytools.d` or `steamapps/common`).
   The tarball's `tools/fsr4cap/bin` holds the prebuilt dxil-spirv and fsr4cap.exe, so
   MinGW, cmake and network are not needed. Unprivileged user namespaces must be allowed
   (CachyOS default). Bazzite's `bwrap: No permissions to create a new namespace` is
   this check failing.
3. From the unpacked test4 tarball:
   `./build-fsr4.sh ~/Downloads/OptiScaler_0.9.4/amd_fidelityfx_upscaler_dx12.dll`
   (`PROTONPATH=<a Proton folder> ./build-fsr4.sh ...` picks one Proton).
   It records 40 sizes on RDNA4 (FP8 and INT8). Work files go to `~/.cache/bbhost/fsr4cap`
   (`BB_FSR4CAP_CLEAN=1` removes them after a successful build).
4. The assets land in `~/.local/share/bbhost/fsr4_411/` (bbhost reads them there, or
   wherever `BBHOST_FSR411_DIR` points). Check that `t1080_m0/initializer.bin` or
   `fp8/t1080_m0/initializer.bin` exists in that folder.
   Exit codes: 2 the DLL is not 4.1.x, 3 no Proton, 4 tools missing, 5 FSR 4.1 did not start
   under any Proton, 7 a loader is needed. Keep the last 30 lines if it fails.
5. test3's log said `fsr4: no FSR 4.1.1 assets in /home/helias/.local/share/bbhost/fsr4_411`.
   After the build, that line must be gone.

## test3 checks (~20 min)

Start with the upscaler and the Resolution you want to test **already set**. The display
buffers are now sized to the output when the game starts with an upscaler preset, so change
the Resolution only between runs. A live change runs without the upscaler until the next
start, and the log says `scene-split: not split at ...`.

Start with `BBHOST_FSR_DUMP=1 BBHOST_MOTION_PROBE=1 bbtest start test3 --capture`, set to
FSR 3.1 Quality at 3840x2160.
1. The picture fills the screen at normal brightness, with no grey band. Run `bbtest mark` in
   the world: it also writes an `fsr-*/` folder of FSR stage PNGs into the session's `frames/`.
2. In the log, look for:
   - `resolution: display buffers 3840x2160, the output's`;
   - `fsr3: output 3840x2160 at 0,0 in the scale pass's 3840x2160 target`;
   - `motion: scene constants found at ...` (or the `motion-probe:` lines if it is not
     found);
   - `fsr3: ... N resets` well below the dispatches;
   - `jitter: paused` only while there is no camera.
3. Compare FPS and the `gpu-wait=` figure with FSR 1 at the same Resolution. gpu-wait is the
   host waiting for the GPU: high means GPU-bound.
4. Repeat at 5120x2160 with FSR 3.1 (test2's 5k run was FSR 1).

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
