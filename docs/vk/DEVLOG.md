# bbhost-vk development log

A running log: one dated entry per phase or work session (times are
America/Chicago). Each says what was done, the decisions and why, what was
tried and did not work, what is still open, and the test results.
The plan is [PLAN.md](PLAN.md); how sessions on the player's PC are recorded
is [TESTING.md](TESTING.md).

---

## 2026-10-18 (Sun), Phase 0: Groundwork (branch `phase-0-groundwork`)

Base: `main` = upstream bbhost v0.2.18 (c421ea0) + the 120 fps option
(e9d124d) + fork docs; tag `v0.2.18-vk.0`. Work done on the build box
(Debian 13, clang 19.1.7, glibc 2.41), where the game cannot run.

### Done
- **Reproducible build** (`packaging/vk/build.sh`). Found how the shipped
  `v0.2.18-vk.0` binary was made: `CI_PORTABLE=1
  CI_CMAKE_ARGS=-DBBHOST_RELEASE=ON tools/ci_build.sh` on this box
  (`build-ci/build/CMakeCache.txt`: RelWithDebInfo, BBHOST_RELEASE=ON,
  `-static-libstdc++ -static-libgcc -fuse-ld=lld`; prefix with
  Vulkan-Headers 1.4.350, SDL 3.4.16, ffmpeg 7.1.1). The stripped
  `bbhost.bin` is exactly `objcopy --strip-debug` of it (as
  `tools/package_linux.sh` does): byte-identical SHA-256. build.sh wraps
  that, inits the submodules, strips, writes the SHA-256 and a BUILDINFO.txt,
  and says when the host is not the reference Debian 13 / clang 19.
- **FSR-Vulkan** (FireBurn, MIT) as the submodule `third_party/FSR-Vulkan`
  at c64f093, built by `cmake/fsr_vulkan.cmake`: `ffx-vulkan::portable`
  (FSR 3.1.4) and `ffx-vulkan::fsr3-vk-backend-3.1.5` with the checked-in
  SPIR-V tables. Not called. `upscaler_test` and the start-up log check
  that all 11 FSR 3.1.5 passes' modules are embedded (11 of 11).
  THIRD_PARTY_NOTICES.md, README credits; the Linux package now ships the
  MIT notices; CI checks out submodules.
- **Settings**, stored and logged only: `upscaler`, `upscale_quality`,
  `upscale_sharpness`, `debug_motion_vectors`, `debug_upscaler_time` in
  `bbhost-options.toml` (src/host/options.cpp, settings.h); F10 screen
  (new UPSCALING header after Resolution); in-game System menu (two new
  sections); setup window (two combos, a slider, two checkboxes).
  `src/host/upscaler_presets.h` is the one place the ratios and the render
  size are computed.
- **Debug tooling**: `GpuTimer` (src/host/gpu_timer.*), Vulkan timestamps
  around the presenter's passes (`present`, `upscale` or `one pass`), read a
  frame late without waiting, shown under the FPS counter and logged every
  300 frames; the motion-vector hook (logs once); the presented-frame capture
  (src/host/frame_capture.*).
- **bbtest** (`packaging/vk/bbtest`, docs/vk/TESTING.md).

### Decisions and why
1. **Upscaler "Off" draws as FSR 1 in Phase 0.** The brief says Off/FSR 1
   must be exactly stock, and stock bbhost always uses FSR 1 when it renders
   smaller than the window. Making Off mean bilinear or native now would
   change the picture. Later phases can give Off its own meaning (the plan
   for that: Off = render at the window size, no upscale); the line help
   says "not used yet" today.
2. **Defaults change nothing**: upscaler FSR 1, quality Native AA, sharpness
   0.8. 0.8 is chosen because FSR 3's sharpness runs 0..1 and stock FSR 1's
   RCAS uses 0.2 stops, and `rcas_stops = 1 - sharpness` maps 0.8 to exactly
   that. Sharpness is stored in tenths (eleven steps) so the in-game slider
   (0..10) and the toml agree without conversion.
3. **Two in-game sections, not one.** The game's option section takes one
   pick list and only on its last row (it is translucent and opens over the
   rows below); the left/right choice row holds two entries at most. Upscaler
   (4 choices) and Quality (5) both need pick lists, so: **PC Upscaling**
   (Show Motion Vectors, Show Upscaler GPU Time, Upscaler) shaped like PC
   Enhancements, and **Upscaling Quality** (Sharpness slider, Quality) shaped
   like Steam Deck. System rows 110014/110015, sprite depths 391/392, title
   ids 125020/126020, generator version 9.
