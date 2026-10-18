# Changelog (bbhost-vk)

## Unreleased (Phase 5 and the first test build, branch `phase-5-test-release`)
- **test5 (character shimmer with FSR 3.1, FSR 4.1.1 copy):**
  - The game's object-velocity scale is now checked before use. test4 read (1, 1) for two
    of its three windows. That decodes a still character's velocity texel to a 5-pixel
    motion, and anything else to hundreds of pixels. A real scale (test4's third window:
    8.89, -5, the 160x90 target's size / 18) keeps the scene's aspect. Anything else is
    skipped and logged (`motion: velocity draw ... not a packing scale`).
  - Object motion now replaces the camera's only inside the velocity target's objects (not
    on their 16x16-pixel edges). It is also used only where it differs from the camera's
    motion by more than `BBHOST_OBJECT_MOTION_MIN` (1.5 render pixels; one 8-bit step is
    ~1.1). The motion-vector view shows such pixels dim blue, and used object motion
    bright blue.
  - FSR 4.1.1 also writes the scale pass's target directly (test4: copy 0.26 ms).
    `BBHOST_FSR_DIRECT_TARGET=0` keeps the copy for both upscalers.
  - Rejected and documented: `BBHOST_SUBMIT_EVERY_JOB=0` (test4 run C: 64 fps, 1% low
    14.5, flashing effects). The default stays a submit per job.
- **test4 (performance at 4K output, camera):**
  - The scene camera is now found every frame. test3's probe showed that most of the
    G-buffer draws' V#s carry no record count, so their size read as 0 and their scene
    constants were never looked at; the camera only came from one pipeline whose V# said
    864 bytes (test3: 1582 history resets in 3000 dispatches, jitter pausing and resuming).
    Such a V# is now read as far as the shader reads it, and a block whose head matches is
    read on to its 864 bytes when that memory is mapped.
  - FSR 3.1 writes the scale pass's 3840x2160 target directly instead of its own image plus
    a copy (test3: copy 0.23 ms a frame). Only the output-sized RGBA16F targets take STORAGE
    usage, only with the split and FSR 3.1/4.1.1 chosen. `BBHOST_FSR_DIRECT_TARGET=0` keeps
    the copy. The `fsr3:` summary counts `written directly`.
  - Measured (TESTING.md "test4"): at 4K output the GPU is the limit (busy 91-93% from the
    busy meter, ~300 W). `gpu-wait` is the command processor waiting for one of its 16
    submission slots, i.e. it runs ahead of the GPU. It is not a host-side stall.
- **test3 fixes:**
  - The dark, stretched picture with a grey band at 3840x2160 (test2) is fixed. Any upscaler
    preset below Native AA now makes the display buffers the output's size at the start.
    Before, YEBIS's post-processing was built at the 5120x2160 buffers and read the
    3840-wide picture across 5120. A Resolution change while the game runs that no longer
    matches the buffers runs without the upscaler until the next start (logged).
    `BBHOST_SPLIT_DISPLAY=max` restores the old sizing.
  - The scene camera is looked for as bbport does: any shader stage, any buffer size, and
    inside the G-buffer draws' buffers. `BBHOST_MOTION_PROBE=1` logs what those draws bind.
  - The jitter pauses while no scene camera is found (it could only flicker).
    `BBHOST_JITTER_NO_CAMERA=1` keeps it on.
  - New debug option: `BBHOST_FSR_DUMP=1` (or `[debug] fsr_dump = true` in bbhost.toml)
    writes one frame's FSR inputs, output and target as PNGs, with numbers. It runs at
    `bbtest mark`, with `fsr-dump.request`, and once by itself.
  - The `upscaler: settings at start` line now computes the render size from the Resolution
    setting, not a fixed 3840x2160.
- **test2 fixes:**
  - FSR 3.1 and 4.1.1 now upscale to the active output (the Resolution setting) instead of
    the larger display buffers that live resolution keeps. test1 showed the scene in only
    about a quarter of a 4K screen.
  - The scene camera is also found by the contents of its constants, so FSR's history
    should no longer reset every frame (needs a hardware check).
  - Clearer reset lines in the log.
  - The setup window's token row says the token is optional.
- **Per-pass GPU timing:** **Show Upscaler GPU Time** now breaks the renderer's work down: the
  motion vectors, reactive mask, opaque snapshot, FSR 3.1 or 4.1.1 dispatch, and output copy.
  It is on the overlay and in the log (`pass-timer:` every 10 s).
- **Fewer allocations per frame:**
  - The FSR 3.1 backend reuses its constant buffers and descriptor pools. Before, it made a
    driver memory allocation and a descriptor pool for every dispatch. This comes from a
    build-time patch to copies of FSR-Vulkan's files (`cmake/patches/`); the submodule is
    unmodified.
  - The motion pass's and FSR 4.1.1's image views are kept while their images live.
- **Game Settings menu resolution freeze:** a resolution, upscaler or quality change made in
  the game's own System menu now waits for the pick list to close before it resizes. A
  never-shown question is withdrawn. F10 is unchanged.
- **Packaging:** `tools/package_linux.sh` names the tarball for `git describe` and adds
  `build-fsr4.sh` with the FSR 4.1.1 builder. It refuses to package any DLL, game file or FSR
  4.1.1 asset, and writes a `.sha256`.
- **Vulkan validation pass:** every Vulkan test and the stream selftest run clean of our
  own errors under the Khronos validation layer with sync validation (`tools/vvl_check.sh`).
  New `fsr411_fallback_test` covers FSR 4.1.1's failure paths. The selftest now checks the
  kept pass views. motion_shader_test no longer leaks. FSR-Vulkan's own back-to-back clears
  are reported in DEVLOG, not patched.
- **Docs:**
  - README: a "Testing bbhost-vk" section.
  - TESTING.md: the test-build session.
  - The updater needs no token (the repository is public).
- **CI fix:** the release workflow's package-all job (the Steam Deck kit and the bundle check) failed
  because it looked for `bbhost-linux-*` while the Linux package is now `bbhost-vk-linux-*`. The
  Steam Deck and playtest kits, the bundle check, the publish step and the release announcement
  now take either name (the kit is `bbhost-vk-steamdeck-*`), so the live-server check covers the
  Linux and Steam Deck bundles again.

## Unreleased (Phase 4 groundwork and test-build prep, branch `phase-4-fsr411`)
- **FSR 4.1.1** replays AMD's 4.1.1 upscaler on Vulkan (ported from
  bbport) from assets you build once from your own
  `amd_fidelityfx_upscaler_dx12.dll` (`tools/fsr4cap/build_assets.sh`, under
  Proton; for example the DLL in an OptiScaler release). It uses FP8 on
  RDNA4 and INT8 elsewhere. Without the assets, or on a GPU that lacks the
  features, it is FSR 3.1, and the log says why.
