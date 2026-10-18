#pragma once

// bbhost-vk Phase 3: the rules of the FSR 3.1 pass (host/fsr3_pass.cpp), as
// plain data with no Vulkan, so tests/fsr3_policy_test.cpp takes them as
// they are: when a context fits, when the history resets, what sharpening
// the setting asks for, and how bbhost's conventions (host/upscaler_inputs.h)
// map to FSR's.

#include <cstdint>

namespace temporal {

struct Fsr3Sizes {
    std::uint32_t render_w = 0, render_h = 0, output_w = 0, output_h = 0;
    bool operator==(const Fsr3Sizes& o) const {
        return render_w == o.render_w && render_h == o.render_h && output_w == o.output_w && output_h == o.output_h;
    }
    bool operator!=(const Fsr3Sizes& o) const { return !(*this == o); }
};

// FSR 3.1 upscales by 1x (native AA) to 3x (Ultra Performance) on each side.
// Anything else - a scene larger than the output, a ratio past 3 - keeps the
// bilinear copy. A small slack: render sizes are rounded to multiples of 8.
inline bool fsr3_sizes_ok(const Fsr3Sizes& s) {
    if (!s.render_w || !s.render_h || !s.output_w || !s.output_h) return false;
    if (s.render_w > s.output_w || s.render_h > s.output_h) return false;
    return s.output_w <= 3 * s.render_w + 8 && s.output_h <= 3 * s.render_h + 8;
}

// A context is made for exact sizes: FSR's resources are sized from its
// maximum render size, so making it for the output size would hold 4K
// resources at every preset. A preset or resolution change makes a new one
// (and the history starts over, as it would anyway).
inline bool fsr3_context_fits(const Fsr3Sizes& made, const Fsr3Sizes& want) { return made == want; }

enum class Fsr3Reset : int { None = 0, NewContext, Cut, Gap, Requested };
inline const char* fsr3_reset_name(Fsr3Reset r) {
    switch (r) {
    case Fsr3Reset::None: return "none";
    case Fsr3Reset::NewContext: return "a new context";
    case Fsr3Reset::Cut: return "a camera cut";
    case Fsr3Reset::Gap: return "frames without the upscaler";
    case Fsr3Reset::Requested: return "asked for";
    }
    return "?";
}

// The history of one context: the last frame (flip) it upscaled.
struct Fsr3History {
    bool primed = false;
    std::uint64_t last_frame = 0;
};

// Whether this frame's dispatch resets the history, and why. A gap: the
// upscaler did not run for more than `max_gap` flips (a load, a menu that
// hides the scene, a fallback frame) - its history is of another picture.
// Call once per dispatch; it records the frame.
inline Fsr3Reset fsr3_reset(Fsr3History& h, std::uint64_t frame, bool cut, bool requested, std::uint64_t max_gap = 3) {
    Fsr3Reset r = Fsr3Reset::None;
    if (!h.primed) r = Fsr3Reset::NewContext;
    else if (cut) r = Fsr3Reset::Cut;
    else if (frame > h.last_frame + max_gap || frame < h.last_frame) r = Fsr3Reset::Gap;
    else if (requested) r = Fsr3Reset::Requested;
    h.primed = true;
    h.last_frame = frame;
    return r;
}

// The setting's sharpness (0..1, settings upscale_sharpness) as FSR's RCAS:
// 0 turns sharpening off; otherwise FSR's own 0..1 (1 the sharpest).
struct Fsr3Sharpen {
    bool enable = false;
    float sharpness = 0.0f;
};
inline Fsr3Sharpen fsr3_sharpen(float setting) {
    const float s = setting < 0.0f ? 0.0f : setting > 1.0f ? 1.0f : setting;
    return {s > 0.0f, s};
}

// bbhost's jitter is the viewport shift of the scene's draws in render
// pixels, +x right, +y down (host/upscaler_inputs.h): the picture's content
// moved by (+x, +y). FSR's jitterOffset is the same quantity - the offset its
// samples add to the projection, 2x/w and -2y/h in D3D clip space, which
// moves content right and down in texture space. So it is passed as it is;
// `sign_x` / `sign_y` (BBHOST_FSR_JITTER_SIGN for a test session) flip it
// should a hardware test show the picture wobbling in step with the jitter.
// Motion vectors are render pixels, current to previous, +y down - FSR's
// own convention once scaled by motionVectorScale / renderSize: the scale is 1.
struct Fsr3Conventions {
    float jitter_x = 0.0f, jitter_y = 0.0f;
    float mv_scale_x = 1.0f, mv_scale_y = 1.0f;
};
inline Fsr3Conventions fsr3_conventions(float jitter_x, float jitter_y, int sign_x = 1, int sign_y = 1) {
    Fsr3Conventions c;
    c.jitter_x = sign_x < 0 ? -jitter_x : jitter_x;
    c.jitter_y = sign_y < 0 ? -jitter_y : jitter_y;
    return c;
}

// FSR's camera inputs must be finite and positive; the game's are near 0.05,
// far 3000, a vertical FOV from the projection. Bad values get defaults.
struct Fsr3Camera {
    float near_plane = 0.05f, far_plane = 3000.0f, fov_y = 1.0f;
};
inline Fsr3Camera fsr3_camera(float near_plane, float far_plane, float fov_y) {
    Fsr3Camera c;
    const auto ok = [](float v) { return v == v && v > 0.0f && v < 1e30f; };
    if (ok(near_plane)) c.near_plane = near_plane;
    if (ok(far_plane) && far_plane > c.near_plane) c.far_plane = far_plane;
    if (ok(fov_y) && fov_y < 3.1f) c.fov_y = fov_y;
    return c;
}

}  // namespace temporal
