// bbhost-vk Phase 2: the temporal upscaler's inputs (host/temporal_inputs.h,
// host/upscaler_inputs.h). The rules are host/temporal/*.h (unit-tested);
// this is where the renderer's draws meet them.
//
// Per frame, between two of the game's scale passes (ccbf44a6, the end of
// the scene):
//   - every 864-byte constant buffer a draw binds is parsed as the scene
//     constants (bbport) and counted by camera; the most-bound camera is the
//     frame's (IFreemz);
//   - the scene depth is learned as the depth bound while drawing into the
//     scene colour (the scale pass's source, IFreemz), or before that by its
//     size (the render size);
//   - the scene's geometry draws with that depth get the frame's Halton
//     jitter on their viewport (temporal upscalers only);
//   - the game's own object-velocity draws (IFreemz's three pixel shaders)
//     are noted, their 160x90 target and packing scale kept;
//   - the scene colour is copied before its first blended draw (the opaque
//     snapshot the backends' reactive masks are made from);
//   - at the scale pass, one compute pass (shaders/motion.comp) writes the
//     motion vectors from the depth and the two cameras (and the velocity
//     target), the camera history moves on (cuts logged), the jitter phase
//     advances.
#include "host/temporal_inputs.h"
#include "host/pass_timer.h"

#include "engine/scene_split.h"
#include "hle/modules.h"
#include "host/gpu_internal.h"
#include "host/options.h"
#include "host/settings.h"
#include "host/shaders/motion.spv.h"
#include "host/shaders/reactive.spv.h"
#include "host/temporal/halton.h"
#include "host/temporal/motion_classify.h"
#include "host/upscaler_presets.h"
#include "log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace gpu {
namespace {

bool env_off(const char* name) {
    const char* e = std::getenv(name);
    return e && e[0] == '0';
}

struct Config {
    std::uint64_t flip = ~0ull;
    bool split = false, temporal = false, debug_view = false;
    bool watching = false, jitter = false, object_motion = true, opaque = true, reactive = true;
    std::uint32_t render_w = 0, render_h = 0, output_w = 0, output_h = 0;
    int upscaler = 0;
};
Config g_cfg;

// An image the module owns (motion vectors, opaque snapshot).
struct Owned {
    VkImage image = VK_NULL_HANDLE;
    ImageMemory memory;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint32_t w = 0, h = 0;
    bool initialised = false;
};
// Images replaced on a size change, destroyed some flips later: the
// presenter's command buffer may still sample the motion image.
struct Grave {
    Owned img;
    std::uint64_t flip = 0;
};
std::vector<Grave> g_graves;

void bury(Owned& o, std::uint64_t flip) {
    if (o.image) g_graves.push_back({o, flip});
    o = Owned{};
}

void dig(std::uint64_t flip) {
    for (auto it = g_graves.begin(); it != g_graves.end();) {
        if (flip > it->flip + 8) {
            if (it->img.view) defer_destroy_private_view(it->img.view);
            defer_destroy_image(it->img.image, it->img.memory);
            it = g_graves.erase(it);
        } else {
            ++it;
        }
    }
}

bool make_owned(Owned& o, VkFormat format, std::uint32_t w, std::uint32_t h, VkImageUsageFlags usage, std::uint64_t flip) {
    if (o.image && o.format == format && o.w == w && o.h == h) return true;
    bury(o, flip);
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(g.device, &ci, nullptr, &o.image) != VK_SUCCESS) {
        o = Owned{};
        return false;
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(g.device, o.image, &req);
    if (!image_memory_alloc(req, o.memory)) {
        vkDestroyImage(g.device, o.image, nullptr);
        o = Owned{};
        return false;
    }
    vkBindImageMemory(g.device, o.image, o.memory.memory, o.memory.offset);
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = o.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(g.device, &vci, nullptr, &o.view) != VK_SUCCESS) o.view = VK_NULL_HANDLE;
    o.format = format;
    o.w = w;
    o.h = h;
    o.initialised = false;
    return true;
}

