# bbhost-vk vk.1-rc5 plan (from rc4 = 1f0a403, "build 3")

Written 2026-10-20 (America/Chicago), the night of the pre-authorised build. The rules: only items that
are clearly safe and measurable; no loss of picture or function; no menu or launcher toggles (debug env
reverts only); no change to the translator (the shader cache stays valid: fingerprint
cbb93a8ae24af3169f3cf6d437c1ac69 before and after); no HDR, CA, online or MangoHud changes; no game, AMD
or proprietary files.

## What limits heavy Old Yharnam outdoors in rc4 (the evidence)

From `rc4plain/full`, snapshot `snap-20261020-010838-m23` and the 300-flip windows around it:

| Measure | Value | Meaning |
|---|---|---|
| fps | 114.4 avg, 1% low 81.9, 1675 of 3430 frames over 8.5 ms | both chains are at the limit |
| bb-cp0 | 880-970 ms/s | the command processor is ~91% of a core |
| cp-thread split (per 2.6 s, 438k draws) | exec 2279 ms (5.2 us/draw), draw 1809, bind 1252 (2.86 us/draw), prefetch 228, pipeline 24 | the CP's per-draw cost is the CPU limit |
| bb-record | 830-940 ms/s, of which `recorder spin 1656 ms, replay 611 ms` a window | 72% of the recorder is PAUSE spinning |
| cp-backlog | 10 (the cap) | the game waits on the CP |
| GPU idle with work queued | 75 ms/s (1226 gaps/s), ~50 ms/s after every game submission (b33f77 = host_gpu_submit) at ~45-55 us each, whatever the span | a per-command-buffer boundary cost, ~0.5 ms/frame |
| pacing | limiter `late avg 54 us p99 95 us`, `spin 0.17 ms a frame`; `vblank woke late avg 55 us` | Linux's 50 us timer slack, on every sleep |
| placement | no affinity anywhere | the hot chain can be split across the two CCDs (~600 vs ~100 ns per cache-line hand-off) |
| menu "bb-cp0=2885 ms/s" | the lazily started TableMask (x4) and LeanBuilder (x2) workers inherit the name `bb-cp0` | an accounting artefact, not the CP |
| blit_ride | rode 29487, alone 7322, missed 1553 | works as designed (alone = the frame's submission had already gone; missed = a race with a 344 us vkQueueSubmit). Nothing to gain |

## Items for rc5

Saving: what it frees, where the snapshot shows it. "Risk" is to picture/function. All are on by default,
chosen automatically, and each has a debug env revert.

| # | Item | Expected saving | Risk | Effort | Evidence | Measure in rc5 (log label / F11 field) |
|---|---|---|---|---|---|---|
| 1 | **Recorder waits with MONITORX/MWAITX** instead of 4,000 PAUSEs (CPU audit M3). Monitor on g_published's line; the timer = the spin's own length in TSC ticks (calibrated at start); then the futex as before. Gated on CPUID 8000_0001h ECX[29]; the PAUSE loop stays where it is absent. | ~0.4-0.6 core freed (bb-record ~870 → ~300 ms/s); the SMT sibling's issue slots and package power for boost on the CCD that runs bb-cp0 | None to the picture (the same blocks, same order; the wake is a store to the monitored line). Function: a missed wake falls through to the futex as before | S | `recorder spin 1656 ms, replay 611 ms` per window | log `recorder: wait for the next block with MONITORX/MWAITX`; the 300-flip line `rc5 wait mwaitx: mwaitx N (M ended by a block)`; [cpu] `recorder_wait:`; [seconds] bb-record. Revert `BBHOST_RECORDER_MWAITX=0` |
| 2 | **CCD-aware placement** (M1). With ≥2 L3 domains of ≥6 CPUs: the hot chain (main thread, GXRenderThread, bb-cp0, bb-record, bb-submit, bb-present, bb-vblank) gets one whole CCD (larger L3, then prefcore ranking / CPPC / max freq); bb-compile (in game), bb-cs-optimize, bb-tablemask and bb-lean get the other; every other thread keeps the whole mask (explicitly reset, since threads inherit their creator's mask). | 0-0.7 ms/frame of bb-cp0 + bb-record (cache-line hand-offs: ~1,400 packets a frame plus the CP ↔ game ring); compiles stop evicting the hot L3 | None to the picture. Function: whole-CCD masks only (16 threads for 7 hot ones), so a pinned thread is never stuck behind one busy core; compiles get the whole mask back while booting/loading | S | No affinity in the code (`scePthreadSetaffinity` → 0, `host_thread_set_class` no-op on Linux); the recorder's 600 vs 100 ns note | log `placement:`; [cpu] `placement:` (hot/cold lists, threads per class, failures). Revert `BBHOST_AFFINITY=0` |
| 3 | **Timer slack 1 ns + lower spin floors** (M2): PR_SET_TIMERSLACK on the timing threads; the limiter's learnt margin floor 300 → 150 us (+100 instead of +200), the presenter's spin 300 → 150 us, only when the slack was set | ~0.15 ms/frame less main-thread spin, ~0.15 ms/frame less bb-present spin; wakes ~50 us more precise | None to the picture. Function: a late wake past the smaller margin shows as `late` in `pacing:` and the margin re-learns itself upward (it is clamped at 2 ms) | XS | `late avg 54-55 us` = the default 50 us slack, in every window | log `timer slack:`; [cpu] `timer_slack:`; `pacing:` late avg and spin. Revert `BBHOST_TIMER_SLACK=0` |
| 4 | **ThinLTO** for the bbhost executable (clang + lld); the decomp sources stay out (`-fno-lto`), their objects are byte-identical to rc4's flags (checked, 15 of 15) | 2-8% CPU on bb-cp0 / bb-record (render.cpp ↔ gpu.cpp ↔ recorder.cpp calls inline) | None: same semantics; the decomp's bit-exact maths is in non-LTO objects; the translator library is not LTO'd. Risk of a compiler bug, covered by ctest + selftests | XS | CPU audit §4 | [cpu] `cp_cost:` exec/draw/bind ns per draw (rc4: ~5,200 / ~4,100 / ~2,860). Build option `-DBBHOST_THINLTO=OFF` |
| 5 | **Page tables in ReBAR video memory** (GPU audit #3): the L1, the sink L2 and every L2 table in DEVICE_LOCAL\|HOST_VISIBLE memory when that heap is > 1 GiB; else system memory as before | 0.02-0.10 ms/frame of GPU (every remaining walk's two dependent loads stop crossing PCIe after each submission's cache invalidation) | None to the picture (the same bytes). The CPU only writes them (write-combined as before); RADV invalidates L2/vector/scalar caches at the start of every submission, so visibility is the same as now | XS | 142 of 712 VS code objects, 73 of 119 CS and all HS still walk | log `page tables:`; [walks] `page_tables: vram= system=`; [pass_stats] scene passes. Revert `BBHOST_TABLES_VRAM=0` |
| 6 | **Name the inherited workers**: `bb-tablemask`, `bb-lean` (they inherited `bb-cp0`) | Accounting only: [seconds] bb-cp0 is the CP again (the menu's 2885 ms/s) | None | XS | snapshot 011151 | [seconds] |
| 7 | **C3: the motion-blur name search** stops after its 8 log lines instead of running on every draw with a colour target | ~20-40 ns/draw (~0.03-0.05 ms/frame) | None (diagnostic only) | XS | CPU audit C3 | `cp_cost:` |
| 8 | **[cpu] snapshot section**: placement, timer slack, recorder wait, CP cost per draw since start and since the previous snapshot | Measurement for 1-7 | None | XS | — | F11 `[cpu]` |

Combined expectation for heavy Old Yharnam (an estimate, to be measured): bb-cp0's per-draw cost −3-10%
(ThinLTO, placement, the freed SMT sibling), bb-record −0.5 core, ~0.3 ms/frame less spinning on the
main and present threads, and a small GPU saving from #5. If the CP is the limit in that scene, that is
roughly +3-8 fps average and a better 1% low; if the GPU is, mostly the 1% lows and the latency.

## Considered and left out tonight (with reasons)

| Item | Why not now |
|---|---|
| GPU #1 compute constant buffers (cb_ssbo for CS) | 0.15-0.30 ms GPU, but the copy-version/window hazard rules of `resolve_stage_buffers` and YEBIS's self-writes need a game test; M-L effort. **First on the next list** |
| GPU #2 tess LS/HS walks, #10 translator ALU ops | change the translator → the fingerprint → the user's shader cache rebuilds; need an image comparison |
| GPU #6 pass-end barrier narrowing, #7 RT-copy elision | corruption risk; no image comparison possible on the box |
| GPU #5 async-compute FSR | rejected before (sync and pacing risk) |
| Submit batching | rejected in t9 (queued gaps only 66 → 53 ms/s) |
| Blit ride changes | works as designed (see above) |
| M6 vkQueueSubmit BO list | udmabuf's 64 MiB size_limit forces the 95 pieces |
| M5 lower CP backlog | latency, not fps; needs the A/B (`BBHOST_CP_BACKLOG`) |
| C2 key-shape memo, C1 bind-section work | need the profile to say which part of the 2.86 us/draw dominates; a wrong key = wrong pipeline |
| `-march=x86-64-v3` / znver3 | the release must stay portable; FMA would need global `-ffp-contract=off` |
| Frame pointers | ~1% cost; DWARF unwinding already works with perf |
| PGO | needs a profile run on the PC; next build |
| PS wave32 (`BBHOST_PS_WAVE32=32`), params WC (`BBHOST_PARAMS_WC=1`) | unknown sign: stay env A/Bs (TESTING.md) |

## Ranked candidates for the next build (by measured cost)

1. **CP draw path, the bind section: 2.86 us/draw ≈ 4 ms/frame of bb-cp0** (and the draw section's ~4 us).
   Profile with the bundled `cpuprofile.sh` (dwarf-bb-cp0.txt) first, then: cache per-stage descriptor
   resolution across draws when the stage's program, user data and T#/S# words are unchanged (C2 memo,
   exact-key only), drop the per-draw `DrawPacket` vector churn, move set lookup by content to a
   per-pipeline last-hit cache. Expected 0.5-1.5 ms/frame of CP.
2. **Split bb-cp0's draw work** (resolve on cp0, a second thread builds the packets/descriptor writes per
   pass), or record command buffers per pass in parallel on bb-record workers. Large redesign (`g.mu`
   covers all of draw_impl today); the gain is up to ~half the CP's 91% if the split is clean.
3. **GPU #1: compute constant buffers** (0.15-0.30 ms GPU).
4. **The per-command-buffer boundary (~0.5 ms/frame GPU idle with work queued, ~45-55 us per game
   submission)**: RADV's end-of-cmdbuf flushes (the busy meter's timestamps add PS/CS partial flush +
   INV_L2) — measure with the meter's timestamps off (an A/B), then fewer game command buffers per frame.
5. **PGO** (instrumented, Old Yharnam + Dream): 10-20% of the branchy CP code.
6. **GPU #2 tess walks** (0.10-0.20 ms), together with any other translator change, so the cache rebuilds
   once.
7. **Merging the small post passes** (needs replacement shaders: out of scope until then).
8. M4 (the ~5 ms hitch every 1-2 s) from the cpuprofile sched traces.

## Validation plan

- Clean CI build (`tools/ci_build.sh`, CI_PORTABLE=1, BBHOST_RELEASE=ON), ctest 53/53 (also under the
  validation layer).
- `--stream-selftest`, `=2` with BBHOST_FSR4_FEATURES=1, `=20`, with the validation layer and sync
  validation: pass, the same 10 upstream SYNC-HAZARD lines; the same with every rc5 revert set.
- The decomp objects with and without the LTO flags: byte-identical `.text`.
- The translator fingerprint unchanged.
- On the PC (TESTING.md "rc5 checks"): the new log lines, [cpu] fields, the same Old Yharnam spot as
  rc4 `010838`, and the env reverts as A/Bs.
