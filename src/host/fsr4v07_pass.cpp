// Helia's Dream: FSR 4 (INT8, v07) at the scale pass (host/fsr4v07_pass.h).
#include "host/fsr4v07_pass.h"

#include "host/gpu_internal.h"
#include "log.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#if defined(BBHOST_HAVE_FSR4_V07)

extern "C" {
#include "ffx_vk_fsr4_v07.h"
#include "ffx_vk_fsr4_v07_assets.h"
#include "ffx_vk_fsr4_v07_schedule.h"
#include "ffx_vk_fsr4_v07_types.h"
}

namespace gpu {
namespace {

struct Support {
    bool asked = false;  // the assets are there: the device was offered the features
    bool ok = false;     // and it has them
    std::string missing;
};
Support g_support;
VkPhysicalDeviceVulkan11Features g_f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};

// One provider context: for one model, tier and pair of sizes.
struct Context {
    FfxInterface iface{};
    ffxContext ctx = nullptr;
    std::vector<std::uint8_t> scratch;
    FfxFsr4ModelPreset preset = FFX_FSR4_MODEL_PRESET_NATIVE_AA;
    std::string tier;
    std::uint32_t rw = 0, rh = 0, ow = 0, oh = 0;
    std::uint64_t last_frame = 0;  // the submission it last recorded into
};
Context* g_ctx = nullptr;
bool g_fatal = false;
std::string g_fatal_why;

struct OutImage {
    VkImage image = VK_NULL_HANDLE;
    ImageMemory memory;
    VkImageView view = VK_NULL_HANDLE;
    std::uint32_t w = 0, h = 0;
    bool general = false;
} g_out;

bool env_off(const char* name) {
    const char* e = std::getenv(name);
    return e && e[0] == '0';
}

bool read_file(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !out.empty();
}

void destroy(Context* c) {
    if (!c) return;
    // Everything it recorded must be done first.
    if (c->last_frame) wait_submission_locked(c->last_frame);
    if (c->ctx) ffxFsr4V07DestroyContext(&c->ctx, nullptr);
    if (c->iface.device) ffxFsr4VkDestroyContext(static_cast<FfxFsr4VkContext*>(c->iface.device));
    delete c;
}

// A context for these sizes, its shaders and weights read from the asset folder.
Context* make(std::uint32_t rw, std::uint32_t rh, std::uint32_t ow, std::uint32_t oh, std::string* why) {
    const FfxFsr4ModelPreset preset = ffxFsr4SelectModelPreset(rw, ow, false);
    FfxFsr4V07AssetSet set{};
    if (!ffxFsr4V07BuildAssetSet(preset, ow, oh, &set)) {
        *why = "no FSR 4 model for an output of " + std::to_string(ow) + "x" + std::to_string(oh);
        return nullptr;
    }
    const std::string dir = fsr4v07_assets_dir() + "/";
    std::vector<std::uint8_t> blobs[FFX_FSR4_VK_PASS_COUNT], init, pre_weights;
    const char* names[FFX_FSR4_VK_PASS_COUNT];
    names[0] = set.pre;
    for (int i = 0; i < 12; ++i) names[1 + i] = set.model[i];
    names[13] = set.post;
    names[14] = set.rcas;
    names[15] = set.spdAutoExposure;
    for (int i = 0; i < FFX_FSR4_VK_PASS_COUNT; ++i) {
        if (!read_file(dir + names[i], blobs[i]) || blobs[i].size() % 4) {
            *why = std::string("missing ") + names[i] + " in " + dir;
            return nullptr;
        }
    }
    if (!read_file(dir + set.initializer, init) || !read_file(dir + set.prePassWeights, pre_weights)) {
        *why = std::string("missing ") + set.initializer + " or its pre-pass weights in " + dir;
        return nullptr;
    }
    auto* c = new Context;
    c->scratch.resize(ffxFsr4VkGetScratchMemorySize());
    FfxFsr4VkCreateInfo ci{};
    ci.device = g.device;
    ci.physicalDevice = g.phys;
    for (int i = 0; i < FFX_FSR4_VK_PASS_COUNT; ++i) {
        ci.shaders[i].spirv = reinterpret_cast<const std::uint32_t*>(blobs[i].data());
        ci.shaders[i].sizeBytes = blobs[i].size();
        ci.shaders[i].entryPoint = "main";
    }
    ci.modelInitializer = init.data();
    ci.modelInitializerSize = init.size();
    ci.prePassWeights = pre_weights.data();
    ci.prePassWeightsSize = pre_weights.size();
    ci.scratchBuffer = c->scratch.data();
    ci.scratchBufferSize = c->scratch.size();
    if (const VkResult r = ffxFsr4VkCreateContext(&ci, &c->iface); r != VK_SUCCESS) {
        *why = "the provider would not start (VkResult " + std::to_string(static_cast<int>(r)) + ": a shader the GPU or driver refused)";
        delete c;
        return nullptr;
    }
    ffxFsr4V07SetBackendInterface(&c->iface);
    ffxCreateContextDescUpscale desc{};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    // The game's scene colour is linear HDR (RGBA16F); exposure is FSR's own.
    desc.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
    desc.maxRenderSize = {rw, rh};
    desc.maxUpscaleSize = {ow, oh};
    const ffxReturnCode_t rc = ffxFsr4V07CreateContext(&c->ctx, &desc.header, nullptr);
    ffxFsr4V07SetBackendInterface(nullptr);
    if (rc != FFX_API_RETURN_OK) {
        *why = "the FSR 4 context could not be made (code " + std::to_string(static_cast<int>(rc)) + ")";
        c->ctx = nullptr;
        destroy(c);
        return nullptr;
    }
    c->preset = preset;
    c->tier = set.tier;
    c->rw = rw;
    c->rh = rh;
    c->ow = ow;
    c->oh = oh;
    return c;
}

bool make_output(std::uint32_t w, std::uint32_t h) {
    if (g_out.image && g_out.w == w && g_out.h == h) return true;
    if (g_out.image) {
        if (g_out.view) defer_destroy_private_view(g_out.view);
        defer_destroy_image(g_out.image, g_out.memory);
        g_out = OutImage{};
    }
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    ci.extent = {w, h, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateImage(g.device, &ci, nullptr, &g_out.image) != VK_SUCCESS) {
        g_out = OutImage{};
        return false;
    }
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(g.device, g_out.image, &req);
    if (!image_memory_alloc(req, g_out.memory)) {
        vkDestroyImage(g.device, g_out.image, nullptr);
        g_out = OutImage{};
        return false;
    }
    vkBindImageMemory(g.device, g_out.image, g_out.memory.memory, g_out.memory.offset);
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = g_out.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = ci.format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(g.device, &vci, nullptr, &g_out.view) != VK_SUCCESS) g_out.view = VK_NULL_HANDLE;
    g_out.w = w;
    g_out.h = h;
    return g_out.view != VK_NULL_HANDLE;
}

