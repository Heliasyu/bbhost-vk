# bbhost-vk

**bbhost-vk** is a GPLv3 fork of [droogie's bbhost](https://github.com/droogie/bbhost), the native Linux host for Bloodborne (CUSA03173, game version 1.09). All of bbhost's work is theirs; this fork adds on top of it:

- a **120 fps** frame-rate cap (done, `v0.2.18-vk.0`)
- temporal upscaling with **AMD FSR 3.1** and **FSR 4.1.1** (FSR 4.1.1 built once on your own PC from your own `amd_fidelityfx_upscaler_dx12.dll`; never distributed)
- an overhauled launcher with tabs, dropdowns and a dedicated Upscaling tab
- a KDE/Wayland wrapper (`packaging/vk/bbhost`) that sizes the launcher correctly on monitors with or without display scaling

No game files are included or supported here. Online play uses bbhost's servers exactly as upstream does.

- Development plan: [docs/vk/PLAN.md](docs/vk/PLAN.md)
- Changes: [CHANGELOG.md](CHANGELOG.md)
- Upstream README: [README.upstream.md](README.upstream.md)
- Development log: [docs/vk/DEVLOG.md](docs/vk/DEVLOG.md)
- Testing sessions (`bbtest`): [docs/vk/TESTING.md](docs/vk/TESTING.md); installing a test build: [Testing bbhost-vk](#testing-bbhost-vk)
- Building: `packaging/vk/build.sh` (the CI's portable release build, stripped, with its SHA-256)

## Testing bbhost-vk
A test build is a tarball named `bbhost-vk-linux-<git describe>.tar.gz`, made locally with
`tools/package_linux.sh`. Nothing is published. It contains the `bbhost` binary (stripped), `run-bbhost.sh`,
`build-fsr4.sh` with the FSR 4.1.1 builder (`tools/fsr4cap`, with prebuilt helpers in `tools/fsr4cap/bin`),
`docs/TESTING.md` (the test session) and `docs/KNOWN_ISSUES.md`. It contains no game files, no AMD
DLL and no FSR 4.1.1 assets.

**Install over an existing bbhost setup**
1. Back up your current binary first: in your bbhost folder, `cp bbhost.bin bbhost.bin.upstream`
   (bbhost-vk's wrapper `packaging/vk/bbhost` runs `bbhost.bin`). Also keep a copy of
   `~/.config/bbhost/bbhost.toml`. bbhost-vk adds its own keys to it, and upstream ignores them.
2. Unpack the tarball anywhere, then copy its `bbhost` over your `bbhost.bin`:
   `tar xzf bbhost-vk-linux-*.tar.gz && cp bbhost-vk-linux-*/bbhost /path/to/your/bbhost/bbhost.bin`.
   Keep your own `plugins/` folder. The official plugins are signed by upstream, and the test
   build's copies are not. The `patches/` manifests are the same as upstream's.
3. Start it as you always do (your wrapper or `bbtest`). Or run it from the unpacked folder with
   `./run-bbhost.sh`, which reads the same per-user settings.
4. To go back: `cp bbhost.bin.upstream bbhost.bin`.

**Settings.** The setup window (`--setup`) and F10 > UPSCALING have: **Upscaler** (Off / FSR 1 /
FSR 3.1 / FSR 4.1.1), **Upscaling quality** (Native AA, Quality 67%, Balanced 58%, Performance 50%,
Ultra Performance 33%), **Upscaler sharpness**, **Upscaler reactive mask**, **Upscaler jitter**,
**Show motion vectors** and **Show Upscaler GPU Time**. The GPU time line shows each pass: MV,
reactive, opaque, FSR 3.1 or 4.1.1, and copy. Frame rate goes up to **120**. The setup window
also has the FSR 4.1.1 DLL path and whether its assets are built. The game's own System menu has
PC Upscaling and Upscaling Quality. Everything is saved in `~/.config/bbhost/bbhost.toml`.

**FSR 4.1.1 (optional, once per PC).** FSR 4.1.1 cannot be shipped, so you build it from your
own copy of AMD's DLL:
1. Download the latest [OptiScaler release](https://github.com/optiscaler/OptiScaler/releases)
   and take `amd_fidelityfx_upscaler_dx12.dll` (4.1.x) out of its archive (`7z x`).
2. With Proton installed (GE-Proton 10+, Proton-CachyOS or Proton Experimental), plus `python3`
   and `spirv-tools` (spirv-dis/spirv-as), run `./build-fsr4.sh /path/to/amd_fidelityfx_upscaler_dx12.dll`
   from the unpacked folder. It runs the DLL once under Proton and writes the assets to
   `~/.local/share/bbhost/fsr4_411`. It takes a few minutes and needs about 300 MB of work space
   in `~/.cache/bbhost/fsr4cap`.
3. Pick **Upscaler FSR 4.1.1**. Without the assets, or on a GPU without the needed features, it
   upscales with FSR 3.1 instead, and the log says why (`fsr4:` lines). RDNA4 uses the FP8
   variant, other GPUs INT8.

**Logs.** `./run-bbhost.sh` keeps `logs/bbhost-<date>-<time>.log` beside it (the newest ten).
Through `bbtest`, the log goes into that session's folder (docs/vk/TESTING.md). With **Show
Upscaler GPU Time** on, the log also gets a `pass-timer:` line every 10 s. FSR's own lines start
with `fsr3:`, `fsr4:` and `motion:`.

**Online.** It is unchanged from upstream: the same thehuntersdream.com servers, sign-in, account
and networking (bbhost-vk does not touch that code). Your account and sign-in carry over.

**What to test:** [docs/vk/TESTING.md](docs/vk/TESTING.md), "Test build session". Known issues:
[docs/vk/KNOWN_ISSUES.md](docs/vk/KNOWN_ISSUES.md).

## Branches and versions
`main` is the playable build. Work happens on `phase-*` branches, and new bbhost releases are merged on `upstream-sync` first. Versions are `<bbhost version>-vk.<n>`.

## Credits
- [droogie/bbhost](https://github.com/droogie/bbhost): everything this fork builds on.
- [FireBurn/FSR-Vulkan](https://github.com/FireBurn/FSR-Vulkan) (MIT): AMD FidelityFX FSR 3.1 on native Vulkan, a submodule at `third_party/FSR-Vulkan`.
- AMD FidelityFX SDK (MIT), carried inside FSR-Vulkan.
- Kyo's Decoupled UI patches (via [shadps4-emu/ps4_cheats](https://github.com/shadps4-emu/ps4_cheats)), [IFreemz](https://github.com/IFreemz/shadPS4-Bloodborne-DLSS-FSR), [bbport](https://github.com/deadinside28/bloodborne_pc), [Supermedo](https://github.com/Supermedo/bloodborne_pc) and [Awakenchan](https://github.com/Awakenchan/BloodBorneButPc): the findings the scene/UI split builds on (no code copied; THIRD_PARTY_NOTICES.md).

## License
GPL-3.0, same as bbhost. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
