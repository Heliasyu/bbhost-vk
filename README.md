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

## Branches and versions
`main` is the playable build. Work happens on `phase-*` branches, and new bbhost releases are merged on `upstream-sync` first. Versions are `<bbhost version>-vk.<n>`.

## License
GPL-3.0, same as bbhost. See [LICENSE](LICENSE).
