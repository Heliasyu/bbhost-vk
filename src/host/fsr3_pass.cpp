// bbhost-vk Phase 3: FSR 3.1 at the game's scale pass (host/fsr3_pass.h).
//
// Per frame, at the scale pass (ccbf44a6) with FSR 3.1 (or FSR 4.1.1, until
// Phase 4) chosen and the scene split:
//   - the inputs host/temporal_inputs.cpp made a moment ago: the HDR scene
//     colour, the depth copy (R32F), the motion vectors, the reactive mask,
//     the jitter, the camera cut;
//   - a context for exactly this render and output size (a new one, and a
//     history reset, when either changes; the old one destroyed once the GPU
//     is past its last frame);
//   - the history reset on a camera cut, a new context, or a gap of frames;
//   - sharpening from the setting (FSR's RCAS), exposure FSR's own;
//   - FSR 3.1.5 records into the renderer's command buffer, then its output
//     (RGBA16F at the output size) is copied - or blitted, for another
//     format - into the pass's target, and the game's draw is skipped;
//   - with Show Upscaler GPU Time on, timestamps around the dispatch.
// Anything missing: the game's draw runs (Phase 1's bilinear copy) and the
// reason is logged when it changes.
#include "host/fsr3_pass.h"

#if BBHOST_HAVE_FSR_VULKAN

#include "hle/modules.h"
#include "host/fsr3_upscaler.h"
#include "host/gpu_internal.h"
#include "host/options.h"
#include "host/settings.h"
#include "host/temporal/fsr3_policy.h"
#include "host/temporal_inputs.h"
#include "host/upscaler_presets.h"
#include "log.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace gpu {
namespace {

enum class Fallback : int {
    None = 0,
    Disabled,       // BBHOST_FSR3=0
    NoInputs,       // no scene colour, depth or motion this frame
    Sizes,          // a ratio FSR 3.1 does not take
    Target,         // the target is not a plain 2D colour image of the output size
    Format,         // the target's format can neither be copied nor blitted to
    Context,        // the context could not be made for these sizes
    Dispatch,       // the dispatch failed
    kCount
};
const char* fallback_name(Fallback f) {
    switch (f) {
    case Fallback::None: return "none";
    case Fallback::Disabled: return "BBHOST_FSR3=0";
    case Fallback::NoInputs: return "no scene inputs this frame (colour, depth copy or motion vectors)";
    case Fallback::Sizes: return "the scene/output ratio is outside FSR 3.1's 1x..3x";
    case Fallback::Target: return "the scale pass's target is not a plain colour image of the output size";
    case Fallback::Format: return "the target's format takes neither a copy nor a blit from RGBA16F";
    case Fallback::Context: return "the FSR 3.1 context could not be made";
    case Fallback::Dispatch: return "the FSR 3.1 dispatch failed";
    default: return "?";
    }
}

struct Grave {
    std::unique_ptr<Fsr3Upscaler> fsr;
    std::uint64_t last = 0;
};

std::unique_ptr<Fsr3Upscaler> g_fsr;
std::vector<Grave> g_graves;
temporal::Fsr3Sizes g_failed_sizes;  // the context failed for these: not retried until they change
temporal::Fsr3History g_history;
std::uint64_t g_last_frame = ~0ull;  // the flip last dispatched (a second scale pass in it copies again)
Fallback g_last_fallback = Fallback::kCount;
bool g_said_411 = false;

struct Stats {
    std::uint64_t dispatches = 0, copies = 0, blits = 0, repeats = 0, resets = 0, contexts = 0;
    std::uint64_t fallbacks[static_cast<int>(Fallback::kCount)] = {};
};
Stats g_window;

// ---- the timer: timestamps around the dispatch, read once the submission retired ----
constexpr std::uint32_t kTimerSlots = 32;
VkQueryPool g_queries = VK_NULL_HANDLE;
bool g_timer_tried = false;
double g_tick_ns = 1.0;
std::uint64_t g_tick_mask = ~0ull;
std::uint64_t g_slot_frame[kTimerSlots] = {};  // the frame id each slot was written in; 0 free
std::uint32_t g_slot_next = 0;
double g_ms_sum = 0.0;
int g_ms_n = 0;
std::atomic<float> g_shown_ms{-1.0f};
std::chrono::steady_clock::time_point g_shown_at{};

bool timer_ready_locked() {
    if (g_timer_tried) return g_queries != VK_NULL_HANDLE;
    g_timer_tried = true;
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(g.phys, &props);
    std::uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g.phys, &n, nullptr);
    std::vector<VkQueueFamilyProperties> qf(n);
    vkGetPhysicalDeviceQueueFamilyProperties(g.phys, &n, qf.data());
    const std::uint32_t bits = g.family < n ? qf[g.family].timestampValidBits : 0;
    if (!bits || props.limits.timestampPeriod <= 0.0f) {
        host_log("fsr3: no GPU timestamps on this queue: the dispatch is not timed");
        return false;
    }
    g_tick_ns = props.limits.timestampPeriod;
    g_tick_mask = bits >= 64 ? ~0ull : (1ull << bits) - 1;
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = 2 * kTimerSlots;
    if (vkCreateQueryPool(g.device, &qi, nullptr, &g_queries) != VK_SUCCESS) g_queries = VK_NULL_HANDLE;
    return g_queries != VK_NULL_HANDLE;
}