std::uint32_t ffx_format(VkFormat f) {
    switch (f) {
        case VK_FORMAT_R16G16B16A16_SFLOAT: return FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
        case VK_FORMAT_R32_SFLOAT: return FFX_SURFACE_FORMAT_R32_FLOAT;
        case VK_FORMAT_R16G16_SFLOAT: return FFX_SURFACE_FORMAT_R16G16_FLOAT;
        case VK_FORMAT_R8G8B8A8_UNORM: return FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
        default: return 0;
    }
}

// An image of ours for the provider: its view, its size, and its state (General
// before and after, read and written by compute) registered for this frame.
bool resource(FfxInterface* iface, VkImage image, VkImageView view, VkFormat format, std::uint32_t w, std::uint32_t h, bool write,
              FfxApiResource* out) {
    const std::uint32_t ff = ffx_format(format);
    if (!image || !view || !ff) return false;
    FfxFsr4VkExternalImageState st{};
    st.structSize = sizeof(st);
    st.image = image;
    st.view = view;
    st.layout = VK_IMAGE_LAYOUT_GENERAL;
    st.stageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    st.accessMask = VK_ACCESS_SHADER_READ_BIT | (write ? VK_ACCESS_SHADER_WRITE_BIT : 0);
    st.restoreLayout = VK_IMAGE_LAYOUT_GENERAL;
    st.restoreStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    st.restoreAccessMask = st.accessMask;
    if (ffxFsr4VkSetExternalImageState(iface, &st) != VK_SUCCESS) return false;
    std::memset(out, 0, sizeof(*out));
    out->resource = view;
    out->description.type = FFX_RESOURCE_TYPE_TEXTURE2D;
    out->description.format = ff;
    out->description.width = w;
    out->description.height = h;
    out->description.depth = 1;
    out->description.mipCount = 1;
    out->state = write ? FFX_API_RESOURCE_STATE_UNORDERED_ACCESS : FFX_API_RESOURCE_STATE_COMPUTE_READ;
    return true;
}

}  // namespace

std::string fsr4v07_assets_dir() {
    if (const char* e = std::getenv("BBHOST_FSR4V07_DIR"); e && e[0]) return e;
    std::string base;
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && x[0]) base = x;
    else if (const char* h = std::getenv("HOME"); h && h[0]) base = std::string(h) + "/.local/share";
    return base.empty() ? std::string("fsr4_v07") : base + "/bbhost/fsr4_v07";
}

