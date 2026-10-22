# bbhost-vk

**bbhost-vk** runs Bloodborne (CUSA03173, game version 1.09) natively on Linux with modern
PC features: temporal upscaling up to FSR 4.1.1, a smooth 120 fps, DualSense HD haptics and
adaptive triggers, and built-in performance tools. It started as a fork of
[droogie's bbhost](https://github.com/droogie/bbhost), which does the core work of running
the game, and has grown into its own project on top of it. A new name and a new launcher
are on the way (see [Roadmap](#roadmap)).

It's tuned on one PC (RX 9070 XT, Ryzen 9 5950X, KDE Plasma on Wayland), targeting
**1080p render → 4K output with FSR 4.1.1 Performance at a locked 120 fps.**

No game files, AMD DLLs or FSR 4.1.1 assets are in this repository or its releases.
Online play is unchanged from bbhost: the same thehuntersdream.com servers, sign-in and
networking code.

## Features

### Upscaling and image quality
- **FSR 4.1.1** on Vulkan, built from your own copy of AMD's DLL. FP8 on RDNA4, INT8
  elsewhere, with an automatic fallback to FSR 3.1.
- **FSR 3.1** and **FSR 1** as well.
- **Presets:** Native AA, Quality, Balanced, Performance and Ultra Performance, plus
  sharpness. They change live in the game, with no restart.
- **Sharp HUD and menus:** the 3D scene renders at the preset's size, the interface at full
  resolution.
- **Motion vectors and camera jitter** fed to the upscaler, for a stable image in motion.
- **Post-processing at render size:** depth of field, fog and motion blur run before the
  upscaler, so they cost less and upscale cleanly.

### Frame rate
- **A 120 fps cap with even frame pacing.**
- Measured at 4K with FSR 4.1.1 Performance in Old Yharnam: about **120-122 fps average,
  1% lows around 110.** Heavy smoke can still dip below 120; that's the current focus.
- Work done so far: a latency limit, hot threads on the fastest cores, asynchronous
  logging, cheaper GPU submissions and a profile-guided (PGO) build.

### DualSense and DualSense Edge
- **HD haptics (USB):** the game's rumble and its sound effects (hits, gunshots,
  footsteps, doors) played on the controller's haptic motors. Music and ambience are
  filtered out, and left and right follow where a sound comes from.
- **Adaptive triggers** that follow what your character is doing:
  - **R2:** resistance on a charged heavy attack's wind-up, a rumble that builds while it
    charges, and a kick on the release.
  - **L2:** a trigger break with a recoil kick on each shot. It goes slack when you're out
    of Quicksilver Bullets.
- **Menu taps** when moving through menus, firmer ones on confirm and back.
- All adjustable live in F10 > CONTROLLER.

### Tools
- **F11 performance snapshots:** the last 30 seconds of frame times, GPU time per pass and
  CPU time per thread, saved in one keypress.
- **`bbtest`:** logged test sessions with screenshots, markers and snapshots.
- **A KDE/Wayland launcher wrapper** that sizes the setup window correctly with or without
  display scaling.

## Install
You need a working [bbhost](https://github.com/droogie/bbhost) setup first: the game dump
with the 1.09 update, its decrypted eboot, and a Vulkan 1.3 driver. bbhost-vk replaces only
the binary.

1. **Back up** your `bbhost.bin` (`cp bbhost.bin bbhost.bin.upstream`) and your settings in
   `~/.config/bbhost/`.
2. **Download** the latest tarball and `SHA256SUMS` from
   [Releases](https://github.com/Heliasyu/bbhost-vk/releases), then check them with
   `sha256sum -c SHA256SUMS`.
3. **Copy the binary in:**
   ```sh
   tar xzf bbhost-vk-linux-*.tar.gz
   cp bbhost-vk-linux-*/bbhost /path/to/your/bbhost/bbhost.bin
   ```
   Keep your own `plugins/` folder (the official plugins are signed by upstream).
4. **Start it** as usual. To go back: `cp bbhost.bin.upstream bbhost.bin`.

### FSR 4.1.1 (optional, once per PC)
FSR 4.1.1 can't be redistributed, so its assets are built from your own copy of AMD's DLL:
1. Get `amd_fidelityfx_upscaler_dx12.dll` (4.1.x), for example from an
   [OptiScaler release](https://github.com/optiscaler/OptiScaler/releases).
2. Install a Proton (GE-Proton 10+, Proton-CachyOS or Proton Experimental) and
   `mingw-w64-gcc cmake ninja python spirv-tools git`.
3. In the unpacked release folder: `./build-fsr4.sh /path/to/amd_fidelityfx_upscaler_dx12.dll`
   (a few minutes).
4. Pick **Upscaler: FSR 4.1.1** in the settings.

### HD haptics
Plug the DualSense in by USB **with its Bluetooth link off** (disconnect it in your
Bluetooth settings, then replug), and keep its sound card in the **Direct** profile, not
Pro Audio. Over Bluetooth you get classic rumble and the adaptive triggers.

## Settings
Settings live in the setup window (`--setup`), the F10 overlay in the game (UPSCALING,
CONTROLLER) and the game's own System menu. They are saved in
`~/.config/bbhost/bbhost-options.toml`.

## Roadmap
- **Smoke-proof 120 fps:** 1% lows of 115+ in the heaviest areas (Old Yharnam, the
  Forbidden Woods, Yahar'gul).
- **A new launcher, built from scratch:** easier to use, with a Bloodborne look.
- **A new name** for the project, to match what it has become.
- **More GPUs:** FSR 4 presets for RDNA2/3 and the Steam Deck, then Windows.

Details: [docs/vk/PLAN.md](docs/vk/PLAN.md). Known issues:
[docs/vk/KNOWN_ISSUES.md](docs/vk/KNOWN_ISSUES.md). Changes per version:
[CHANGELOG.md](CHANGELOG.md).

## Building
```sh
git submodule update --init --recursive
packaging/vk/build.sh          # PGO_PROFILE=<file> for the PGO build
tools/package_linux.sh         # the release tarball
```
For developers: [docs/vk/TESTING.md](docs/vk/TESTING.md) (test sessions) and
[docs/vk/DEVLOG.md](docs/vk/DEVLOG.md) (the working log, with measurements).

## Credits
- [droogie/bbhost](https://github.com/droogie/bbhost): the native Bloodborne host this
  project is built on. Its original README: [README.upstream.md](README.upstream.md).
- [bbport](https://github.com/deadinside28/bloodborne_pc) (GPL-2.0-or-later): the FSR 4.1.1
  replay and asset build this project adapted, and findings behind the motion vectors.
- [FireBurn/FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan) (MIT): FSR 3.1 on Vulkan,
  with the AMD FidelityFX SDK.
- [HansKristian-Work/dxil-spirv](https://github.com/HansKristian-Work/dxil-spirv) (MIT):
  used when building the FSR 4.1.1 assets.
- Kyo's Decoupled UI patches (via
  [shadps4-emu/ps4_cheats](https://github.com/shadps4-emu/ps4_cheats)),
  [IFreemz](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR),
  [Supermedo](https://github.com/Supermedo/bloodborne_pc) and
  [Awakenchan](https://github.com/Awakenchan/BloodBorneButPc): findings behind the
  scene/UI split.

What was copied, adapted or only learned from: [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License
GPL-3.0, same as bbhost. See [LICENSE](LICENSE).
