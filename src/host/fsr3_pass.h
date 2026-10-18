#pragma once

// bbhost-vk Phase 3: FSR 3.1 at the game's scale pass (host/upscaler.h,
// ScenePass::Fsr31). The renderer calls it for the pass's draw, after the
// temporal inputs are made (host/temporal_inputs.h): FSR 3.1.5
// (host/fsr3_upscaler.h) upscales the frame's scene into an image of its
// own, which is copied into the pass's target, and the game's draw is
// skipped. When it cannot - no context (its creation failed, and is not
// retried until the sizes change), inputs missing this frame, a target it
// cannot copy into - it says why once and the game's draw runs as Phase 1's
// bilinear copy. FSR 4.1.1 selected uses this until Phase 4.

#include <cstdint>
#include <string>

namespace gpu {

struct RtImage;

// Under g.mu, the pass's render target bound (and initialised). True: the
// target holds the upscaled scene; the caller skips the draw and marks the
// target written.
bool fsr3_scale_pass_locked(RtImage* target);

// One line for the reports: dispatches, fallbacks and why, resets, memory.
std::string fsr3_report_locked();

}  // namespace gpu

// The presenter (host/window.cpp), for the GPU time line under the FPS
// counter (debug_upscaler_time): "FSR 3.1 1.23 ms", or empty.
std::string host_gpu_upscale_timing();
