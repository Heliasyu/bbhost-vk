# Third-party notices (bbhost-vk)

bbhost-vk is GPL-3.0 (LICENSE). Besides the components upstream bbhost already
carries under `third_party/` (Dear ImGui, libatrac9, Monocypher - each with
its own license file there), bbhost-vk links:

## FSR-Vulkan
- Source: https://github.com/FireBurn/FSR-Vulkan, a git submodule at
  `third_party/FSR-Vulkan`, pinned to c64f093404125e960813f95f1996c14780f0dd69.
- License: MIT - `third_party/FSR-Vulkan/LICENSE.txt`
  ("Copyright (c) 2026 Q2RTX FSR Vulkan contributors"), with its
  `NOTICE.md` describing its distribution boundary.
- It carries subsets of AMD's FidelityFX SDK 1.1.4 and 2.3.0 (MIT,
  Copyright Advanced Micro Devices, Inc.); the full SDK license is
  `third_party/FSR-Vulkan/upstream/ffx-1.1.4/sdk/LICENSE.txt`, and the
  per-file notices are kept in those files.
- What bbhost-vk builds from it: the portable FSR 3.1.4 upscaler and the FSR
  3.1.5 upscaler with their embedded SPIR-V shader tables
  (`cmake/fsr_vulkan.cmake`). Its experimental "source-v07 FSR4" provider is
  **not** built, and it is not AMD FSR 4.1.1.
- MIT is GPL-3.0-compatible; the Linux package (`tools/package_linux.sh`)
  ships these notices under `licenses/`.

## Ideas and facts used, no code copied (Phase 1)
The scene/UI split (`src/engine/scene_split*`) is bbhost-vk's own code. It
uses facts and ideas from these projects, credited here and in
docs/vk/DEVLOG.md; none of their code is in bbhost-vk:
- **Kyo's "Decoupled UI" patches** for Bloodborne 1.09, as published in
  [shadps4-emu/ps4_cheats](https://github.com/shadps4-emu/ps4_cheats)
  (`PATCHES/Bloodborne.xml`): the list of eboot sites that size the scene
  separately from the output. bbhost-vk re-encodes them for any size and
  replaces their code caves with its own stubs; `tests/scene_split_test.cpp`
  embeds his published byte values to check the encoder.
- **IFreemz**, [shadPS4-Bloodborne-DLSS-FSR](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR)
  (GPL-2.0): the identification of the scene-to-UI scale pass
  (`HdrSceneCopy`, pixel shader `ccbf44a6`).
- **bbport**, [deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc)
  (GPL-2.0), and **Supermedo**'s Windows build of it,
  [Supermedo/bloodborne_pc](https://github.com/Supermedo/bloodborne_pc)
  (GPL-2.0): render-size alignment and changing sizes at a display-pass
  boundary.
- **Awakenchan**, [BloodBorneButPc](https://github.com/Awakenchan/BloodBorneButPc)
  (GPL-2.0): how scene size versus output size affects the game's memory.

## Adapted code (Phase 2)
- **IFreemz**, [shadPS4-Bloodborne-DLSS-FSR](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR)
  (GPL-2.0-or-later, which is GPL-3.0-compatible): the motion-vector math
  in `src/host/shaders/motion.comp` (view-space reprojection from D3D depth,
  the sky as a direction, the previous view times the inverse current view
  built in double, the decoding of the game's velocity target) follows his
  `bb_dlss_motion.comp`; bbhost-vk's version is rewritten for its own
  bindings and credited in the file. Facts from the same project: the
  velocity, depth, motion-blur and UI shader hashes, the jitter rule, the
  majority camera and the section-switch threshold.

## Ideas and facts used, no code copied (Phase 2)
- **bbport** (deadinside28/bloodborne_pc, GPL-2.0) and **Supermedo**'s
  build of it: the 864-byte scene-constant layout, the constant-buffer
  classes, the viewport-offset jitter, the moving gate and the
  previous-frame-positions approach to object motion
  (`src/host/temporal/`).

## Not distributed
AMD's FSR 4.1.1 (`amd_fidelityfx_upscaler_dx12.dll`) and anything built from
it are never part of this repository or its packages (docs/vk/PLAN.md,
Phase 4: built on the player's own PC from their own copy).
