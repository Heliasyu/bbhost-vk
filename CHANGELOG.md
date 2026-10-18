# Changelog (bbhost-vk)

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