- Setup window: the FSR 4.1.1 DLL path and asset status, and **Upscaler
  jitter** (also in F10).
- The update check looks at bbhost-vk's own releases; versions like
  `v0.2.18-vk.1` are ordered.
- docs/vk/KNOWN_ISSUES.md: the resolution question in the game's own
  Settings menu (upstream issue).

## Unreleased (Phase 3: FSR 3.1 temporal, branch `phase-3-fsr31`)
With Upscaler Off or FSR 1, or Upscaling quality at Native AA, nothing
changes. Online play (servers, sign-in, networking), HDR and motion blur are
untouched.
- **FSR 3.1 upscales the 3D scene.** With the split active and Upscaler FSR
  3.1, FidelityFX FSR 3.1.5 (FireBurn/FSR-Vulkan, MIT) runs at the game's
  scale pass with the scene colour, depth, motion vectors, jitter and a
  reactive mask, resets its history on camera cuts, uses its own auto
  exposure and sharpens with RCAS from **Upscaler sharpness** (0 = off).
  Its output replaces the game's bilinear copy; the UI is drawn on top at
  the output size as before. Every preset (Native AA to Ultra Performance)
  works; a preset or resolution change makes a new context.
- **FSR 4.1.1** selected uses FSR 3.1 until Phase 4 (logged once).
- **Fallback**: if FSR 3.1's context cannot be made, or a frame lacks its
  inputs, the frame is drawn with the Phase 1 bilinear copy and the reason
  is logged (`fsr3:`).
- **Upscaler reactive mask** (F10 and the setup window, default On): a mask
  from the scene against the opaque snapshot - particles, fog, transparents,
  Blood Echoes, the lantern's glow - so FSR keeps less history there.
- **Show Upscaler GPU Time** adds the FSR 3.1 dispatch's own GPU time to the
  GPU line (`FSR 3.1 x.xx ms`) and to the log.
- Test switches: `BBHOST_FSR3=0` (bilinear copy instead of FSR 3.1),
  `BBHOST_REACTIVE=0`, `BBHOST_FSR_JITTER_SIGN=x|y|xy` (flip the jitter
  FSR is told, should the picture wobble), `BBHOST_REACTIVE_SCALE`,
  `_THRESHOLD`, `_VALUE`.
- Tests: `fsr3_upscaler_test` (contexts at every preset for 720p and 4K
  on software Vulkan, resource sizes, flat frames, reset on a cut) and the
  FSR 3.1 policy in `temporal_test`.
- Not done: object motion from skinned meshes' previous bone palettes
  (bbport) - deferred, see docs/vk/DEVLOG.md.

## Unreleased (Phase 2: inputs for temporal upscaling, branch `phase-2-mv-jitter`)
With Upscaler Off or FSR 1, or Upscaling quality at Native AA, nothing
changes: no draw, viewport or pass is touched. Online play, HDR and motion
blur are untouched.
- The scene's motion vectors: a compute pass at the scale pass turns the
  scene depth and this and last frame's camera (from the game's 864-byte
  scene constants, matrices handled in double) into an RGBA16F motion
  image; where the game draws its own object velocity, the object's motion
  is used. Camera cuts (teleport, a fast turn, FOV change, resize, a gap)
  reset the history and are logged (`camera-cut:`).
