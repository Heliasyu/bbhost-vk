# bbhost-vk: development plan (FSR 4.1.1 + launcher overhaul)

Target: **bbhost-vk**, a GPLv3 fork of droogie's bbhost (based on v0.2.18 + the 120 fps patch) with temporal FSR 4.1.1 (FP8 on RDNA4, INT8 fallback) and FSR 3.1 fallback, a sharp native-resolution UI, and quality and performance matching or beating bbport on an RX 9070 XT at 4K.

**End goal:** the game rendering at 1920x1080 and upscaled to 3840x2160 with FSR 4.1.1 Performance (50% scale), holding a steady 120 fps on a 165 Hz VRR monitor.

Mockup of the new Upscaling page: [mock/upscaling-tabs.png](mock/upscaling-tabs.png).

References: Supermedo's Windows bbport launcher (visual style only), bbport (deadinside28, GPL-2.0+), IFreemz shadPS4 fork (GPL-2.0+), FireBurn/FSR-Vulkan (MIT). Their methods carry over; their code has to be adapted, because bbhost has its own renderer.

## Work order (quickest first, within dependencies)
1. Phase 0: Groundwork (1-2 days)
2. Phase 1: Scene/UI split (3-5 days)
3. Phase 2: Motion vectors and jitter (4-7 days)
4. Phase 3: FSR 3.1 (3-5 days), which ends with an early playable FSR 3.1 build
5. Phase L: Launcher overhaul (5-8 days)
6. Phase 4: FSR 4.1.1 (5-8 days)
7. Phase 5: Optimization (4-7 days)
8. Phase 6: Polish and release (3-5 days), the first working release
9. Phase 7: More upscalers for non-RDNA4 GPUs (after the first working release)
10. Phase 8: Windows release (only once Phase 7 is working)
11. Phase H (optional, after the first working release): DualSense HD haptics and adaptive triggers

Phases 1-3 have to go in this order because each one needs the one before it. The launcher could start any time, but it goes after FSR 3.1 so its Upscaling tab controls a working upscaler.

---

## How your test sessions work

Every phase ends with a short test you play on your PC while I monitor it from my end. I'll set up a small `bbtest` command for this:

- `bbtest start <phase>` launches bbhost as you normally play it and starts logging: the bbhost log, MangoHud frame times to a CSV, and a screenshot every few seconds of the game monitor only. Add `--record` to keep a rolling recording of the game monitor (via `gpu-screen-recorder`, at low overhead) so clips can be saved on demand.
- `bbtest mark` (bound to a hotkey) flags "something looked wrong here": it saves a screenshot, the last 30 seconds of video if recording, and a timestamp in the log.
- `bbtest clip` saves the last 30 seconds of video.
- `bbtest stop` closes everything and bundles the session into one folder for me.

While you play, I read the logs and screenshots live and message you if I see a crash, an error, or a frame-time problem. Afterwards I go through the clips and marks and send you a short pass/fail summary with what to fix next. You don't need to send me anything by hand. I can also use the virtual controller and F10 menu to change settings between runs myself, so you only need to be present for the gameplay parts.

---

