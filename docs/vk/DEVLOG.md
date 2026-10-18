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
- Phase 0 binary: built by `packaging/vk/build.sh` from ba5082a
  (`v0.2.18-vk.0-9-gba5082a`), stripped, needs GLIBC_2.38, SHA-256
  `571c7ebcc82271cc04b47676e40db6800f0b17db9fc405711bc03cf22e1dc5b8`
  (on the build box at /workspace/bbhost-vk-builds/phase0/; not released).
- Player's PC test: pending (checklist in TESTING.md, "Phase 0 test").

---

## 2026-10-18 (Sun), Phase 1: Scene/UI resolution split (branch `phase-1-scene-ui-split`)

Base: `phase-0-groundwork` (PR #1, unmerged); this branch's PR is stacked on
it. Work done on the build box; there is no game dump here, so nothing below
ran against the game yet.

- **Plan change (2026-10-18):** `main` f813313 added Phase 7 (FSR 4 INT8 for
  Steam Deck/RDNA2-3, DLSS, XeSS) and Phase 8 (Windows release) to PLAN.md at
  the player's request; Phase 1 keeps the upscaler interface
  backend-agnostic (`ScenePass` in host/upscaler.h) so they plug in later.

### Findings
- **The scale pass** is the fullscreen draw with pixel shader `ccbf44a6`
  (IFreemz's `HdrSceneCopy`): it copies the HDR scene (image slot 0) into the
  target the game's post-processing runs on. With the scene and the output at
  one size it is a 1:1 copy; with the decoupled-UI patches it is the
  game's own upscale. In bbhost it is a pipeline `VS+ccbf44a6` in
  host/render.cpp.
- **What sizes the scene** is Kyo's "Decoupled UI" patch set for 1.09
  (shadps4-emu/ps4_cheats `PATCHES/Bloodborne.xml`, 640x360 to 2560x1440).
  Decoded, its 24 sites are (Binary Ninja addresses, 0x400000 base):
  - 0x2697aea/af0: the graphics manager's scene context (G-buffer, depth,
    HDR lighting) at construction;
  - 0x26f8ce1/ce7, 0x26f93d8/3df, 0x26f99db/9e2: the contexts made on demand
    (the scene's helper targets);
  - 0x26c6624/662e, 0x26c6760/676a and the two branches 0x26c65f6, 0x26c6735:
    the resize slot sub_26c6540, so a resize re-makes them at the scene's size;
  - 0x269c7d5: the resize listener stores the render size (a jump to a cave);
  - 0x26c3af3: Initialize's colour is always DefRenderTarget;
  - 0x25d818d: YEBIS's viewport from the output entry (+0x80/+0x84);
  - 0x125b1a3/1aa/1d6/1de and 0x26c3de4/df0: the half- and quarter-size
    targets from the scene's size, not the output's;
  - 0x1258f2c: the output-size query returns the output entry (a cave call).
  Everything else - post-processing after the scale pass, Scaleform, the
  display buffers, DefDepthStencil - stays at the output size, which is what
  live_resolution already sizes.
- Kyo's bytes are the same at every size except the immediates (width,
  height) - `tests/scene_split_test.cpp` checks our encoder byte for byte
  against all four of his sizes.

### Done
- `src/engine/scene_split_sites.h`: the sites as a table with the render size
  as its parameter (no dependencies, unit-tested); `render_size()` = output /
  preset ratio, rounded down to a multiple of 8, at least 64, never above the
  output; the bilinear S# rewrite.
- `src/engine/scene_split.cpp`: keeps the load-time bytes of every site, logs
  them, checks the branch sites are short Jccs and 0x26c3af3 is the game's
  `74 36` or bbhost's `75 11`, refuses otherwise; the two cave targets are
  host stubs within rel32 reach (`engine_alloc_exec_near`), not eboot data.
  `BBHOST_SCENE_SPLIT=0` never splits.
- `live_resolution` sizes two things at its quiet point: the output (as
  before) and the scene. A scene change writes the sites before the GX
  resize broadcast; the on-demand contexts move from the old scene size to
  the new; the graphics manager's scene size is read back and logged; an
  unfit change is rolled back. The start applies the scene's size at load,
  before the graphics manager is built. Changing Upscaler or Upscaling
  quality (F10, System, setup window) asks for the scene alone after the
  usual 1.5 s menu-settle delay.
- The scale pass is drawn as a plain bilinear, clamped, no-mip, no-aniso copy
  while split (the S# is rewritten before the sampler is made). FSR 3.1
  replaces it in Phase 3.
- Logging for the test session: `scene-split: render size WxH for an output of WxH`,
  `scene-split: scale pass ... drawn` (once per size, then every 3000 flips),
  `heap: render-target heap ...` before and after each change,
  `yebis: restart N ...` for each YEBIS shutdown/restart and its dropped
  frees, and each site's original bytes at load.
- Help texts (setup window, F10, System) say what the options now do.

### Decisions and why
1. **Output = the Resolution setting; render = output / preset ratio.**
   Output-size work (post, UI, display) is what Resolution always meant, and
   the preset is the one control that should cost or save GPU time.
2. **Split only when Upscaler is not Off and the preset is not Native AA.**
   Otherwise every site has its load-time bytes and the scale pass is
   untouched - the same code bytes and the same draws as stock, so Off and
   Native 100% are bit-identical to stock by construction (nothing is
   written, `code_write` skips equal bytes).
3. **Host stubs instead of Kyo's caves.** His caves overwrite eboot rodata
   at 0x4d3bad4/0x4d3baf0; bbhost's code pages are its own, and a stub near
   the code avoids guessing whether that rodata is unused.
4. **0x26c3af3 gets `EB 36`, not only `EB`.** bbhost's both-sides patch
   rewrites the same branch to `75 11`, so the displacement has to go back
   to the game's too.
5. **Bilinear by S# rewrite, not a new shader.** The game's own pass stays;
   only its filter changes, so HDR, the colour format and post are as they
   were. No HDR, motion-blur or online changes; nothing touches the
   presenter, so goverlay/MangoHud see the same swapchain as before.
6. **Rescale-only requests reuse the resolution path** (same quiet point,
   same settle delay) instead of a second mechanism.

### What the other projects taught (no code copied)
- **IFreemz** (shadPS4-Bloodborne-DLSS-FSR, GPL-2.0): the name and hash of
  the scale pass (`HdrSceneCopy`, `ccbf44a6`) and the idea that the game's
  own decoupled-UI path is the place to hook an upscaler.
- **Kyo** (patches in shadps4-emu/ps4_cheats): the site list itself;
  bbhost-vk re-encodes it from the render size (facts about the 1.09 eboot,
  not code).
- **bbport** (deadinside28/bloodborne_pc, GPL-2.0): a host-side approach -
  keep guest allocations at 1920x1080, render the scene into host proxies at
  output / scale, reconstruct at the first Scaleform pass, resolve proxies
  back for guest compute, align sizes (it hit a 1916x1080 alignment bug).
  We took the alignment lesson (render sizes are multiples of 8) and the
  "resolve/settle at a display-pass boundary" idea; we chose the game's own
  decoupled path over proxies because bbhost runs the game's code natively
  and already resizes the real targets.
- **Supermedo** (Supermedo/bloodborne_pc, GPL-2.0): the Windows build of
  bbport; same scene handling (identical `ui_composition.h` /
  `vk_temporal_upscaler.cpp` approach). Useful as a reference for Phase 8.
- **Awakenchan** (BloodBorneButPc, GPL-2.0, from DarkIzuku's bbport fork):
  memory budgeting - only a scene above 1080p grows the guest heaps; an
  output-only increase does not. Phase 1 never renders the scene above the
  output, so splitting can only shrink the scene's heap use; the `heap:`
  lines confirm it on the first test. Its notes on point-sampled reduced
  proxies (blocky post) are why the open risks list includes post passes
  that read render-size targets.

### Open risks (only the hardware test can answer)
- The sites' original bytes are unverified (no dump here, and no CRC table
  covers them). The 1.09 SHA gate and the branch checks guard it; the first
  log prints every site's bytes.
- Whether the graphics manager re-makes its scene context when only the
  scene size changes (logged: `scene-split: the graphics manager's scene is
  WxH`, or `..., not the WxH asked for`). If not,
  a live Native<->67% toggle needs a resolution change or a restart.
- The scene's colour is DefRenderTarget at the max display size; with YEBIS
  reading its viewport from the output entry, the old black/grey band issue
  could come back at some sizes.
- Post passes that read render-size depth or half-size targets with point
  sampling could look blocky; item icons and Scaleform's offscreen targets
  could be sized from the wrong entry.
- Whether `ccbf44a6` samples with `image_sample` (the S# rewrite) - logged.
- Sites are written at the quiet point while other guest threads may run.

### Test results (build box)
- `packaging/vk/build.sh`: configure, build and ctest pass: 44 tests, 0
  failures (16 skip without game files). `scene_split_test`: "24 sites
  match Kyo's Decoupled UI patches at 4 sizes; render sizes and the bilinear
  S# ok".
- Phase 1 binary: built by `packaging/vk/build.sh` from d38480b
  (`v0.2.18-vk.0-19-gd38480b`), stripped, needs GLIBC_2.38, SHA-256
  `96bfcf84d6ef42f2fa77f9c6e7335aa49ee6e77309c97ac86a507b8275f60f2c`
  (on the build box at /workspace/bbhost-vk-builds/phase1/; not released).
- Player's PC test: pending (TESTING.md, "Pending combined test session").

---

## 2026-10-18 (Sun), Phase 2: Inputs for temporal upscaling (branch `phase-2-mv-jitter`)

Base: `phase-1-scene-ui-split` (PR #2, unmerged); this branch's PR is
stacked on it. Work done on the build box, with no game dump: the math and
the shader are tested here (the shader on lavapipe), everything that
depends on the game's own data is logged for the first test.
No upscaler files were needed: no OptiScaler release was downloaded and no
FSR DLLs were used (FSR 3.1 comes from the FSR-Vulkan submodule, as in
Phase 0).

### Findings
- **Pipeline names are hashes the other projects use.** bbhost names a
  pipeline `VSHASH+PSHASH` from the OrbShdr footer, which is shadPS4's
  `pgm_hash`, so IFreemz's shader hashes apply unchanged.
- **The scene constants** are an 864-byte (216-float) constant buffer
  (bbport): [0] far (3000), [1] 1/far, [4..5] the render size, [6..7] its
  reciprocals, the view as 3x4 rows at [8..19], the projection at
  [52, 57, 62, 63], the inverse view at [180..191]. The previous frame's
  matrices are not in it, so bbhost keeps its own history.
- **Two cameras a frame**: two map sections can bind different scene
  constants in one frame (IFreemz); the camera used by most draws is the
  scene's.
- **The game draws its own object velocity** (IFreemz): pixel shaders
  `34bc187c` (opaque, packing scale in constant buffer 0), `749e4f9e` and
  `b25e4fae` (alpha-tested, scale in buffer 1), into a coarse 160x90 RGBA8
  target; decoded delta = (rg - 0.5) * (-w/sx, h/sy) where alpha > 0.5.
- **Jitter eligibility** (IFreemz, bbport): draws whose depth is the scene
  depth and that are indirect, or have more than 6 indices, or more than one
  instance; the scene depth is the one bound while drawing into the scene
  colour. Fullscreen passes (6 indices or fewer) and UI vertex shaders
  (`34e8a281`, `81d336ce`, `09957251`, `24042a9b`, `a400228b`) never jitter.
- **Phase count**: FidelityFX's `int(8 * (display / render)^2)`: 18
  (Quality), 23 (Balanced), 32 (Performance), 72 (Ultra Performance), 8 at
  native.

### Done
- `src/host/temporal/` (pure, header-only, unit-tested in
  `tests/temporal_test.cpp`):
  - `halton.h`: Halton(2,3), phase count, offset = halton(i % n + 1) - 0.5
    in render pixels (+x right, +y down).
  - `camera.h`: scene-constant parsing with signature checks (far, size,
    reciprocals, unit rotation rows) and a double-precision inverse;
    `CameraHistory` (majority camera of up to 8 a frame, previous frame
    kept); camera-cut detection - first frame, no camera, a gap of more than
    2 frames, a teleport (> 20 units, IFreemz's section switch), a rotation
    (> 45 degrees), a projection change (> 10% FOV), a resize;
    `make_reprojection` (previous view x inverse current view, in double);
    `camera_motion` (the shader's math in double, for the tests).
  - `motion_classify.h`: the hashes above, buffer classes (864 = scene,
    640..16384 = skeleton, 96..639 in steps of 48 = small skeleton; bbport),
    `jitter_eligible`, the blend bit, and bbport's moving gate (a palette
    whose hash matches the previous frame is static).
- `src/host/upscaler_inputs.h`: the backend-agnostic `UpscalerInputs` -
  colour, depth, motion, the opaque-colour snapshot, (reactive, exposure:
  empty, the backend generates them), the jitter and its phase count,
  reset/cut, near/far, vertical FOV, frame time. Phase 3's FSR 3.1 and
  Phase 4's FSR 4.1.1 take this; Phase 7's FSR 4 INT8, DLSS and XeSS take
  the same struct.
- `src/host/temporal_inputs.cpp` (the runtime) and the hooks in
  host/render.cpp:
  - constant buffers are observed while their V#s are resolved (only while
    watching); the scene block is parsed once per address and content, the
    camera accepted only if it carries the render size;
  - the jitter is added to the viewport of jitter-eligible draws;
  - at the scale pass (`ccbf44a6`): the camera history ends the frame, cuts
    are logged, `shaders/motion.comp` fills an RGBA16F motion image at the
    render size (rg = current-to-previous motion in render pixels, b = object
    motion, a = camera valid), and the next jitter phase is chosen;
  - before the first blended draw into the scene colour, the colour is
    copied (the opaque snapshot, for the backends' reactive masks).
- Show Motion Vectors (Phase 0's toggle) draws the motion image over the
  picture in the presenter (`shaders/motion_view.frag`): red/green =
  horizontal/vertical motion (|d| / 8 px, square root), blue = object
  motion, a dark wash where there is no valid camera. With the toggle on the
  motion vectors are computed even without a temporal upscaler (still only
  while the scene is split).
- Logs: `jitter:` (on/off and why, phase count), `camera-cut:` (each cut
  and its kind, repeats compressed), `motion:` (scene constants found, the
  scene depth, the first motion pass, a summary every 3000 frames:
  cameras, cuts, velocity draws, opaque snapshots, palettes moving/static).
- Switches for A/B on the test PC: `BBHOST_JITTER=0`,
  `BBHOST_MOTION_VECTORS=0`, `BBHOST_OBJECT_MOTION=0`,
  `BBHOST_OPAQUE_SNAPSHOT=0`.
- `tests/motion_shader_test.cpp`: runs the compiled `motion.comp` on any
  Vulkan device (lavapipe here) over a synthetic floor/wall/sky scene 1800
  units from the origin with a turning, moving camera and a jitter, and
  checks every pixel against `camera_motion` in double.

### Decisions and why
1. **Jitter only with the split active and FSR 3.1 or FSR 4.1.1 selected**
   (as the plan asks). Nothing resolves the jitter before Phase 3, so a
   Phase 2 build with FSR 3.1 selected shows a sub-pixel shimmer of the 3D
   scene. That is expected; `BBHOST_JITTER=0` removes it for comparison.
2. **Off, FSR 1 and Native AA are untouched by construction**: the runtime
   decides once per flip whether it watches (split, and a temporal upscaler
   or Show Motion Vectors); when not, every hook is a single branch and no
   draw, viewport, buffer or pass changes.
3. **Jitter is removed from the motion vectors** in the shader (the pixel's
   position is unjittered before reprojection), so they describe motion
   only; the upscaler still gets the jitter separately.
4. **Matrices in double on the CPU**: Bloodborne's world coordinates reach
   the thousands; the reprojection matrix is built as prev x inverse(cur)
   in double and only the product goes to the GPU, so float precision is
   spent on view-space distances (the GPU test: worst 0.016 px at 1800 units).
5. **Object motion: the game's own velocity first.** It is exact where the
   game draws it and costs nothing. bbport's previous-frame positions for
   skinned meshes, cloth and weapons are only **surveyed** in this phase
   (moving vs static palettes counted, static map pieces skipped by the
   moving gate); re-running those draws with last frame's palettes is
   deferred until the first test shows where the game's velocity is
   missing. This is the main piece of Phase 2 not done.
6. **Reactive mask and exposure**: the opaque snapshot is the input FSR's
   own reactive-mask generation wants; no dedicated R8 mask yet. Exposure
   is left to the backends' auto-exposure (FSR 3.1 and 4 have one), so no
   guess at the game's tonemapping is needed.
7. **The overlay is drawn only in the presenter's one-pass path** (the
   split's normal path); the FSR 1 magnified path does not draw it.
8. **Online play, HDR, motion blur** are untouched; the motion-blur shader
   (`e0305cef`) is not hooked. The presenter's swapchain is unchanged, so
   goverlay/MangoHud see the same as before.

### What the other projects taught
- **IFreemz** (shadPS4-Bloodborne-DLSS-FSR, GPL-2.0-or-later): the velocity
  shaders and their decoding, the jitter rule, the majority camera, the
  section-switch threshold, the view-space reprojection with the sky as a
  direction. `motion.comp` adapts that math (credited in the shader and in
  THIRD_PARTY_NOTICES.md; GPL-2.0-or-later is GPL-3.0-compatible).
  IFreemz replays the velocity draws at render size into an RGBA16F mirror;
  bbhost samples the game's 160x90 target for now (see the risks).
- **bbport** (deadinside28/bloodborne_pc, GPL-2.0): the layout of the
  864-byte scene block, the buffer classes, the viewport-offset jitter,
  the moving gate and previous-frame positions for object motion. Facts and
  ideas; no code copied.
- **Supermedo** (the Windows build of bbport): same approach; nothing new
  for Phase 2 beyond confirming bbport's layout.
- **Awakenchan** (BloodBorneButPc): no new guest allocations in Phase 2 -
  the motion image and the opaque snapshot are host images at the render
  size, so the guest heaps are as in Phase 1.

### Open risks (only the hardware test can answer)
- **Whether the scene constants are found at all** (`motion: scene
  constants found ...`) and whether they carry the render size or the
  output size while split (logged once if the output's).
- **The velocity target**: its alpha semantics, its coarse 160x90 edges
  (object motion bleeds a few pixels around moving enemies in the overlay),
  and whether it is drawn before the scale pass (`velocity draws ... after
  the scale pass` in the summary). If it is late, the frame uses last
  frame's velocity.
- **Jitter coverage**: shadow maps, the sky, decals and particles that use
  the scene depth may or may not shift; a shadow that shimmers more than
  the geometry is the sign.
- **Camera-cut thresholds** (20 units, 45 degrees) against fast rolls,
  ladders, lamp warps, cutscenes and the boss arenas' camera.
- **The opaque snapshot**: the first blended draw may come before all the
  opaque geometry (then the snapshot misses some); its copy costs one
  render-size colour copy a frame.
- **Cost**: one 8x8 compute dispatch at the render size and one colour copy
  a frame, plus a hash of each 864-byte block; measured with Show Upscaler
  GPU Time on the PC.

### Test results (build box)
- `packaging/vk/build.sh`: configure, build and ctest pass: 46 tests, 0
  failures (19 skip without game files). `temporal_test` (Halton, phase counts, scene-constant parsing, history
  and every cut kind, motion against direct projection to 0.02 px);
  `motion_shader_test`: "llvmpipe: 320x180 pixels match the CPU math
  (worst 0.0156 px, mean |motion| 8.83 px, 20404 sky)".
- Phase 2 binary: built by `packaging/vk/build.sh` from 4341e1a
  (`v0.2.18-vk.0-31-g4341e1a`), stripped, needs GLIBC_2.38, SHA-256
  `fd56aa030e6ddbbafdac70647d4e3dd3fa1e445880c5904415fffd4832846728`
  (on the build box at /workspace/bbhost-vk-builds/phase2/; not released).
- Player's PC test: pending (TESTING.md, "Pending combined test session",
  part C).