bool fsr4v07_assets_present(const std::string& dir) {
    std::error_code ec;
    return std::filesystem::is_regular_file(dir + "/rcas.spv", ec) &&
           std::filesystem::is_regular_file(dir + "/fsr4_model_v07_i8_balanced_initializers.bin", ec);
}

void fsr4v07_device_features(VkPhysicalDeviceFeatures& f, VkPhysicalDeviceVulkan12Features& f12, VkPhysicalDeviceVulkan13Features& f13,
                             void** chain) {
    if (env_off("BBHOST_FSR4V07")) return;
    const std::string dir = fsr4v07_assets_dir();
    if (!fsr4v07_assets_present(dir)) {
        host_log("fsr4: no FSR 4 assets in %s: FSR 4 is FSR 3.1 (the launcher's Set up downloads them)", dir.c_str());
        return;
    }
    g_support.asked = true;
    VkPhysicalDeviceVulkan11Features q11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceVulkan12Features q12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features q13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    q11.pNext = &q12;
    q12.pNext = &q13;
    VkPhysicalDeviceFeatures2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    q2.pNext = &q11;
    vkGetPhysicalDeviceFeatures2(g.phys, &q2);
    std::string missing;
    const auto need = [&](bool ok, const char* what) {
        if (!ok) missing += missing.empty() ? what : std::string(", ") + what;
        return ok;
    };
    bool ok = need(q13.shaderIntegerDotProduct, "shaderIntegerDotProduct");
    ok = need(q12.shaderInt8, "shaderInt8") && ok;
    ok = need(q12.shaderFloat16, "shaderFloat16") && ok;
    ok = need(q12.storageBuffer8BitAccess, "storageBuffer8BitAccess") && ok;
    ok = need(q11.storageBuffer16BitAccess, "storageBuffer16BitAccess") && ok;
    g_support.ok = ok;
    if (!ok) {
        g_support.missing = missing;
        host_log("fsr4: the GPU lacks %s: FSR 4 is FSR 3.1", missing.c_str());
        return;
    }
    f13.shaderIntegerDotProduct = VK_TRUE;
    f12.shaderInt8 = VK_TRUE;
    f12.shaderFloat16 = VK_TRUE;
    f12.storageBuffer8BitAccess = VK_TRUE;
    if (q12.uniformAndStorageBuffer8BitAccess) f12.uniformAndStorageBuffer8BitAccess = VK_TRUE;
    if (q2.features.shaderInt16) f.shaderInt16 = VK_TRUE;
    f.shaderStorageImageExtendedFormats = q2.features.shaderStorageImageExtendedFormats;
    g_f11.storageBuffer16BitAccess = VK_TRUE;
    g_f11.uniformAndStorageBuffer16BitAccess = q11.uniformAndStorageBuffer16BitAccess;
    g_f11.pNext = *chain;
    *chain = &g_f11;
    host_log("fsr4: FSR 4 (INT8) assets in %s; the device runs them", dir.c_str());
}