## Git project and maintenance
Repository: [Heliasyu/bbhost-vk](https://github.com/Heliasyu/bbhost-vk) (private for now; GPLv3 requires publishing the source if builds are ever shared).
- **History:** keeps droogie's full bbhost history so upstream changes merge cleanly. The `upstream` remote points at `droogie/bbhost`.
- **Branches:** `main` is the stable, playable build. Each phase gets a branch (`phase-0-groundwork`, `phase-1-res-split`, ...) that merges into `main` once its test session passes. `upstream-sync` is where new bbhost releases are merged and tested first.
- **Versioning:** `<upstream version>-vk.<n>`, for example `v0.2.18-vk.1`, with a git tag and a GitHub release per playable build (Linux binary plus the wrapper script; never game files or FSR 4.1.1 assets).
- **Commits:** one logical change per commit, with messages that say what changed in the game. Each phase's test result goes in its pull request.
- **Upstream updates:** when bbhost puts out a release, I merge it into `upstream-sync`, rebuild, run a short test session, and then merge it into `main`. Conflicts in the renderer and launcher are the main ongoing cost.
- **Docs in the repo:** `README.md` (what bbhost-vk adds and credits), `docs/vk/PLAN.md` (this plan), `docs/vk/TESTING.md` (the `bbtest` sessions), `CHANGELOG.md`.
- **Safety:** `.gitignore` keeps out game dumps, saves, the FSR 4.1.1 assets built from your DLL, logs, and account files such as recovery codes.

---

## Phase 0: Groundwork (1-2 days)
- Commit the 120 fps patch on `main` of the bbhost-vk repo (on top of upstream `v0.2.18`) and tag it `v0.2.18-vk.0`.
- Add FSR-Vulkan as a submodule and build it into bbhost (CMake, shader tables).
- Add the Upscaler setting (Off / FSR 1 / FSR 3.1 / FSR 4.1.1), Quality presets (Native AA, Quality 67%, Balanced 58%, Performance 50%, Ultra Performance 33%), a sharpness slider and debug toggles, in the setup window, the F10 menu and the toml.
- Add debug tooling: a motion-vector view overlay, per-pass GPU timestamps, and a frame-capture option for side-by-side comparisons.
- **Milestone:** builds and launches with the new menu; nothing changes in-game yet.
- **Your test (5 min):** launch with `bbtest start p0`, open the setup window and the F10 menu, flip each new upscaler option, load your save, quit.
- **What I watch:** the log for errors and the toml for saved values. I take screenshots of both menus. Pass if every option saves and the game looks identical to stock.

## Phase 1: Split scene and UI resolution (3-5 days, the riskiest piece)
- Find the game's scene-to-UI scale pass (IFreemz's "decoupled UI" finding: the game scales its HDR scene up to UI size in one pass before post).
- Teach `live_resolution` to size scene targets (G-buffer, depth, HDR lighting) at render size while post-processing, Scaleform UI and display buffers stay at output size.
- In this first step, replace the game's own scale pass with a plain bilinear copy.
- **Milestone:** 4K output with the scene at, say, 1440p; the UI is pixel-sharp; no corruption in menus, cutscenes, the Hunter's Dream or online messages.
- **Your test (15 min):** `bbtest start p1`. Set the render scale to 67%, then visit the Hunter's Dream, a lamp menu, the inventory, a cutscene, and an area with messages and bloodstains. Toggle Native/67% once in each spot with F10. Drop a `bbtest mark` whenever something looks wrong.
- **What I watch:** paired screenshots at each mark (UI sharpness, corruption), the log for heap and YEBIS restart errors, and the MangoHud frame-time log. Pass if the UI is sharp and nothing is corrupt or crashes.

## Phase 2: Inputs for temporal upscaling (4-7 days)
1. **Scene matrices:** identify the per-frame scene constants (the 864-byte block bbport documents), keep the previous frame's view/projection, and detect camera cuts.
2. **Jitter:** a Halton(2,3) sub-pixel viewport offset only on draws into the main scene depth, removed before motion vectors are computed.
3. **Camera motion vectors:** a compute pass from depth plus the current and previous matrices, in double precision on the CPU side.
4. **Object motion:** start with re-drawing the game's own velocity draws (IFreemz approach). Then add previous-frame vertex positions for skinned meshes, cloth and weapons (bbport approach), gated so static map pieces are skipped.
5. **Depth/reactive:** pass the depth buffer, a reactive mask for particles, fog and transparent effects, and auto-exposure.
- **Milestone:** the motion-vector overlay looks correct while walking, turning, rolling and in boss fights.
- **Your test (20 min):** `bbtest start p2 --record`. Turn on the motion-vector overlay, then walk, spin the camera, roll, use a lantern and a Beast Blood pellet, and fight a few enemies and one boss. Each recording is a short clip (about 30 s, triggered by `bbtest clip`).
- **What I watch:** I analyze the clips for wrong or missing motion colors (objects that read as static while moving), and check the log for jitter and camera-cut events. Pass if camera and object motion look right everywhere you tested.