- Halton(2,3) jitter on the main scene's geometry draws, only with the
  split active and FSR 3.1 or FSR 4.1.1 selected; the phase count follows
  FidelityFX (18 at Quality). Nothing resolves it until Phase 3, so the 3D
  scene shimmers slightly in this build with those upscalers
  (`BBHOST_JITTER=0` turns it off). Logged (`jitter:`).
- An opaque-colour snapshot before the first transparent draw, for the
  upscalers' reactive masks; exposure is left to their auto-exposure.
- A backend-agnostic `UpscalerInputs` (src/host/upscaler_inputs.h) for FSR
  3.1, FSR 4.1.1 and later backends.
- **Show Motion Vectors** now draws the motion vectors over the picture
  (red/green = motion, blue = object motion).
- Test switches: `BBHOST_JITTER=0`, `BBHOST_MOTION_VECTORS=0`,
  `BBHOST_OBJECT_MOTION=0`, `BBHOST_OPAQUE_SNAPSHOT=0`.
- Tests: `temporal_test` (Halton, camera history and cuts, motion math) and
  `motion_shader_test` (the shader against the CPU math on any Vulkan
  device).
- Not yet: object motion from last frame's bone palettes (bbport) is only
  surveyed and logged.

## Unreleased (Phase 1: scene/UI resolution split, branch `phase-1-scene-ui-split`)
With Upscaler Off, or Upscaling quality at Native AA (the default), nothing
changes: the game's code and draws are stock. Display-only; online play,
HDR and motion blur are untouched.
- Upscaling quality now sets the 3D scene's render size when an upscaler is
  on: output (the Resolution setting) / preset ratio, a multiple of 8 - e.g.
  4K Quality renders the scene at 2560x1440. Post-processing, the Scaleform
  UI and the display buffers stay at the output size, so menus and text are
  sharp. Based on Kyo's Decoupled UI patch sites for 1.09, re-encoded for
  any size, with host stubs instead of eboot code caves
  (`src/engine/scene_split*`). `BBHOST_SCENE_SPLIT=0` turns it off.
- The game's scene-to-UI scale pass (`ccbf44a6`) is drawn as a plain
  bilinear copy while split (FSR 3.1 replaces it in Phase 3).
- Changing Upscaler or Upscaling quality in F10 or the System menu resizes
  the scene live, at the same point and delay as a resolution change.
- Log lines for testing: `scene-split:` (render size, scale pass, site
  bytes), `heap:` before/after each change, `yebis: restart N`.
- `scene_split_test`: the site table against Kyo's patches at four sizes.

## Unreleased (Phase 0: groundwork, branch `phase-0-groundwork`)
Nothing changes on screen or online in this phase: with every new setting at
its default (and with Upscaler Off or FSR 1) the game is stock bbhost v0.2.18
plus the 120 fps option.
- FSR-Vulkan (FireBurn, MIT) added as the submodule `third_party/FSR-Vulkan`,
  pinned at c64f093, and built into bbhost: the FSR 3.1.5 and 3.1.4 upscalers
  with their embedded SPIR-V tables (all 11 FSR 3.1.5 passes; checked by
  `upscaler_test` and logged at start). Not called yet. `THIRD_PARTY_NOTICES.md`;
  the Linux package ships the MIT notices under `licenses/`.
- New settings in `bbhost-options.toml`, the F10 screen (UPSCALING), the
  in-game System menu (PC Upscaling, Upscaling Quality) and the setup window:
  `upscaler` (Off / FSR 1 / FSR 3.1 / FSR 4.1.1, default FSR 1),
  `upscale_quality` (Native AA 100% / Quality 67% / Balanced 58% /
  Performance 50% / Ultra Performance 33%, default Native AA),
  `upscale_sharpness` (0.0-1.0, default 0.8 = stock FSR 1's sharpening),
  `debug_motion_vectors`, `debug_upscaler_time`. Stored and logged only.
- Debug tooling: a per-pass Vulkan timestamp timer for the presenter (shown
  under the FPS counter and logged with Show Upscaler GPU Time), a motion-vector
  view hook (no-op until Phase 2), and a presented-frame PNG capture
  (`BBHOST_VK_CAPTURE=1`; Shift+F12, a `capture.request` file, or
  `BBHOST_VK_CAPTURE_EVERY`).
- `packaging/vk/build.sh`: the CI's portable release build, stripped like the
  release package, with SHA-256 and BUILDINFO.txt.
- `packaging/vk/bbtest` and `docs/vk/TESTING.md`: test sessions on the
  player's PC (log, MangoHud CSV, monitor screenshots, replay clips, marks).
- `docs/vk/DEVLOG.md`: the development log.
- CI checks out submodules.

## v0.2.18-vk.0
- Based on bbhost v0.2.18 (c421ea0).
- Added a 120 fps frame-rate option to the launcher and the F10 menu.
- Added the KDE/Wayland scaling wrapper (`packaging/vk/bbhost`).
- Added the development plan (`docs/vk/PLAN.md`) and the launcher mockup.
