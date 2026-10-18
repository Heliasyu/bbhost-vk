# Known issues (bbhost-vk)

## Menus stop responding after a resolution change in the game's own Settings menu
- **Seen on:** upstream bbhost too, before bbhost-vk's changes (reported 2026-10-18).
- **What happens:** in Bloodborne's own in-game Settings menu, with bbhost's added PC entries
  (System > PC Graphics > Resolution), you pick a new resolution and the "keep this
  resolution?" yes/no question appears. From then on the menus take no input, while the game
  keeps running behind them. This is the game's menu, not the F10 overlay.
- **How it works (src/engine/live_resolution.cpp):** the change waits 1.5 s for the pick list to
  close (`kMenuSettleNs`), then resizes at the present step's quiet point. That rebuilds the
  scene targets, restarts YEBIS and sets every live Scaleform movie's viewport again. After
  that, `confirm_tick` opens the question modally on PC Graphics' own stack
  (`option_menu_confirm_ask`, engine/option_menu.cpp). If there is no answer within 10 s it
  answers NO itself, and 3 s later the change goes back regardless.
- **Likely causes (not confirmed; needs the game):**
  1. The pick list was still closing when the resize ran: the source notes it once "left it
     open, taking the keys meant for the question". The 1.5 s settle may not cover every case,
     for example a slower menu close or a held key.
  2. The question's movie is built while the Scaleform viewports are reset, so its input or
     hit-test area may not match the new size. This would affect the mouse more than keys or
     the pad.
  3. The modal on PC Graphics' stack does not take focus: the answer would then come only from
     the 10 s timeout.
- **Not likely bbhost-vk's:** the scene/UI split (Phase 1) only changes the scene's size in the
  same resize; upstream shows the bug without it. TESTING.md part G checks with and without
  `BBHOST_SCENE_SPLIT=0`.
- **Workaround:** change the resolution with F10 > Resolution or in the setup window, or wait
  about 13 s for the question to answer itself (NO) and close.
