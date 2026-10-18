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

## Not distributed
AMD's FSR 4.1.1 (`amd_fidelityfx_upscaler_dx12.dll`) and anything built from
it are never part of this repository or its packages (docs/vk/PLAN.md,
Phase 4: built on the player's own PC from their own copy).