bool fsr4v07_record_locked(VkCommandBuffer cmd, const UpscalerInputs& in, std::uint32_t output_w, std::uint32_t output_h, const Fsr4Params& p,
                           VkImage* out, bool* fatal, std::string* why) {
    *fatal = false;
    const auto fail_for_good = [&](const std::string& w) {
        g_fatal = true;
        g_fatal_why = w;
        *fatal = true;
        *why = w;
        return false;
    };
    if (g_fatal) {
        *fatal = true;
        *why = g_fatal_why;
        return false;
    }
    if (env_off("BBHOST_FSR4V07")) return fail_for_good("BBHOST_FSR4V07=0");
    if (!g_support.asked) return fail_for_good("no FSR 4 assets in " + fsr4v07_assets_dir() + " (the launcher's Set up downloads them)");
    if (!g_support.ok) return fail_for_good("the GPU lacks " + g_support.missing);
    if (!g_ctx || g_ctx->rw != in.render_w || g_ctx->rh != in.render_h || g_ctx->ow != output_w || g_ctx->oh != output_h) {
        destroy(g_ctx);
        g_ctx = nullptr;
        std::string w;
        g_ctx = make(in.render_w, in.render_h, output_w, output_h, &w);
        if (!g_ctx) return w.rfind("missing", 0) == 0 || w.find("provider") != std::string::npos ? fail_for_good("FSR 4: " + w) : (*why = "FSR 4: " + w, false);
        host_log("fsr4: FSR 4 (INT8 v07) context: %s model, tier %s, scene %ux%u -> %ux%u", ffxFsr4ModelPresetName(g_ctx->preset), g_ctx->tier.c_str(),
                 in.render_w, in.render_h, output_w, output_h);
    }
    if (!make_output(output_w, output_h)) {
        *why = "could not make its output image";
        return false;
    }
    if (!g_out.general) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = g_out.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        g_out.general = true;
    }
    FfxInterface* iface = &g_ctx->iface;
    // The provider keeps three frames' constants: what the GPU finished is given back first.
    ffxFsr4VkRetireFrame(iface, g.completed_submits);
    const std::uint64_t frame_id = g.flushes + 1;  // this recording: complete once completed_submits reaches it
    VkResult br = ffxFsr4VkBeginFrame(iface, frame_id);
    if (br == VK_NOT_READY && g_ctx->last_frame) {
        wait_submission_locked(g_ctx->last_frame);
        ffxFsr4VkRetireFrame(iface, g.completed_submits);
        br = ffxFsr4VkBeginFrame(iface, frame_id);
    }
    if (br != VK_SUCCESS) {
        *why = "FSR 4: no free frame slot";
        return false;
    }
    const bool direct = p.direct_image && p.direct_view;
    const VkImageView cv = pass_view_locked(in.color.image, in.color.format, VK_IMAGE_ASPECT_COLOR_BIT);
    const VkImageView dv = pass_view_locked(in.depth_copy.image, in.depth_copy.format, VK_IMAGE_ASPECT_COLOR_BIT);
    const VkImageView mv = pass_view_locked(in.motion.image, in.motion.format, VK_IMAGE_ASPECT_COLOR_BIT);
    ffxDispatchDescUpscale d{};
    d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    d.commandList = cmd;
    const bool ok_res = resource(iface, in.color.image, cv, in.color.format, in.color.width, in.color.height, false, &d.color) &&
                        resource(iface, in.depth_copy.image, dv, in.depth_copy.format, in.depth_copy.width, in.depth_copy.height, false, &d.depth) &&
                        resource(iface, in.motion.image, mv, in.motion.format, in.motion.width, in.motion.height, false, &d.motionVectors) &&
                        resource(iface, direct ? p.direct_image : g_out.image, direct ? p.direct_view : g_out.view, VK_FORMAT_R16G16B16A16_SFLOAT,
                                 output_w, output_h, true, &d.output);
    if (!ok_res) {
        *why = "FSR 4: an input it cannot take (format or view)";
        return false;
    }
    d.jitterOffset = {p.jitter_x, p.jitter_y};
    d.motionVectorScale = {1.0f, 1.0f};  // render pixels, current to previous: as FSR 3.1 takes them
    d.renderSize = {in.render_w, in.render_h};
    d.upscaleSize = {output_w, output_h};
    d.enableSharpening = p.sharpen && p.sharpness > 0.0f;
    d.sharpness = p.sharpness;
    d.enableAutoExposure = true;
    d.frameTimeDelta = in.frame_ms > 0.0f ? in.frame_ms : 16.7f;
    d.preExposure = 1.0f;
    d.reset = p.reset;
    d.cameraNear = in.near_plane;
    d.cameraFar = in.far_plane;
    d.cameraFovAngleVertical = in.fov_y > 0.0f ? in.fov_y : 1.0f;
    d.viewSpaceToMetersFactor = 1.0f;
    const ffxReturnCode_t rc = ffxFsr4V07Dispatch(&g_ctx->ctx, &d.header);
    if (rc != FFX_API_RETURN_OK) {
        *why = "FSR 4: the dispatch failed (code " + std::to_string(static_cast<int>(rc)) + ")";
        return false;
    }
    g_ctx->last_frame = frame_id;
    *out = direct ? p.direct_image : g_out.image;
    return true;
}

std::string fsr4v07_describe_locked() {
    if (!g_ctx) return std::string();
    return std::string(ffxFsr4ModelPresetName(g_ctx->preset)) + " t" + g_ctx->tier + " output " + std::to_string(g_ctx->ow) + "x" +
           std::to_string(g_ctx->oh);
}

}  // namespace gpu

#else  // FSR-Vulkan built without its FSR 4 provider

namespace gpu {
std::string fsr4v07_assets_dir() { return std::string(); }
bool fsr4v07_assets_present(const std::string&) { return false; }
void fsr4v07_device_features(VkPhysicalDeviceFeatures&, VkPhysicalDeviceVulkan12Features&, VkPhysicalDeviceVulkan13Features&, void**) {}
bool fsr4v07_record_locked(VkCommandBuffer, const UpscalerInputs&, std::uint32_t, std::uint32_t, const Fsr4Params&, VkImage*, bool* fatal,
                           std::string* why) {
    *fatal = true;
    *why = "this build has no FSR 4 provider";
    return false;
}
std::string fsr4v07_describe_locked() { return std::string(); }
}  // namespace gpu

#endif