void to_general(Owned& o) {
    if (o.initialised) return;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = o.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(g_cmd(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    o.initialised = true;
}

void memory_barrier(VkPipelineStageFlags src, VkAccessFlags src_access, VkPipelineStageFlags dst, VkAccessFlags dst_access) {
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = src_access;
    mb.dstAccessMask = dst_access;
    vkCmdPipelineBarrier(g_cmd(), src, dst, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

// ---- the motion pass ----
struct MotionPush {
    float reproject[12];
    float proj[4], prev_proj[4];
    float jitter[2];
    float velocity_scale[2];
    std::uint32_t extent[2];
    std::uint32_t flags, pad;
};
static_assert(sizeof(MotionPush) <= 128, "push constants");

// A compute pipeline of the module's: its set layout (the bindings' types
// in order), layout and pipeline.
struct Compute {
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    bool tried = false;
};

bool make_compute(Compute& c, const std::uint32_t* words, std::size_t bytes, const VkDescriptorType* types, std::uint32_t count,
                  std::uint32_t push_bytes) {
    VkDescriptorSetLayoutBinding binds[8] = {};
    for (std::uint32_t k = 0; k < count && k < 8; ++k) binds[k] = {k, types[k], 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo sli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sli.bindingCount = count;
    sli.pBindings = binds;
    if (vkCreateDescriptorSetLayout(g.device, &sli, nullptr, &c.set_layout) != VK_SUCCESS) return false;
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, push_bytes};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &c.set_layout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &range;
    if (vkCreatePipelineLayout(g.device, &pli, nullptr, &c.layout) != VK_SUCCESS) return false;
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = bytes;
    smi.pCode = words;
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(g.device, &smi, nullptr, &module) != VK_SUCCESS) return false;
    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = module;
    ci.stage.pName = "main";
    ci.layout = c.layout;
    const bool ok = vkCreateComputePipelines(g.device, g.cache, 1, &ci, nullptr, &c.pipeline) == VK_SUCCESS;
    vkDestroyShaderModule(g.device, module, nullptr);
    if (!ok) c.pipeline = VK_NULL_HANDLE;
    return ok;
}

// Writes `count` image descriptors (binding k = infos[k], types[k]).
void write_set(VkDescriptorSet set, const VkDescriptorImageInfo* infos, const VkDescriptorType* types, std::uint32_t count) {
    VkWriteDescriptorSet w[8] = {};
    for (std::uint32_t k = 0; k < count && k < 8; ++k) {
        w[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[k].dstSet = set;
        w[k].dstBinding = k;
        w[k].descriptorCount = 1;
        w[k].descriptorType = types[k];
        w[k].pImageInfo = &infos[k];
    }
    vkUpdateDescriptorSets(g.device, count, w, 0, nullptr);
}

constexpr VkDescriptorType kMotionTypes[5] = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                              VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
constexpr VkDescriptorType kReactiveTypes[4] = {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLER, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
Compute g_motion_pass, g_reactive_pass;

bool motion_pipeline_locked() {
    if (!g_motion_pass.tried) {
        g_motion_pass.tried = true;
        make_compute(g_motion_pass, k_motion_spv, sizeof(k_motion_spv), kMotionTypes, 5, sizeof(MotionPush));
        host_log("motion: the motion-vector pass %s",
                 g_motion_pass.pipeline ? "is ready" : "could not be made (pipeline creation failed); no motion vectors");
    }
    return g_motion_pass.pipeline != VK_NULL_HANDLE;
}

// ---- the reactive pass ----
struct ReactivePush {
    std::uint32_t extent[2];
    float scale, threshold, binary_value;
    std::uint32_t pad[3];
};

bool reactive_pipeline_locked() {
    if (!g_reactive_pass.tried) {
        g_reactive_pass.tried = true;
        // Its R8 storage image needs shaderStorageImageExtendedFormats (gpu.cpp enables it where there is one).
        VkPhysicalDeviceFeatures f{};
        vkGetPhysicalDeviceFeatures(g.phys, &f);
        if (!f.shaderStorageImageExtendedFormats) {
            host_log("motion: no reactive mask: the GPU lacks shaderStorageImageExtendedFormats");
            return false;
        }
        make_compute(g_reactive_pass, k_reactive_spv, sizeof(k_reactive_spv), kReactiveTypes, 4, sizeof(ReactivePush));
        host_log("motion: the reactive-mask pass %s", g_reactive_pass.pipeline ? "is ready" : "could not be made; no reactive mask");
    }
    return g_reactive_pass.pipeline != VK_NULL_HANDLE;
}

float env_float(const char* name, float fallback) {
    const char* e = std::getenv(name);
    if (!e || !*e) return fallback;
    char* end = nullptr;
    const float v = std::strtof(e, &end);
    return end != e && std::isfinite(v) ? v : fallback;
}

// ---- per-frame state ----
temporal::CameraHistory g_history;
temporal::MovingGate g_gate;
temporal::Jitter g_jitter;
std::uint64_t g_frames = 0;  // scale passes seen while watching (the jitter's counter)
float g_last_raw[16] = {};   // the last scene block's view + projection, to skip parsing repeats
bool g_last_raw_ok = false;
temporal::SceneCamera g_last_cam;

std::uint64_t g_scene_color_base = 0;  // the last scale pass's source
std::uint64_t g_scene_depth_base = 0;  // learned: the depth drawn with into the scene colour
bool g_depth_learned = false;
std::uint64_t g_frame_depth_base = 0;  // this frame's scene depth (the last jittered or learned draw's)
std::uint64_t g_velocity_base = 0;     // this frame's velocity target
float g_velocity_scale[2] = {};
bool g_velocity_scale_ok = false;
bool g_velocity_this_frame = false;
bool g_opaque_taken = false;
Owned g_motion, g_opaque, g_depth_copy, g_reactive;
std::uint64_t g_motion_flip = 0;  // the flip the motion image was last written at
bool g_motion_has = false;

UpscalerInputs g_inputs;

// Counters for the logs.
struct Stats {
    std::uint64_t frames = 0, motion_passes = 0, jittered = 0, scene_blocks = 0, blocks_rejected = 0, cameras_multi = 0;
    std::uint64_t velocity_draws = 0, velocity_after_scale = 0, opaque_snapshots = 0, blended_scene = 0;
    std::uint64_t skeleton_draws = 0, small_moving = 0, small_static = 0, skeleton_moving = 0, skeleton_static = 0;
    std::uint64_t no_depth = 0, cuts = 0, reactive_passes = 0;
};
Stats g_stats, g_window;  // the run, and since the last summary
temporal::Cut g_last_cut_logged = temporal::Cut::None;
std::uint64_t g_cut_repeats = 0;
bool g_jitter_was = false;
std::uint32_t g_phases_logged = 0;
bool g_size_mismatch_logged = false;

std::uint64_t fnv(const void* p, std::size_t n) {
    const auto* b = static_cast<const std::uint8_t*>(p);
    std::uint64_t h = 1469598103934665603ull;
    for (std::size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

}  // namespace

void temporal_frame_locked(std::uint64_t flip) {
    if (flip == g_cfg.flip) return;
    g_cfg.flip = flip;
    static const bool no_motion = env_off("BBHOST_MOTION_VECTORS"), no_jitter = env_off("BBHOST_JITTER"),
                      no_objects = env_off("BBHOST_OBJECT_MOTION"), no_opaque = env_off("BBHOST_OPAQUE_SNAPSHOT"),
                      no_reactive = env_off("BBHOST_REACTIVE");
    // The settings are a copy under a lock: once a flip, and only when they moved.
    static std::uint64_t serial = ~0ull;
    static HostSettings hs;
    const std::uint64_t now = host_opt_serial();
    if (now != serial) {
        serial = now;
        hs = host_settings();
    }
    g_cfg.split = scene_split_active();
    g_cfg.upscaler = hs.upscaler;
    g_cfg.temporal = hs.upscaler == upscaler::kFsr31 || hs.upscaler == upscaler::kFsr411;
    g_cfg.debug_view = hs.debug_motion_vectors;
    g_cfg.watching = g_cfg.split && (g_cfg.temporal || g_cfg.debug_view) && !no_motion;
    g_cfg.jitter = g_cfg.watching && g_cfg.temporal && !no_jitter && hs.upscale_jitter;
    g_cfg.object_motion = !no_objects;
    g_cfg.opaque = !no_opaque && g_cfg.temporal;
    g_cfg.reactive = g_cfg.opaque && hs.upscale_reactive && !no_reactive;
    scene_split_render_size(&g_cfg.render_w, &g_cfg.render_h);
    g_cfg.output_w = static_cast<std::uint32_t>(hs.res_width);
    g_cfg.output_h = static_cast<std::uint32_t>(hs.res_height);
    if (g_cfg.jitter != g_jitter_was) {
        g_jitter_was = g_cfg.jitter;
        host_log("jitter: %s (%s, scene %ux%u, output %ux%u)", g_cfg.jitter ? "on" : "off", upscaler::kind_name(g_cfg.upscaler),
                 g_cfg.render_w, g_cfg.render_h, g_cfg.output_w, g_cfg.output_h);
        g_phases_logged = 0;
    }
    if (!g_cfg.watching) g_jitter = temporal::Jitter{};
    dig(flip);
}

bool temporal_watching() { return g_cfg.watching; }

void temporal_observe_buffer_locked(int stage, std::uint32_t orb_vs, std::uint32_t orb_ps, int index, std::uint64_t base, std::uint64_t bytes) {
    if (!g_cfg.watching || !base) return;
    const auto* p = reinterpret_cast<const float*>(static_cast<std::uintptr_t>(base));
    if (stage == 1) {
        if (index == temporal::velocity_scale_buffer(orb_ps) && bytes >= 8) {
            std::memcpy(g_velocity_scale, p, 8);
            g_velocity_scale_ok = std::isfinite(g_velocity_scale[0]) && std::isfinite(g_velocity_scale[1]);
        }
        return;
    }
    const temporal::BufferKind kind = temporal::classify_buffer(bytes);
    if (kind == temporal::BufferKind::Scene) {
        ++g_window.scene_blocks;
        float raw[16];
        std::memcpy(raw, p + 8, 12 * sizeof(float));
        raw[12] = p[52];
        raw[13] = p[57];
        raw[14] = p[62];
        raw[15] = p[63];
        if (g_last_raw_ok && std::memcmp(raw, g_last_raw, sizeof(raw)) == 0) {
            g_history.observe(g_last_cam);
            return;
        }
        temporal::SceneCamera c;
        const char* why = nullptr;
        const bool ok = temporal::parse_scene_constants(p, temporal::kSceneConstantFloats, &c, &why);
        static int said = 0;
        if (!ok) {
            ++g_window.blocks_rejected;
            if (said < 4) {
                ++said;
                host_log("motion: an 864-byte constant buffer at 0x%llx (%s %08x) is not the scene constants: %s; [0..7] %g %g %g %g %g %g %g %g",
                         static_cast<unsigned long long>(base), "VS", orb_vs, why ? why : "?", p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
            }
            return;
        }
        // The scene's camera carries the scene's size; other cameras (item
        // previews, reflections) another.
        const bool scene_sized = c.width == g_cfg.render_w && c.height == g_cfg.render_h;
        const bool output_sized = c.width == g_cfg.output_w && c.height == g_cfg.output_h;
        if (!scene_sized && !output_sized) return;
        if (!scene_sized && !g_size_mismatch_logged) {
            g_size_mismatch_logged = true;
            host_log("motion: the scene constants carry %ux%u, the output's, not the scene's %ux%u (taken anyway)", c.width, c.height,
                     g_cfg.render_w, g_cfg.render_h);
        }
        static bool first = true;
        if (first) {
            first = false;
            host_log("motion: scene constants found at 0x%llx (VS %08x): far %g, %ux%u, camera at (%.2f, %.2f, %.2f), projection %g %g %g %g",
                     static_cast<unsigned long long>(base), orb_vs, c.far_plane, c.width, c.height, c.position[0], c.position[1], c.position[2],
                     c.proj.x, c.proj.y, c.proj.zs, c.proj.zo);
            if (p[54] != 0.0f || p[58] != 0.0f)
                host_log("motion: the projection has off-centre terms ([54] %g, [58] %g): not used", p[54], p[58]);
        }
        std::memcpy(g_last_raw, raw, sizeof(raw));
        g_last_raw_ok = true;
        g_last_cam = c;
        g_history.observe(c);
        return;
    }
    // bbport's palettes: surveyed every 30th frame (what object motion from
    // previous-frame positions would have to keep).
    if ((kind == temporal::BufferKind::Skeleton || kind == temporal::BufferKind::SmallSkeleton) && g_frames % 30 == 0) {
        ++g_window.skeleton_draws;
        const std::uint64_t key = base ^ (static_cast<std::uint64_t>(orb_vs) << 40);
        const bool moved = g_gate.moved(key, fnv(p, static_cast<std::size_t>(bytes)), g_frames);
        if (kind == temporal::BufferKind::Skeleton) (moved ? g_window.skeleton_moving : g_window.skeleton_static)++;
        else (moved ? g_window.small_moving : g_window.small_static)++;
    }
}

bool temporal_draw_locked(const RtImage* depth, const RtImage* color0, std::uint32_t orb_vs, std::uint32_t orb_ps, std::uint32_t index_count,
                          std::uint32_t instances, bool indirect, bool blended, float* jx, float* jy) {
    if (!g_cfg.watching) return false;
    if (temporal::is_velocity_ps(orb_ps) && color0) {
        ++g_window.velocity_draws;
        g_velocity_base = color0->base;
        g_velocity_this_frame = true;
        return false;
    }
    // IFreemz: the depth bound while drawing into the scene colour is the scene depth.
    if (depth && color0 && g_scene_color_base && color0->base == g_scene_color_base) {
        if (!g_depth_learned || g_scene_depth_base != depth->base) {
            host_log("motion: the scene depth is 0x%llx (%ux%u, format %d)%s", static_cast<unsigned long long>(depth->base), depth->width,
                     depth->height, static_cast<int>(depth->format), g_depth_learned ? ", changed" : "");
        }
        g_scene_depth_base = depth->base;
        g_depth_learned = true;
        if (blended) ++g_window.blended_scene;
    }
    bool scene_depth = false;
    if (depth) {
        if (g_depth_learned) scene_depth = depth->base == g_scene_depth_base;
        else scene_depth = depth->width >= g_cfg.render_w && depth->width < g_cfg.render_w + 128 && depth->height >= g_cfg.render_h &&
                           depth->height < g_cfg.render_h + 128 && (depth->width != g_cfg.output_w || depth->height != g_cfg.output_h);
    }
    if (scene_depth) g_frame_depth_base = depth->base;
    temporal::DrawFacts f;
    f.scene_depth = scene_depth;
    f.ui = temporal::is_ui_vs(orb_vs);
    f.indirect = indirect;
    f.index_count = index_count;
    f.instances = instances;
    if (!g_cfg.jitter || !temporal::jitter_eligible(f)) return false;
    ++g_window.jittered;
    *jx = g_jitter.x;
    *jy = g_jitter.y;
    return true;
}

bool temporal_before_blend_locked(RtImage* color0, const RtImage* depth) {
    if (!g_cfg.watching || !g_cfg.opaque || g_opaque_taken || !color0 || !depth || !g_scene_color_base || color0->base != g_scene_color_base ||
        depth->base != g_scene_depth_base || color0->depth)
        return false;
    g_opaque_taken = true;
    if (!make_owned(g_opaque, color0->format, color0->width, color0->height,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, g_cfg.flip))
        return false;
    begin_recording_locked();
    render_end_pass_locked();
    to_general(g_opaque);
    memory_barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
    VkImageCopy c{};
    c.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.dstSubresource = c.srcSubresource;
    c.extent = {color0->width, color0->height, 1};
    const std::int32_t timed = pass_timer_begin_locked(g_cmd(), TimedPass::Opaque);
    vkCmdCopyImage(g_cmd(), color0->image, VK_IMAGE_LAYOUT_GENERAL, g_opaque.image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    pass_timer_end_locked(g_cmd(), timed);
    memory_barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                   VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    ++g_window.opaque_snapshots;
    return true;
}

void temporal_scale_pass_locked(RtImage* scene_color, std::uint32_t output_w, std::uint32_t output_h) {
    if (!g_cfg.watching) return;
    const std::uint64_t flip = g_cfg.flip;
    ++g_window.frames;
    if (scene_color) g_scene_color_base = scene_color->base;
    if (g_history.cameras_this_frame() > 1) ++g_window.cameras_multi;
    const temporal::Cut cut = g_history.end_frame(flip);
    // Cuts: each one logged, a run of the same one counted.
    if (cut != temporal::Cut::None) {
        ++g_window.cuts;
        if (cut == g_last_cut_logged) {
            ++g_cut_repeats;
        } else {
            if (g_cut_repeats) host_log("camera-cut: ... %llu more \"%s\"", static_cast<unsigned long long>(g_cut_repeats), temporal::cut_name(g_last_cut_logged));
            g_cut_repeats = 0;
            g_last_cut_logged = cut;
            const temporal::SceneCamera& c = g_history.current();
            host_log("camera-cut: %s at flip %llu (camera at %.2f, %.2f, %.2f)", temporal::cut_name(cut), static_cast<unsigned long long>(flip),
                     c.position[0], c.position[1], c.position[2]);
        }
    } else if (g_last_cut_logged != temporal::Cut::None) {
        if (g_cut_repeats) host_log("camera-cut: ... %llu more \"%s\"", static_cast<unsigned long long>(g_cut_repeats), temporal::cut_name(g_last_cut_logged));
        g_cut_repeats = 0;
        g_last_cut_logged = temporal::Cut::None;
    }
    const std::uint32_t rw = g_cfg.render_w, rh = g_cfg.render_h;
    UpscalerInputs in;
    in.frame = flip;
    in.render_w = rw;
    in.render_h = rh;
    in.output_w = output_w;
    in.output_h = output_h;
    in.jitter_x = g_jitter.x;
    in.jitter_y = g_jitter.y;
    in.jitter_phase = g_jitter.phase;
    in.jitter_phases = g_jitter.phases;
    in.cut = cut;
    in.reset = cut != temporal::Cut::None;
    if (g_history.current().valid) {
        in.far_plane = static_cast<float>(g_history.current().far_plane);
        const double zs = g_history.current().proj.zs, zo = g_history.current().proj.zo;
        if (std::fabs(zs) > 1e-9) in.near_plane = static_cast<float>(-zo / zs);
        in.fov_y = static_cast<float>(2.0 * std::atan(1.0 / g_history.current().proj.y));
    }
    if (scene_color) in.color = {scene_color->image, scene_color->format, scene_color->width, scene_color->height, rw, rh};
    if (g_opaque_taken && g_opaque.image) in.opaque_color = {g_opaque.image, g_opaque.format, g_opaque.w, g_opaque.h, rw, rh};
    RtImage* depth = g_frame_depth_base ? find_render_target(g_frame_depth_base) : nullptr;
    if (depth && depth->depth) in.depth = {depth->image, depth->format, depth->width, depth->height, rw, rh};
    else ++g_window.no_depth;
    if (!g_velocity_this_frame) {
        // Drawn after the scale pass, if at all: the last frame's target is what there is.
        if (g_velocity_base) ++g_window.velocity_after_scale;
    }
    // The motion pass (and the depth copy).
    if (in.depth.image && rw && rh && motion_pipeline_locked() &&
        make_owned(g_motion, VK_FORMAT_R16G16B16A16_SFLOAT, rw, rh, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, flip) &&
        make_owned(g_depth_copy, VK_FORMAT_R32_SFLOAT, rw, rh, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, flip)) {
        RtImage* velocity = g_cfg.object_motion && g_velocity_base && g_velocity_scale_ok ? find_render_target(g_velocity_base) : nullptr;
        if (velocity && velocity->depth) velocity = nullptr;
        begin_recording_locked();
        render_end_pass_locked();
        to_general(g_motion);
        to_general(g_depth_copy);
        memory_barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        // Phase 5: the two views are kept while their targets live (pass_view_locked).
        const VkImageView depth_view = pass_view_locked(depth->image, depth->format, VK_IMAGE_ASPECT_DEPTH_BIT);
        const VkImageView velocity_view = velocity ? pass_view_locked(velocity->image, velocity->format, VK_IMAGE_ASPECT_COLOR_BIT) : VK_NULL_HANDLE;
        const VkDescriptorSet set = depth_view ? alloc_set_locked(g_motion_pass.set_layout) : VK_NULL_HANDLE;
        if (set && g_motion.view && g_depth_copy.view) {
            const VkDescriptorImageInfo infos[5] = {
                {VK_NULL_HANDLE, depth_view, VK_IMAGE_LAYOUT_GENERAL},
                {g.dummy_sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED},
                {VK_NULL_HANDLE, velocity_view ? velocity_view : g.dummy_view, VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE, g_motion.view, VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE, g_depth_copy.view, VK_IMAGE_LAYOUT_GENERAL},
            };
            write_set(set, infos, kMotionTypes, 5);
            MotionPush push{};
            const bool camera = g_history.usable();
            if (camera) {
                const temporal::Reprojection r = temporal::make_reprojection(g_history.previous(), g_history.current());
                std::memcpy(push.reproject, r.reproject, sizeof(push.reproject));
                std::memcpy(push.proj, r.proj, sizeof(push.proj));
                std::memcpy(push.prev_proj, r.prev_proj, sizeof(push.prev_proj));
            }
            push.jitter[0] = g_jitter.x;
            push.jitter[1] = g_jitter.y;
            push.velocity_scale[0] = g_velocity_scale[0];
            push.velocity_scale[1] = g_velocity_scale[1];
            push.extent[0] = rw;
            push.extent[1] = rh;
            push.flags = (camera ? 1u : 0u) | (velocity_view ? 2u : 0u);
            VkCommandBuffer cmd = g_cmd();
            const std::int32_t timed = pass_timer_begin_locked(cmd, TimedPass::Motion);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_motion_pass.pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_motion_pass.layout, 0, 1, &set, 0, nullptr);
            vkCmdPushConstants(cmd, g_motion_pass.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
            vkCmdDispatch(cmd, (rw + 7) / 8, (rh + 7) / 8, 1);
            pass_timer_end_locked(cmd, timed);
            memory_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
            in.motion = {g_motion.image, g_motion.format, g_motion.w, g_motion.h, rw, rh};
            in.depth_copy = {g_depth_copy.image, g_depth_copy.format, g_depth_copy.w, g_depth_copy.h, rw, rh};
            g_motion_flip = flip;
            g_motion_has = true;
            ++g_window.motion_passes;
            static bool said = false;
            if (!said) {
                said = true;
                host_log("motion: first motion pass at flip %llu: scene %ux%u, depth %ux%u format %d, camera %s, object velocity %s",
                         static_cast<unsigned long long>(flip), rw, rh, depth->width, depth->height, static_cast<int>(depth->format),
                         camera ? "valid" : "not yet (a cut)", velocity_view ? "from the game's target" : "none");
            }
        }
    }
    // The reactive mask: where the finished scene differs from the opaque snapshot.
    if (g_cfg.reactive && in.opaque_color.image && scene_color && !scene_color->depth && scene_color->view && rw && rh &&
        scene_color->width >= rw && scene_color->height >= rh && g_opaque.w >= rw && g_opaque.h >= rh && g_opaque.view &&
        reactive_pipeline_locked() &&
        make_owned(g_reactive, VK_FORMAT_R8_UNORM, rw, rh, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, flip)) {
        const VkDescriptorSet set = alloc_set_locked(g_reactive_pass.set_layout);
        if (set && g_reactive.view) {
            begin_recording_locked();
            render_end_pass_locked();
            to_general(g_reactive);
            memory_barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            const VkDescriptorImageInfo infos[4] = {
                {VK_NULL_HANDLE, g_opaque.view, VK_IMAGE_LAYOUT_GENERAL},
                {g.dummy_sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED},
                {VK_NULL_HANDLE, scene_color->view, VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE, g_reactive.view, VK_IMAGE_LAYOUT_GENERAL},
            };
            write_set(set, infos, kReactiveTypes, 4);
            // AMD's sample values (scale 1, threshold 0.2, binary 0.9); tunable for the test sessions.
            static const float scale = env_float("BBHOST_REACTIVE_SCALE", 1.0f), threshold = env_float("BBHOST_REACTIVE_THRESHOLD", 0.2f),
                               value = env_float("BBHOST_REACTIVE_VALUE", 0.9f);
            ReactivePush push{};
            push.extent[0] = rw;
            push.extent[1] = rh;
            push.scale = scale;
            push.threshold = threshold;
            push.binary_value = value;
            VkCommandBuffer cmd = g_cmd();
            const std::int32_t timed = pass_timer_begin_locked(cmd, TimedPass::Reactive);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_reactive_pass.pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g_reactive_pass.layout, 0, 1, &set, 0, nullptr);
            vkCmdPushConstants(cmd, g_reactive_pass.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
            vkCmdDispatch(cmd, (rw + 7) / 8, (rh + 7) / 8, 1);
            pass_timer_end_locked(cmd, timed);
            memory_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                           VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
            in.reactive = {g_reactive.image, g_reactive.format, g_reactive.w, g_reactive.h, rw, rh};
            ++g_window.reactive_passes;
            static bool said = false;
            if (!said) {
                said = true;
                host_log("motion: first reactive mask at flip %llu (%ux%u; scale %g, threshold %g, value %g)", static_cast<unsigned long long>(flip), rw, rh,
                         static_cast<double>(scale), static_cast<double>(threshold), static_cast<double>(value));
            }
        }
    }
    // The frame time (FSR's history weights), from the wall clock between scale passes.
    {
        static std::chrono::steady_clock::time_point last{};
        const auto now = std::chrono::steady_clock::now();
        const double ms = last.time_since_epoch().count() ? std::chrono::duration<double, std::milli>(now - last).count() : 0.0;
        last = now;
        in.frame_ms = ms > 0.5 && ms < 250.0 ? static_cast<float>(ms) : 16.7f;
    }
    in.valid = in.color.image && in.depth.image;
    g_inputs = in;
    // The next frame's jitter.
    ++g_frames;
    const std::uint32_t phases = temporal::jitter_phase_count(rw, output_w ? output_w : rw);
    g_jitter = g_cfg.jitter ? temporal::jitter_offset(g_frames, phases) : temporal::Jitter{};
    if (g_cfg.jitter && phases != g_phases_logged) {
        g_phases_logged = phases;
        host_log("jitter: Halton(2,3), %u phases (scene %ux%u, output %ux%u)", phases, rw, rh, output_w, output_h);
    }
    // Per-frame state.
    g_frame_depth_base = 0;
    g_velocity_this_frame = false;
    g_opaque_taken = false;
    if (g_frames % 600 == 0) g_gate.sweep(g_frames);
    // A summary every 3000 frames.
    if (g_window.frames >= 3000) {
        host_log("%s", temporal_report_locked().c_str());
        g_stats.frames += g_window.frames;
        g_stats.motion_passes += g_window.motion_passes;
        g_stats.jittered += g_window.jittered;
        g_stats.cuts += g_window.cuts;
        g_window = Stats{};
    }
}

const UpscalerInputs& temporal_inputs_locked() { return g_inputs; }

std::string temporal_report_locked() {
    const Stats& w = g_window;
    const double f = w.frames ? static_cast<double>(w.frames) : 1.0;
    char buf[768];
    std::snprintf(buf, sizeof(buf),
                  "motion: %llu frames: %llu motion passes (%llu without a scene depth), %.0f jittered draws a frame (phase %u of %u, %+.3f %+.3f), "
                  "%llu cuts, %.1f scene blocks a frame (%llu rejected, %llu frames with 2+ cameras), velocity draws %.1f a frame (%llu frames "
                  "after the scale pass, scale %g %g), opaque snapshots %llu, reactive masks %llu, blended scene draws %.1f a frame; palettes (every 30th frame): "
                  "skeletons %llu moving / %llu still, small %llu moving / %llu still",
                  static_cast<unsigned long long>(w.frames), static_cast<unsigned long long>(w.motion_passes),
                  static_cast<unsigned long long>(w.no_depth), w.jittered / f, g_jitter.phase, g_jitter.phases, g_jitter.x, g_jitter.y,
                  static_cast<unsigned long long>(w.cuts), w.scene_blocks / f, static_cast<unsigned long long>(w.blocks_rejected),
                  static_cast<unsigned long long>(w.cameras_multi), w.velocity_draws / f, static_cast<unsigned long long>(w.velocity_after_scale),
                  g_velocity_scale[0], g_velocity_scale[1], static_cast<unsigned long long>(w.opaque_snapshots), static_cast<unsigned long long>(w.reactive_passes), w.blended_scene / f,
                  static_cast<unsigned long long>(w.skeleton_moving), static_cast<unsigned long long>(w.skeleton_static),
                  static_cast<unsigned long long>(w.small_moving), static_cast<unsigned long long>(w.small_static));
    return buf;
}

}  // namespace gpu

bool host_gpu_motion_view(VkImage* image, VkFormat* format, std::uint32_t* w, std::uint32_t* h) {
    using namespace gpu;
    if (!g_motion_has || !g_motion.image || hle_video_flip_count() > g_motion_flip + 2) return false;
    *image = g_motion.image;
    *format = g_motion.format;
    *w = g_motion.w;
    *h = g_motion.h;
    return true;
}