4. **The new sections are made defensively.** They cannot be tested here (no
   game dump on the build box). They are generated after the stock eight,
   inside their own try/catch, with a check that depths 391/392 are free;
   if anything fails the stock movie is kept, a `vk-sections` file beside
   the stamp records it, and the System menu leaves the two rows out. They
   are also left out when the player's own optionsetting.gfx (paths.mods)
   overrides the generated one. So the worst case is "two System rows
   missing", not a crash.
5. **Frame capture is opt-in** (`BBHOST_VK_CAPTURE=1`). It needs
   TRANSFER_SRC on the swapchain images; stock bbhost does not ask for it,
   so it is only added when the variable is set. Triggers: Shift+F12 (only
   with the variable; otherwise Shift+F12 stays F12), a `capture.request`
   file in the capture folder, or a timer. A file trigger was chosen over a
   signal (SIGUSR2) because a signal lands on any thread, including the
   game's, and bbhost runs guest code natively; polling one stat() every
   100 ms from the window pump is harmless. The copy is recorded after the
   image is in PRESENT_SRC and read after the next fence wait, then written
   by a thread of its own (8-bit BGRA/RGBA and 10-bit formats).
6. **GPU timer records nothing while its toggle is off**, so the command
   buffer is stock; it gets its own query pool (gpu_busy and fsr.cpp have
   theirs).
7. **Not labelling FSR-Vulkan's "source-v07 FSR4" provider as FSR 4.1.1**,
   and not building it. FSR 4.1.1 comes in Phase 4 from the player's own DLL.
8. **Windows**: `BBHOST_FSR_VULKAN` defaults OFF on WIN32 until it is tried there.
9. **bbtest**: screenshots prefer `gpu-screen-recorder -w <monitor> -o x.png`
   (that monitor's own pixels), falling back to `spectacle -b -n -f` cropped
   with ImageMagick to the monitor's rectangle from `kscreen-doctor -j`
   (scaled by image width / logical desktop width, which handles the 150%
   monitor). MangoHud gets `read_cfg,...` so goverlay's config is kept. The
   options file copies drop the `[account]` section (the token). Replay via
   SIGUSR1 to a `gpu-screen-recorder -r 30` started with the session.
10. **No release, no tag.** main untouched; PR into main.

### Tried, did not work / dead ends
- `llvm-strip`, `strip` and `llvm-objcopy --strip-debug` all give binaries
  different from the shipped one; only GNU `objcopy --strip-debug` matches.
- A first frame-capture memory-type loop over a braced list of flags did not
  compile (mixed int / VkMemoryPropertyFlags deduction); replaced by an array.
- Starting the built binary on the box with an empty dump only reaches the
  "no eboot" stage, so the start-up `upscaler:` log line and the menus could
  not be seen running here.

### Open issues / risks for the player's test
- The in-game System sections are untested against a real dump. If the
  generator rejects them, PC Upscaling/Upscaling Quality are absent from
  System (log: `menu assets: the PC Upscaling sections were not made`), and
  F10 + the setup window still have every setting. Regenerating the menu
  assets (generator 9) happens once at the first start.
- The GPU timer and frame capture are untested on a real GPU (no GPU here).
  Both are inert unless switched on.
- The F10 screen gains six rows; it scrolled already.
- Upstream's updater still compares against droogie/bbhost's releases
  (unchanged from v0.2.18-vk.0); don't accept an in-app update, it would
  replace the fork's binary.

### Test results (build box)
- `packaging/vk/build.sh`: configure, build and ctest pass: 43 tests, 0
  failures (20 skip without game files, as upstream). `upscaler_test`:
  presets (3840x2160 Performance -> 1920x1080, Balanced -> 2259x1271, ...)
  and "FSR 3.1.5 embedded SPIR-V modules: 11 of 11".
- `bbtest` dry run with a stand-in `./bbhost`: start (foreground and
  `--detach`), status, mark (log line, `capture.request`), clip without
  `--record` (says so), stop, bundle with session.txt / marks.log.
- Player's PC test: pending (checklist in TESTING.md, "Phase 0 test").
