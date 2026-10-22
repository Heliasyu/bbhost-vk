# bbhost-vk

**bbhost-vk** is a personal fork of [droogie's bbhost](https://github.com/droogie/bbhost), the native host that runs Bloodborne (CUSA03173, game version 1.09) on Linux. Everything that makes the game run is bbhost's work. This fork adds temporal upscaling and frame-rate work on top, aimed at one setup: **1080p render → 4K output with FSR 4.1.1 Performance, locked at 120 fps**, on an RX 9070 XT and a Ryzen 9 5950X under KDE Plasma (Wayland).

It's developed in public as a record of the work, not as a supported release. If it gets solid enough it may be shared more widely later. Until then, expect rough edges, and use [upstream bbhost](https://github.com/droogie/bbhost) if you just want to play.

No game files, AMD DLLs or FSR 4.1.1 assets are in this repository or its releases. Online play is untouched: the same thehuntersdream.com servers, sign-in and networking code as upstream.

## What it adds
- **Temporal upscaling:**
  - FSR 3.1, through [FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan).
  - FSR 4.1.1, replayed on Vulkan from passes captured from *your own* `amd_fidelityfx_upscaler_dx12.dll`. It uses the FP8 variant on RDNA4 and INT8 elsewhere, and falls back to FSR 3.1 when the assets or GPU features are missing.
- **Quality presets:** Native AA, Quality 67%, Balanced 58%, Performance 50% and Ultra Performance 33%, plus sharpness. The game's own anti-aliasing switches off automatically under FSR 4.1.1.
- **Scene/UI split:** the 3D scene renders at the preset's size while the HUD and menus stay sharp at full resolution (Kyo's Decoupled UI sites, as data).
- **Motion vectors and jitter** for the upscalers (camera and object motion).
- **Post-processing at render size:** depth of field, the fog/colour pass and motion blur run before FSR at the render size. Chromatic aberration, bloom, tonemap, grain and the HUD stay at full resolution.
- **Frame rate and pacing:**
  - a 120 fps cap;
  - even frame pacing at fixed caps (in testing: 1% lows of ~119-120 at 4K/120 with pacing on, against 88-100 with it off);
  - motion blur off becomes one copy instead of two passes.
- **CPU-side work:** hot threads placed on the preferred cores' L3 domain, asynchronous logging, a PGO build, and a cached key path in the command processor.
- **Tools:**
  - F11 performance snapshots (the last 30 s of frame times, GPU time per pass, CPU per thread);
  - `bbtest` test sessions;
  - a GPU pass timer;
  - FSR stage dumps.
- **A launcher wrapper** for KDE/Wayland (`packaging/vk/bbhost`) that sizes the setup window correctly on monitors with or without display scaling.