## Phase 3: FSR 3.1 temporal (3-5 days)
- Wire the FSR-Vulkan upscaler at the scene-to-UI point: inputs (color, depth, motion, jitter, reactive), history reset on cuts, loading screens and resolution changes.
- Turn off or adjust effects that fight temporal AA (the game's post AA, film grain ordering).
- Tune: ghosting on hair and cloth, Beast Blood and particles, lantern light, fog shimmer.
- **Milestone:** FSR 3.1 Quality at 4K looks clean in motion, with a known performance gain over native.
- **Your test (20-30 min):** `bbtest start p3 --record`. Run a set route (for example, from the Cathedral Ward lamp to Old Yharnam's entrance) once each at Native, FSR 3.1 Quality and FSR 3.1 Performance, with clips of hair and cloth, fog and particles. Then do one online session (a summon or co-op) to confirm nothing breaks.
- **What I watch:** MangoHud CSVs for fps and frame-time spikes per run, clips for ghosting and shimmer, and the log for history resets. I send you a comparison table. Pass if Quality looks at least as clean as native with a measured fps gain.

## Phase 4: FSR 4.1.1 (5-8 days)
- Port bbport's `tools/fsr4cap`: run the user's `amd_fidelityfx_upscaler_dx12.dll` once under GE-Proton/vkd3d-proton, record its passes (INT8 and FP8), and translate them to SPIR-V into `~/.local/share/bbhost/fsr4_411/`.
- **Build from your own DLL (as in bbport):** FSR 4.1.1 can't be shipped, so bbhost builds it once from a DLL you already have (yours is OptiScaler's `amd_fidelityfx_upscaler_dx12.dll`). On the Upscaling tab:
  - A file picker for the DLL, which also searches common places (`~/Downloads`, OptiScaler folders in your Steam games) and offers what it finds.
  - A check that reads the DLL's version and hash before building, and warns if it isn't 4.1.x.
  - A "Build" button that runs the capture under GE-Proton with live progress, and a status card showing "Ready", which DLL it was built from, and which variant (FP8 or INT8). A Rebuild button handles newer DLLs later.
  - If the assets are missing or broken, the upscaler drops to FSR 3.1 and the tab says why.
- Port the replay provider (`vk_fsr4.cpp` equivalent). Pick FP8 on RDNA4 (`VK_EXT_shader_float8`) and INT8 elsewhere, and fall back to FSR 3.1 if the assets are missing or the GPU lacks features.
- Verify output against the DLL is bit-exact on your 9070 XT. This would be the first real RDNA4 test of the FP8 path.
- **Milestone:** FSR 4.1.1 runs in-game, falls back cleanly, and the log says which variant is active.
- **Your test (15 min + 1 one-time step):** click "Build FSR 4.1.1 from my DLL..." once (I watch the build output live). Then `bbtest start p4 --record` and repeat the Phase 3 route with FSR 4.1.1 Quality and Performance. Also rename the asset folder once to confirm it falls back to FSR 3.1.
- **What I watch:** the capture and build output, the log line naming the active variant (FP8), the fallback behavior, and FSR 3.1 vs 4.1.1 clips and frame times side by side. Pass if 4.1.1 runs, looks better than 3.1, and the fallback works.

## Phase 5: Optimization (4-7 days)
- Rewrite the final FSR4 passes to store through workgroup memory (bbport got 2.3-3.5x on those passes), verified bit-exact.
- Async compute for motion vectors; avoid extra copies (write FSR output straight into the post-processing input).
- Cut CPU-side cost: bbport hit a CPU limit from guest memory copies, so profile bbhost's equivalent.
- Measure GPU time per pass and whole-frame time at 4K Quality/Balanced/Performance with the 120 cap; target under 3 ms for FSR4 at 4K on the 9070 XT.
- **Milestone:** a steady 120 fps at 4K output from a 1080p render (FSR 4.1.1 Performance), with Quality/Balanced as stretch goals, in the heaviest areas you pick (e.g. Yahar'gul, Forbidden Woods).
- **Your test (20 min, repeated after each optimization pass):** `bbtest start p5`. Run the same route in Yahar'gul and the Forbidden Woods at 4K with FSR 4.1.1 Quality and Balanced, capped at 120.
- **What I watch:** MangoHud frame times (average, 1% lows, spikes), bbhost's per-pass GPU timestamps, and CPU use per thread, compared against the previous build. Pass if both areas hold 120 fps with no new stutter.

## Phase L: Launcher overhaul (5-8 days, can run alongside Phases 2-5)
Redesign bbhost's setup window so it's streamlined and easy to use. The look follows Supermedo's Windows bbport launcher (clear tabs across the top as in bbhost today, section headings, dropdowns instead of rows of buttons, a status line and a big PLAY button), but the options stay bbhost's own. It doesn't copy their feature set.
- **Tabs (across the top):** Play, Display & frame rate, **Upscaling (new)**, Graphics & effects, Camera & input, PC enhancements, Patches & plugins, Online & account, Game files, Advanced.
- **What moves where:** today's Game / PC enhancements / Patches / Plugins tabs and the Game files, Online, Account, Start-up and display and Updates sections each get a page. The in-game F10 options (V-Sync, FOV, camera, mouse, effects) also show up on matching pages, so both places edit the same settings.
- **Upscaling page:** upscaler dropdown (Off / FSR 1 / FSR 3.1 / FSR 4.1.1), quality preset with the render resolution shown, output resolution, a sharpness slider, the FSR 4.1.1 DLL build card (Phase 4), fine-tuning toggles (fallback, reactive mask, history reset, game's own AA) and debug toggles.
- **Play page:** a ready check (game files found, game version 1.09, account signed in, server reachable, upscaler ready) and a status line summarizing the active settings.
- **Style:** a shared ImGui theme in bbhost's own "Paleblood" palette (moonlit slate `#0a0c11`, `#10131a`, `#161a23`, pale-blue accent `#a9bfe3`, blood-red `#8e1b2a` for PLAY), a serif heading font, consistent spacing, and correct sizing on both of your monitors (keeps the scaling wrapper).
- Built in ImGui inside bbhost, as today, so there's no new Python or toolkit dependency, and settings stay in the same toml files.
- **Milestone:** every current option is reachable from the new launcher, saves correctly, and the old launcher can be removed.
- **Your test (10 min):** `bbtest start pL`. Open each page on both monitors, change a few options per page, launch, and check that F10 shows the same values.
- **What I watch:** screenshots of every page at both scales, the toml before and after, and the log. Pass if nothing is missing, cut off, or mis-saved.

## Phase 6: Polish and hardening (3-5 days)
- Edge cases: photo mode/debug camera, cutscenes, loading screens, alt-tab, resolution changes mid-game, multiplayer summons, Chalice Dungeons.
- Leaving the upscaler off has to behave exactly like stock bbhost.
- Keep the scaling wrapper, write docs, and package the patches so they can be reapplied on each new bbhost release.
- **Milestone:** a release-ready build plus a patch set.
- **Your test (one normal play session):** play the way you usually do for an hour or so with `bbtest start p6` running quietly in the background: online, summons, a Chalice Dungeon, alt-tab, a resolution change. Use `bbtest mark` for anything odd.
- **What I watch:** the full session log, any crash dumps, and screenshots at your marks. Pass if a normal session has no crashes or visual bugs, and upscaler-off matches stock bbhost.

## Phase 7: More upscalers for non-RDNA4 GPUs (after the first working release)
Goal: anyone without an RDNA4 card gets a good temporal upscaler too. All of these reuse the Phase 1-2 inputs (scene/UI split, motion vectors, jitter, depth, reactive mask), so each one is mostly an adapter behind the existing upscaler interface.
- **FSR 4 INT8 (Steam Deck / RDNA2-3 compatible):** the INT8 path of FSR 4, built from the user's own DLL like Phase 4, for GPUs without FP8 (Steam Deck, RX 6000/7000). Needs its own performance presets, since INT8 costs more per frame on older cards.
- **DLSS (NVIDIA RTX):** through NVIDIA's Vulkan DLSS SDK. The SDK's license terms decide whether the runtime can ship with the project or has to be supplied by the user; checked before any code is written.
- **XeSS (Intel Arc, plus a fallback path for other GPUs):** through Intel's XeSS SDK, which has a Vulkan backend.
- If a license forbids shipping a runtime, it is user-provided instead: the Upscaling tab gets a "locate file" field for it, just like the FSR 4.1.1 DLL builder, and nothing proprietary is ever committed.
- The Upscaling tab and F10 list only what the current GPU supports, with FSR 3.1 always available as the fallback.
- **Milestone:** each upscaler matches FSR 3.1's image stability with no new ghosting, and switching between them in F10 works without a restart or crash.
- **Testing:** the user's RX 9070 XT covers FSR 4 INT8 and XeSS. DLSS and the Steam Deck need testers with that hardware (community testers, or a Steam Deck if one is available).

## Phase 8: Windows release (only once Phase 7 is working)
- bbhost is Linux-only today, so this means porting the host's platform layer (windowing and input are already SDL3, but memory mapping, threading, file paths and the PS4 runtime pieces are Linux-specific) to Windows.
- Native Windows builds of the same Vulkan renderer and upscalers; FSR 4 there can use the user's DLL directly instead of the captured-shader path.
- A Windows CI build and a Windows test pass.
- **Milestone:** the Windows build matches the Linux build in features, image quality and performance.
- **Testing:** the user, or community testers, on a Windows install.

## Phase H: DualSense HD haptics and adaptive triggers (optional, after the first working release)
The PS4 game only has DualShock 4 rumble, so haptic data has to be made, not converted 1:1. Can slot in any time after Phase 6; it doesn't touch rendering.
1. **Better rumble:** translate the game's rumble motor values into DualSense haptic waveforms (quickest).
2. **Audio-driven haptics:** pick game sound events (gunshots, parries, weapon impacts, footsteps, rain) and route them as haptic waveforms, the way PC ports like FF7 Rebirth do. Closest to the "HD haptics" feel.
3. **Adaptive triggers:** trigger resistance for the pistol, charged attacks and similar, through SDL3's DualSense support.
- Settings for each on a launcher page and in F10, all off for other controllers.
- **Platform note:** HD haptics need the DualSense's USB audio channels on Linux; Bluetooth haptics are much harder and may not be possible. Rumble and triggers work on both.
- **Milestone:** haptics and triggers feel right on a DualSense over USB, with no input lag and no effect on other controllers.
- **Your test:** a short play session on a DualSense over USB with `bbtest start ph`.

---

## Totals
- Roughly 5-7 weeks of work, spread over many sessions (the launcher overhaul adds about a week, partly in parallel).
- The pace depends on testing turnaround with you: about 8 test sessions in total (one per phase), plus short re-runs when something fails.

## Testing split
- **Me (no testing needed from you):** writing and porting the code, builds, compile and shader checks, unit tests, setup-window and F10 checks, and static-scene screenshots on your PC.
- **You (one session per phase, 5-30 min each):** the gameplay parts of each test above. I monitor every session live and send you the results.
- **Turnaround:** a phase only closes once its test passes. If it fails, I fix it from the logs and clips and ask for a quick re-run of just the failed part.

## Risks
- **Phase 1 resolution split:** the game's memory heaps and YEBIS restarts are fragile.
- **Object motion coverage:** some objects will ghost until handled.
- **FP8 path:** unproven on RDNA4.
- **Upstream updates:** each new bbhost release needs the patch set reapplied.
- **Legal:** FSR 4.1.1 assets have to be built from your own DLL and can't be shared.
- **Online play:** unaffected, since all of this is display-only.
- **DLSS/XeSS licensing:** NVIDIA's and Intel's SDK terms may limit what can ship in a GPLv3 project; checked first in Phase 7. Fallback (user-approved): the runtime files (DLL or .so) are user-provided, like the FSR 4.1.1 DLL, loaded at runtime and never bundled or committed.
- **Windows port:** bbhost's host layer is Linux-specific, so Phase 8 is a real porting job, not just a recompile; its size gets estimated once Phase 7 is done.
