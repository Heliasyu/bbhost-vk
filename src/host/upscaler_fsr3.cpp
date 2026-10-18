// FireBurn/FSR-Vulkan (MIT; third_party/FSR-Vulkan, cmake/fsr_vulkan.cmake)
// in the binary: the FSR 3.1.5 upscaler (its opaque bridge, SDK 2.3 host
// sources and embedded SPIR-V) and the FSR 3.1.4 portable upscaler. Phase 0
// only checks they are there; Phase 3 records their dispatches.
#include "host/upscaler.h"

#include "ffx_vk_fsr3_3_1_5_bridge.h"
#include "ffx_vk_fsr3_3_1_5_bundle.h"
#include "ffx_vk_portable.h"

#include <cstddef>
#include <cstdint>

namespace {
// SDK 2.3's FfxFsr3UpscalerPass values the catalog takes (prepare inputs ..
// autogen reactive) and the one permutation it embeds of each: Lanczos, HDR
// and low-resolution motion vectors, plus sharpening for accumulate-sharpen.
constexpr std::uint32_t kPasses = 11;
constexpr std::uint32_t kBase = 1u | 2u | 4u, kSharpen = 32u;
}  // namespace

int upscaler_fsr3_embedded_modules() {
    int ok = 0;
    for (std::uint32_t pass = 0; pass < kPasses; ++pass) {
        const std::uint32_t* words = nullptr;
        std::size_t count = 0;
        const std::uint32_t perm = pass == 7 ? kBase | kSharpen : kBase;
        if (ffxVkFsr3_3_1_5GetEmbeddedModule(pass, perm, &words, &count) == FFX_VK_PORTABLE_OK && words && count > 5 &&
            words[0] == 0x07230203u)
            ++ok;
    }
    return ok;
}

bool upscaler_fsr3_linked() {
    // Taken through volatile so the linker keeps both upscalers' closures -
    // and with them their shader tables - in the binary from Phase 0 on.
    void* volatile entries[] = {reinterpret_cast<void*>(&ffxVkFsr3_3_1_5UpscalerContextCreate),
                                reinterpret_cast<void*>(&ffxVkFsr3_3_1_5UpscalerContextRecordDispatch),
                                reinterpret_cast<void*>(&ffxVkFsr3_3_1_5UpscalerContextDestroy),
                                reinterpret_cast<void*>(&ffxVkPortableUpscaleContextCreate),
                                reinterpret_cast<void*>(&ffxVkPortableUpscaleContextRecordDispatch),
                                reinterpret_cast<void*>(&ffxVkPortableUpscaleContextDestroy)};
    bool all = true;
    for (void* e : entries) all = all && e != nullptr;
    return all;
}