## Status
| | |
|---|---|
| Latest release | [`v0.2.18-vk.1`](https://github.com/Heliasyu/bbhost-vk/releases/tag/v0.2.18-vk.1) (2026-10-18 build) |
| `main` | vk.1 plus the rc2-rc7 performance work |
| Measured (Old Yharnam, FSR 4.1.1 Performance, 4K, 120 cap) | 114-122 fps average, 1% lows 82-111. Heavy smoke still drops below 120. |
| The limit now | The single command-processor thread (`bb-cp0`, ~7 ms of an 8.33 ms frame) and a GPU frame at ~90% |
| Not done yet | The launcher overhaul (mockups only, `docs/vk/mock/`), the `bb-cp0` split, the FSR 4.1.1 check against the DLL |

The plan, with what's next and why: [docs/vk/PLAN.md](docs/vk/PLAN.md). Known issues: [docs/vk/KNOWN_ISSUES.md](docs/vk/KNOWN_ISSUES.md).

## Install over an existing bbhost setup
You need a working [bbhost](https://github.com/droogie/bbhost) install first: the game dump with the 1.09 update, its decrypted eboot, and a Vulkan 1.3 driver. bbhost-vk only replaces the binary.

1. **Back up:**
   - the binary: in your bbhost folder, `cp bbhost.bin bbhost.bin.upstream`;
   - your settings: `~/.config/bbhost/bbhost-options.toml` (the F10 settings and your account sign-in) and `~/.config/bbhost/bbhost.toml`. bbhost-vk adds its own keys; upstream ignores them.
2. **Download and check** the release tarball and `SHA256SUMS` from [Releases](https://github.com/Heliasyu/bbhost-vk/releases), then run `sha256sum -c SHA256SUMS`.
3. **Copy the binary in:**
   ```sh
   tar xzf bbhost-vk-linux-*.tar.gz
   cp bbhost-vk-linux-*/bbhost /path/to/your/bbhost/bbhost.bin
   ```
   Keep your own `plugins/` folder: the official plugins are signed by upstream, and the release's copies are not.
4. **Start it** as usual (your wrapper, `./run-bbhost.sh`, or `bbtest`).
5. **To go back:** `cp bbhost.bin.upstream bbhost.bin`.

### FSR 4.1.1 (optional, once per PC)
FSR 4.1.1 can't be redistributed, so you build its assets from your own copy of AMD's DLL:
1. Take `amd_fidelityfx_upscaler_dx12.dll` (4.1.x) from an [OptiScaler release](https://github.com/optiscaler/OptiScaler/releases).
2. Install a Proton (GE-Proton 10+, Proton-CachyOS or Proton Experimental), plus `mingw-w64-gcc cmake ninja python spirv-tools git`. The script builds dxil-spirv itself and needs the network for that.
3. From the unpacked release folder, run `./build-fsr4.sh /path/to/amd_fidelityfx_upscaler_dx12.dll`. It runs the DLL once under Proton and writes the assets to `~/.local/share/bbhost/fsr4_411` (a few minutes, ~300 MB of work space in `~/.cache/bbhost/fsr4cap`).
4. Pick **Upscaler: FSR 4.1.1**. The log's `fsr4:` lines say which variant runs (e.g. `FSR 4.1.1 replay, FP8 t2160_m0`), or why it fell back to FSR 3.1.

## Settings
- **Where to set them:** the setup window (`--setup`), F10 > UPSCALING, and the game's own System menu (PC Upscaling, Upscaling Quality).
- **Upscaler:** Off, FSR 1, FSR 3.1 or FSR 4.1.1.
- **Upscaling quality:** the preset.
- **Upscaling sharpness:** 0.0-1.0.
- **Anti-aliasing:** On, Off or Auto. Auto turns the game's own AA off under FSR 4.1.1.
- **Upscaler reactive mask** and **Upscaler jitter.**
- **Show motion vectors** and **Show upscaler GPU time.** The GPU time view puts a `pass-timer:` line in the log every 10 s.
- **Frame rate:** 30, 60, 90, 120 or Uncapped.
- **Where they're saved:** `~/.config/bbhost/bbhost-options.toml`. The FSR 4.1.1 DLL path (`upscaler.fsr4_dll`) is in `~/.config/bbhost/bbhost.toml`.

Even frame pacing, post-processing at render size and the motion-blur skip are always on in `main`; their launcher switches were removed after vk.1. Environment variables still turn them off for comparison or if something looks wrong: `BBHOST_PRESENT_PACE=0`, `BBHOST_POST_AT_RENDER=0`, `BBHOST_MOTION_BLUR_SKIP=0`.

## Testing and performance data
- **F11 in the game:** saves a snapshot of the last 30 s to `snaps/` beside the binary: frame times, 1% lows, GPU time per pass, CPU time per thread, compiles, settings and the log tail. Take one *during* a frame drop.
- **`bbtest`** (`packaging/vk/bbtest`): runs a logged test session, with the bbhost log, MangoHud CSV, screenshots, an optional replay buffer, markers and snapshots, and bundles it into `~/.local/share/bbtest/sessions/`. `bbtest profile` adds RGP captures. See [docs/vk/TESTING.md](docs/vk/TESTING.md).

## Building
`packaging/vk/build.sh` makes the portable release build (stripped, with its SHA-256; `PGO_PROFILE=` for the PGO build), and `tools/package_linux.sh` packs a tarball. The build needs the `third_party/FSR-Vulkan` submodule (`git submodule update --init --recursive`). CI builds every push and pull request.

## Branches and versions
- **`main`:** the current code, and what releases are tagged from.
- **New work:** goes on a topic branch and is merged by pull request.
- **`upstream-sync`:** takes new bbhost releases first; online-code changes from upstream are merged before anything else.
- **The old `phase-*` branches:** history only. Everything in them is in `main`.
- **Versions:** `<bbhost version>-vk.<n>`, for example `v0.2.18-vk.1`.

## Documentation
- [docs/vk/PLAN.md](docs/vk/PLAN.md): the plan and status
- [CHANGELOG.md](CHANGELOG.md): changes per version
- [docs/vk/KNOWN_ISSUES.md](docs/vk/KNOWN_ISSUES.md)
- [docs/vk/TESTING.md](docs/vk/TESTING.md): test sessions and what each build was checked for
- [docs/vk/DEVLOG.md](docs/vk/DEVLOG.md): the full working log, with measurements
- [README.upstream.md](README.upstream.md): upstream bbhost's README (requirements, setup, online, mods)

## Credits
- [droogie/bbhost](https://github.com/droogie/bbhost): everything this fork builds on.
- [bbport](https://github.com/deadinside28/bloodborne_pc) (GPL-2.0-or-later): the FSR 4.1.1 replay (`src/host/fsr411/`, adapted) and the asset build (`tools/fsr4cap/`), plus findings behind the motion vectors and jitter.
- [FireBurn/FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan) (MIT): AMD FidelityFX FSR 3.1 on native Vulkan, a submodule at `third_party/FSR-Vulkan`. It carries the AMD FidelityFX SDK (MIT).
- [HansKristian-Work/dxil-spirv](https://github.com/HansKristian-Work/dxil-spirv) (MIT): translating the captured passes, built by `tools/fsr4cap` at asset-build time.
- Kyo's Decoupled UI patches (via [shadps4-emu/ps4_cheats](https://github.com/shadps4-emu/ps4_cheats)), [IFreemz](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR), [Supermedo](https://github.com/Supermedo/bloodborne_pc) and [Awakenchan](https://github.com/Awakenchan/BloodBorneButPc): the findings the scene/UI split builds on.

Details, with what was copied, adapted or only learned from: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License
GPL-3.0, same as bbhost. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
