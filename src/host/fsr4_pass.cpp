// bbhost-vk Phase 4: FSR 4.1.1 at the scale pass (host/fsr4_pass.h).
#include "host/fsr4_pass.h"

#include "host/fsr411/fsr411.h"
#include "host/gpu_internal.h"
#include "host/upscaler_inputs.h"
#include "log.h"

#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>

#if defined(VK_VALVE_shader_mixed_float_dot_product) && defined(VK_EXT_shader_float8)

namespace gpu {
namespace {

struct Support {
    bool asked = false;        // the device was offered the extensions (assets there)
    bool int8 = false;         // the INT8 variant's needs: Fsr411::Upscaler can run
    bool fp8 = false;          // FP8 cooperative matrices (RDNA4)
    bool fp16_matrices = false;
    std::string missing;       // what the GPU lacks, for the log
};
Support g_support;

VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR g_derivatives{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE g_mixed{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MIXED_FLOAT_DOT_PRODUCT_FEATURES_VALVE};
VkPhysicalDeviceCooperativeMatrixFeaturesKHR g_coop{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};
VkPhysicalDeviceShaderFloat8FeaturesEXT g_float8{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT};

bool env_off(const char* name) {
    const char* e = std::getenv(name);
    return e && e[0] == '0';
}

void link(void** chain, void* s, void** next) {
    *next = *chain;
    *chain = s;
}

// The upscaler and what it holds.
std::unique_ptr<Fsr411::Upscaler> g_fsr4;
bool g_fatal = false;
std::string g_fatal_why;
std::deque<std::uint64_t> g_serials;  // the submissions the last frames were recorded into (its constant ring)
struct OutImage {
    VkImage image = VK_NULL_HANDLE;
    ImageMemory memory;
    VkImageView view = VK_NULL_HANDLE;
    std::uint32_t w = 0, h = 0;
    bool general = false;
} g_out;

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

// A view of an input for this frame, destroyed once the recording retires.
VkImageView frame_view(const UpscalerImage& img) {
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = img.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = img.format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView v = VK_NULL_HANDLE;
    if (vkCreateImageView(g.device, &vci, nullptr, &v) != VK_SUCCESS) return VK_NULL_HANDLE;
    return v;
}

}  // namespace

std::string fsr4_assets_dir() {
    if (const char* e = std::getenv("BBHOST_FSR411_DIR"); e && e[0]) return e;
    std::string base;
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && x[0]) base = x;
    else if (const char* h = std::getenv("HOME"); h && h[0]) base = std::string(h) + "/.local/share";
    return base.empty() ? std::string("fsr4_411") : base + "/bbhost/fsr4_411";
}

bool fsr4_assets_present(const std::string& dir) {
    std::error_code ec;
    return std::filesystem::is_regular_file(dir + "/t1080_m0/initializer.bin", ec) ||
           std::filesystem::is_regular_file(dir + "/fp8/t1080_m0/initializer.bin", ec);
}

void fsr4_device_features(VkPhysicalDeviceFeatures& f, VkPhysicalDeviceVulkan12Features& f12, VkPhysicalDeviceVulkan13Features& f13,
                          std::vector<const char*>& exts, void** chain) {
    if (env_off("BBHOST_FSR4")) return;
    const std::string dir = fsr4_assets_dir();
    const char* force = std::getenv("BBHOST_FSR4_FEATURES");
    if (!fsr4_assets_present(dir) && !(force && force[0] == '1')) {
        host_log("fsr4: no FSR 4.1.1 assets in %s: FSR 4.1.1 is FSR 3.1 (build them from your DLL: tools/fsr4cap)", dir.c_str());
        return;
    }
    g_support.asked = true;
    std::uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(g.phys, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> props(n);
    vkEnumerateDeviceExtensionProperties(g.phys, nullptr, &n, props.data());
    const auto has = [&](const char* name) {
        for (const VkExtensionProperties& p : props) {
            if (!std::strcmp(p.extensionName, name)) return true;
        }
        return false;
    };
    VkPhysicalDeviceVulkan12Features q12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features q13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR qd{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
    VkPhysicalDeviceShaderMixedFloatDotProductFeaturesVALVE qm{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_MIXED_FLOAT_DOT_PRODUCT_FEATURES_VALVE};
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR qc{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};
    VkPhysicalDeviceShaderFloat8FeaturesEXT q8{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT};
    const bool e_deriv = has(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME), e_mixed = has(VK_VALVE_SHADER_MIXED_FLOAT_DOT_PRODUCT_EXTENSION_NAME),
               e_coop = has(VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME), e_f8 = has(VK_EXT_SHADER_FLOAT8_EXTENSION_NAME);
    q12.pNext = &q13;
    void** tail = &q13.pNext;
    if (e_deriv) { *tail = &qd; tail = &qd.pNext; }
    if (e_mixed) { *tail = &qm; tail = &qm.pNext; }
    if (e_coop) { *tail = &qc; tail = &qc.pNext; }
    if (e_f8) { *tail = &q8; tail = &q8.pNext; }
    VkPhysicalDeviceFeatures2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    q2.pNext = &q12;
    vkGetPhysicalDeviceFeatures2(g.phys, &q2);
    // The INT8 variant (bbport's IsFsr411Supported): INT8 dot products, compute
    // derivatives, mixed float dot products, extended storage formats, push descriptors.
    std::string missing;
    const auto need = [&](bool ok, const char* what) {
        if (!ok) missing += missing.empty() ? what : std::string(", ") + what;
        return ok;
    };
    bool int8 = need(q13.shaderIntegerDotProduct, "shaderIntegerDotProduct");
    int8 = need(e_deriv && qd.computeDerivativeGroupLinear, "VK_KHR_compute_shader_derivatives") && int8;
    int8 = need(e_mixed && qm.shaderMixedFloatDotProductFloat16AccFloat32, "VK_VALVE_shader_mixed_float_dot_product") && int8;
    int8 = need(q2.features.shaderStorageImageExtendedFormats, "shaderStorageImageExtendedFormats") && int8;
    int8 = need(g.has_push_descriptor, "VK_KHR_push_descriptor") && int8;
    g_support.int8 = int8;
    if (!int8) {
        g_support.missing = missing;
        host_log("fsr4: the GPU lacks %s: FSR 4.1.1 is FSR 3.1", missing.c_str());
        return;
    }
    // Enable what the passes use, as bbport's device does.
    f13.shaderIntegerDotProduct = VK_TRUE;
    f.shaderStorageImageExtendedFormats = VK_TRUE;
    if (q2.features.shaderInt16) f.shaderInt16 = VK_TRUE;
    if (q12.shaderFloat16) f12.shaderFloat16 = VK_TRUE;
    if (q12.shaderInt8) f12.shaderInt8 = VK_TRUE;
    if (q12.storageBuffer8BitAccess) f12.storageBuffer8BitAccess = VK_TRUE;
    if (q12.uniformAndStorageBuffer8BitAccess) f12.uniformAndStorageBuffer8BitAccess = VK_TRUE;
    exts.push_back(VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
    g_derivatives.computeDerivativeGroupQuads = qd.computeDerivativeGroupQuads;
    g_derivatives.computeDerivativeGroupLinear = VK_TRUE;
    link(chain, &g_derivatives, &g_derivatives.pNext);
    exts.push_back(VK_VALVE_SHADER_MIXED_FLOAT_DOT_PRODUCT_EXTENSION_NAME);
    g_mixed.shaderMixedFloatDotProductFloat16AccFloat32 = VK_TRUE;
    link(chain, &g_mixed, &g_mixed.pNext);
    // The FP8 variant (RDNA4), and FP16 matrices (its emulation, testing).
    const bool matrices = e_coop && qc.cooperativeMatrix && q12.vulkanMemoryModel && q13.subgroupSizeControl && q13.computeFullSubgroups &&
                          q12.storageBuffer8BitAccess;
    if (matrices) {
        exts.push_back(VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME);
        g_coop.cooperativeMatrix = VK_TRUE;
        link(chain, &g_coop, &g_coop.pNext);
        f12.vulkanMemoryModel = VK_TRUE;
        if (q12.vulkanMemoryModelDeviceScope) f12.vulkanMemoryModelDeviceScope = VK_TRUE;
        f13.subgroupSizeControl = VK_TRUE;
        f13.computeFullSubgroups = VK_TRUE;
        g_support.fp16_matrices = true;
        if (e_f8 && q8.shaderFloat8 && q8.shaderFloat8CooperativeMatrix) {
            exts.push_back(VK_EXT_SHADER_FLOAT8_EXTENSION_NAME);
            g_float8.shaderFloat8 = VK_TRUE;
            g_float8.shaderFloat8CooperativeMatrix = VK_TRUE;
            link(chain, &g_float8, &g_float8.pNext);
            g_support.fp8 = true;
        }
    }
    host_log("fsr4: FSR 4.1.1 assets in %s; the device runs the INT8 variant%s", dir.c_str(),
             g_support.fp8 ? " and the FP8 one (FP8 cooperative matrices)" : matrices ? " (FP16 matrices, no FP8)" : "");
}

bool fsr4_record_locked(VkCommandBuffer cmd, const UpscalerInputs& in, std::uint32_t output_w, std::uint32_t output_h, const Fsr4Params& p,
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
    if (env_off("BBHOST_FSR4")) return fail_for_good("BBHOST_FSR4=0");
    if (!g_support.asked) return fail_for_good("no FSR 4.1.1 assets in " + fsr4_assets_dir() + " (build them from your DLL: tools/fsr4cap)");
    if (!g_support.int8) return fail_for_good("the GPU lacks " + g_support.missing);
    if (!g_fsr4) {
        Fsr411::Features feat;
        feat.fp8_matrices = g_support.fp8;
        feat.fp16_matrices = g_support.fp16_matrices;
        g_fsr4 = std::make_unique<Fsr411::Upscaler>(g.phys, g.device, fsr4_assets_dir(), feat);
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
    // Its constant ring holds Fsr411::kFramesInFlight frames: the oldest must be done.
    while (g_serials.size() >= Fsr411::kFramesInFlight) {
        wait_submission_locked(g_serials.front());
        g_serials.pop_front();
    }
    const VkImageView cv = frame_view(in.color), dv = frame_view(in.depth_copy), mv = frame_view(in.motion);
    bool ok = cv && dv && mv;
    if (ok) {
        Fsr411::Frame f;
        f.cmdbuf = cmd;
        f.color = {in.color.image, cv, in.color.width, in.color.height};
        f.depth = {in.depth_copy.image, dv, in.depth_copy.width, in.depth_copy.height};
        f.motion = {in.motion.image, mv, in.motion.width, in.motion.height};
        f.output = {g_out.image, g_out.view, g_out.w, g_out.h};
        f.render_width = in.render_w;
        f.render_height = in.render_h;
        f.ultra_performance = p.ultra_performance;
        f.jitter[0] = p.jitter_x;
        f.jitter[1] = p.jitter_y;
        f.sharpen = p.sharpen && p.sharpness > 0.0f;
        f.sharpness = p.sharpness;
        f.reset = p.reset;
        f.auto_exposure = true;
        ok = g_fsr4->Record(f);
        if (!ok) {
            const std::string& e = g_fsr4->Error();
            // Missing or broken assets, or a device the passes cannot run on, are for good.
            if (e.rfind("missing", 0) == 0 || e.find("pipeline") != std::string::npos || e.find("shader") != std::string::npos)
                fail_for_good("FSR 4.1.1: " + e);
            *why = "FSR 4.1.1: " + e;
        } else {
            g_serials.push_back(g.flushes);
        }
    } else {
        *why = "could not make the inputs' views";
    }
    for (VkImageView v : {cv, dv, mv}) {
        if (v) defer_destroy_private_view(v);
    }
    if (ok) *out = g_out.image;
    return ok;
}

std::string fsr4_describe_locked() { return g_fsr4 ? g_fsr4->Describe() : std::string(); }

}  // namespace gpu

#else  // Vulkan headers without VK_VALVE_shader_mixed_float_dot_product / VK_EXT_shader_float8

namespace gpu {
std::string fsr4_assets_dir() { return std::string(); }
bool fsr4_assets_present(const std::string&) { return false; }
void fsr4_device_features(VkPhysicalDeviceFeatures&, VkPhysicalDeviceVulkan12Features&, VkPhysicalDeviceVulkan13Features&,
                          std::vector<const char*>&, void**) {
    host_log("fsr4: built with Vulkan headers older than 1.4.3xx: FSR 4.1.1 is FSR 3.1");
}
bool fsr4_record_locked(VkCommandBuffer, const UpscalerInputs&, std::uint32_t, std::uint32_t, const Fsr4Params&, VkImage*, bool* fatal,
                        std::string* why) {
    *fatal = true;
    *why = "this build's Vulkan headers lack FSR 4.1.1's extensions";
    return false;
}
std::string fsr4_describe_locked() { return std::string(); }
}  // namespace gpu

#endif