void timer_collect_locked() {
    if (!g_queries) return;
    for (std::uint32_t k = 0; k < kTimerSlots; ++k) {
        if (!g_slot_frame[k] || g_slot_frame[k] > g.completed_submits) continue;
        std::uint64_t t[2] = {};
        if (vkGetQueryPoolResults(g.device, g_queries, 2 * k, 2, sizeof(t), t, sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            const double ms = static_cast<double>((t[1] - t[0]) & g_tick_mask) * g_tick_ns * 1e-6;
            if (ms >= 0.0 && ms < 1000.0) {
                g_ms_sum += ms;
                ++g_ms_n;
            }
        }
        g_slot_frame[k] = 0;
    }
    const auto now = std::chrono::steady_clock::now();
    if (g_ms_n && now - g_shown_at > std::chrono::milliseconds(500)) {
        const double avg = g_ms_sum / g_ms_n;
        g_shown_ms.store(static_cast<float>(avg));
        static std::uint64_t logged = 0;
        if (++logged % 120 == 1) host_log("fsr3: the dispatch takes %.2f ms on the GPU (%d frames)", avg, g_ms_n);
        g_ms_sum = 0.0;
        g_ms_n = 0;
        g_shown_at = now;
    }
}

// ---- the copy into the target ----
// Whether `format` (optimal tiling) can be a blit's destination, and RGBA16F its source.
bool blit_ok(VkFormat format) {
    static std::map<VkFormat, bool> known;
    auto it = known.find(format);
    if (it != known.end()) return it->second;
    VkFormatProperties dst{}, src{};
    vkGetPhysicalDeviceFormatProperties(g.phys, format, &dst);
    vkGetPhysicalDeviceFormatProperties(g.phys, VK_FORMAT_R16G16B16A16_SFLOAT, &src);
    const bool ok = (dst.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) && (src.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT);
    known[format] = ok;
    return ok;
}

void barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkAccessFlags src_access, VkPipelineStageFlags dst, VkAccessFlags dst_access) {
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = src_access;
    mb.dstAccessMask = dst_access;
    vkCmdPipelineBarrier(cmd, src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

bool copy_output(VkCommandBuffer cmd, const Fsr3Upscaler& f, RtImage& target) {
    barrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    if (target.format == f.output_format()) {
        VkImageCopy c{};
        c.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.dstSubresource = c.srcSubresource;
        c.extent = {f.output_w(), f.output_h(), 1};
        vkCmdCopyImage(cmd, f.output(), VK_IMAGE_LAYOUT_GENERAL, target.image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
        ++g_window.copies;
    } else {
        VkImageBlit b{};
        b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        b.dstSubresource = b.srcSubresource;
        b.srcOffsets[1] = {static_cast<std::int32_t>(f.output_w()), static_cast<std::int32_t>(f.output_h()), 1};
        b.dstOffsets[1] = b.srcOffsets[1];
        vkCmdBlitImage(cmd, f.output(), VK_IMAGE_LAYOUT_GENERAL, target.image, VK_IMAGE_LAYOUT_GENERAL, 1, &b, VK_FILTER_NEAREST);
        ++g_window.blits;
    }
    barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    return true;
}

bool fall_back(Fallback why) {
    ++g_window.fallbacks[static_cast<int>(why)];
    if (why != g_last_fallback) {
        g_last_fallback = why;
        host_log("fsr3: the scale pass is drawn bilinear this frame: %s (said once until it changes)", fallback_name(why));
    }
    return false;
}

void dig_locked() {
    for (auto it = g_graves.begin(); it != g_graves.end();) {
        if (it->last <= g.completed_submits) {
            it->fsr->destroy();
            it = g_graves.erase(it);
        } else {
            it->fsr->retire(g.completed_submits);
            ++it;
        }
    }
}

int env_sign(const char* name, int axis) {
    // BBHOST_FSR_JITTER_SIGN=x, y or xy: flip that axis' jitter for FSR.
    const char* e = std::getenv(name);
    if (!e) return 1;
    return std::strchr(e, axis == 0 ? 'x' : 'y') ? -1 : 1;
}

}  // namespace

bool fsr3_scale_pass_locked(RtImage* target) {
    static const bool disabled = [] {
        const char* e = std::getenv("BBHOST_FSR3");
        return e && e[0] == '0';
    }();
    dig_locked();
    if (g_fsr) g_fsr->retire(g.completed_submits);
    timer_collect_locked();
    if (disabled) return fall_back(Fallback::Disabled);
    // The settings: a copy once they moved.
    static std::uint64_t serial = ~0ull;
    static HostSettings hs;
    if (host_opt_serial() != serial) {
        serial = host_opt_serial();
        hs = host_settings();
    }
    if (hs.upscaler == upscaler::kFsr411 && !g_said_411) {
        g_said_411 = true;
        host_log("fsr3: FSR 4.1.1 is chosen, but it comes in Phase 4 (built from your own DLL): FSR 3.1 upscales until then");
    }
    const UpscalerInputs& in = temporal_inputs_locked();
    if (!target || target->depth || target->layers != 1) return fall_back(Fallback::Target);
    if (!in.valid || !in.color.image || !in.depth_copy.image || !in.motion.image || in.frame != hle_video_flip_count())
        return fall_back(Fallback::NoInputs);
    const temporal::Fsr3Sizes sizes{in.render_w, in.render_h, target->width, target->height};
    if (!temporal::fsr3_sizes_ok(sizes) || in.color.width < in.render_w || in.color.height < in.render_h) return fall_back(Fallback::Sizes);
    if (target->format != VK_FORMAT_R16G16B16A16_SFLOAT && !blit_ok(target->format)) return fall_back(Fallback::Format);
    // The context.
    if (!g_fsr || !temporal::fsr3_context_fits({g_fsr->render_w(), g_fsr->render_h(), g_fsr->output_w(), g_fsr->output_h()}, sizes)) {
        if (sizes == g_failed_sizes) return fall_back(Fallback::Context);
        if (g_fsr) {
            g_graves.push_back({std::move(g_fsr), 0});
            g_graves.back().last = g_graves.back().fsr->last_frame_id();
        }
        const auto t0 = std::chrono::steady_clock::now();
        auto made = std::make_unique<Fsr3Upscaler>();
        std::string why;
        if (!made->create(g.phys, g.device, sizes.render_w, sizes.render_h, sizes.output_w, sizes.output_h, &why)) {
            g_failed_sizes = sizes;
            host_log("fsr3: %s (scene %ux%u, output %ux%u): the bilinear copy instead", why.c_str(), sizes.render_w, sizes.render_h, sizes.output_w,
                     sizes.output_h);
            return fall_back(Fallback::Context);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        host_log("fsr3: FSR 3.1.5 context for scene %ux%u -> output %ux%u (%.2fx), %.1f MiB, made in %.0f ms; target format %d (%s)",
                 sizes.render_w, sizes.render_h, sizes.output_w, sizes.output_h, static_cast<double>(sizes.output_w) / sizes.render_w,
                 static_cast<double>(made->memory_bytes()) / (1024.0 * 1024.0), ms, static_cast<int>(target->format),
                 target->format == VK_FORMAT_R16G16B16A16_SFLOAT ? "copied" : "blitted");
        g_fsr = std::move(made);
        g_failed_sizes = {};
        g_history = {};
        ++g_window.contexts;
    }
    begin_recording_locked();
    render_end_pass_locked();
    VkCommandBuffer cmd = g_cmd();
    if (in.frame == g_last_frame) {
        // A second scale pass in the same frame: the same picture again.
        ++g_window.repeats;
        copy_output(cmd, *g_fsr, *target);
        return true;
    }
    const temporal::Fsr3Reset reset = temporal::fsr3_reset(g_history, in.frame, in.reset, false);
    if (reset != temporal::Fsr3Reset::None) {
        ++g_window.resets;
        static temporal::Fsr3Reset said = temporal::Fsr3Reset::None;
        static std::uint64_t said_at = 0;
        // Each kind once, then at most every 600 flips (cuts repeat during loads).
        if (reset != said || in.frame > said_at + 600) {
            said = reset;
            said_at = in.frame;
            host_log("fsr3: history reset at flip %llu: %s", static_cast<unsigned long long>(in.frame), temporal::fsr3_reset_name(reset));
        }
    }
    Fsr3Frame fr;
    fr.cmd = cmd;
    fr.color = in.color;
    fr.depth = in.depth_copy;
    fr.motion = in.motion;
    if (hs.upscale_reactive) fr.reactive = in.reactive;
    static const int sign_x = env_sign("BBHOST_FSR_JITTER_SIGN", 0), sign_y = env_sign("BBHOST_FSR_JITTER_SIGN", 1);
    const temporal::Fsr3Conventions conv = temporal::fsr3_conventions(in.jitter_x, in.jitter_y, sign_x, sign_y);
    fr.jitter_x = conv.jitter_x;
    fr.jitter_y = conv.jitter_y;
    fr.mv_scale_x = conv.mv_scale_x;
    fr.mv_scale_y = conv.mv_scale_y;
    fr.reset = reset != temporal::Fsr3Reset::None;
    const temporal::Fsr3Sharpen sharpen = temporal::fsr3_sharpen(hs.upscale_sharpness);
    fr.sharpen = sharpen.enable;
    fr.sharpness = sharpen.sharpness;
    fr.frame_ms = in.frame_ms;
    const temporal::Fsr3Camera cam = temporal::fsr3_camera(in.near_plane, in.far_plane, in.fov_y);
    fr.near_plane = cam.near_plane;
    fr.far_plane = cam.far_plane;
    fr.fov_y = cam.fov_y;
    fr.frame_id = g.flushes + 1;  // this recording's serial + 1: complete once completed_submits reaches it
    // Every input was written by commands before this point.
    barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    std::int32_t slot = -1;
    if (hs.debug_upscaler_time && timer_ready_locked() && !g_slot_frame[g_slot_next % kTimerSlots]) {
        slot = static_cast<std::int32_t>(g_slot_next++ % kTimerSlots);
        vkCmdResetQueryPool(cmd, g_queries, 2 * static_cast<std::uint32_t>(slot), 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_queries, 2 * static_cast<std::uint32_t>(slot));
    }
    std::string why;
    const bool ok = g_fsr->record(fr, &why);
    if (slot >= 0) {
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_queries, 2 * static_cast<std::uint32_t>(slot) + 1);
        g_slot_frame[slot] = fr.frame_id;
    }
    if (!ok) {
        static int said = 0;
        if (said++ < 8) host_log("fsr3: %s", why.c_str());
        g_history = {};  // the next dispatch starts over
        return fall_back(Fallback::Dispatch);
    }
    copy_output(cmd, *g_fsr, *target);
    g_last_frame = in.frame;
    ++g_window.dispatches;
    if (g_last_fallback != Fallback::None) {
        if (g_last_fallback != Fallback::kCount) host_log("fsr3: upscaling again at flip %llu", static_cast<unsigned long long>(in.frame));
        else
            host_log("fsr3: first FSR 3.1 frame at flip %llu: scene %ux%u -> %ux%u, jitter %u phases, reactive mask %s, sharpening %s %.1f",
                     static_cast<unsigned long long>(in.frame), in.render_w, in.render_h, target->width, target->height, in.jitter_phases,
                     fr.reactive.image ? "on" : hs.upscale_reactive ? "on (none this frame)" : "off", fr.sharpen ? "on" : "off",
                     static_cast<double>(fr.sharpness));
        g_last_fallback = Fallback::None;
    }
    std::uint64_t frames = g_window.dispatches;
    for (std::uint64_t v : g_window.fallbacks) frames += v;
    if (frames >= 3000) {
        host_log("%s", fsr3_report_locked().c_str());
        g_window = Stats{};
    }
    return true;
}

std::string fsr3_report_locked() {
    const Stats& w = g_window;
    std::string falls;
    for (int k = 1; k < static_cast<int>(Fallback::kCount); ++k) {
        if (!w.fallbacks[k]) continue;
        char part[160];
        std::snprintf(part, sizeof(part), "%s%llu %s", falls.empty() ? "" : ", ", static_cast<unsigned long long>(w.fallbacks[k]),
                      fallback_name(static_cast<Fallback>(k)));
        falls += part;
    }
    char buf[640];
    std::snprintf(buf, sizeof(buf), "fsr3: %llu dispatches (%llu copied, %llu blitted, %llu repeats), %llu resets, %llu contexts made, %.1f MiB; "
                  "bilinear frames: %s; GPU %.2f ms",
                  static_cast<unsigned long long>(w.dispatches), static_cast<unsigned long long>(w.copies), static_cast<unsigned long long>(w.blits),
                  static_cast<unsigned long long>(w.repeats), static_cast<unsigned long long>(w.resets), static_cast<unsigned long long>(w.contexts),
                  g_fsr ? static_cast<double>(g_fsr->memory_bytes()) / (1024.0 * 1024.0) : 0.0, falls.empty() ? "none" : falls.c_str(),
                  static_cast<double>(g_shown_ms.load()));
    return buf;
}

}  // namespace gpu

std::string host_gpu_upscale_timing() {
    const float ms = gpu::g_shown_ms.load();
    if (ms < 0.0f) return std::string();
    char buf[48];
    std::snprintf(buf, sizeof(buf), "FSR 3.1 %.2f ms", static_cast<double>(ms));
    return buf;
}

#else  // !BBHOST_HAVE_FSR_VULKAN: upscaler_scene_pass() never asks for FSR 3.1

namespace gpu {
bool fsr3_scale_pass_locked(RtImage*) { return false; }
std::string fsr3_report_locked() { return "fsr3: not built (BBHOST_FSR_VULKAN=OFF)"; }
}  // namespace gpu
std::string host_gpu_upscale_timing() { return std::string(); }

#endif
