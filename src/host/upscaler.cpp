// bbhost-vk's upscaler settings and their scaffolding (host/upscaler.h).
#include "host/upscaler.h"

#include "engine/scene_split.h"
#include "host/options.h"
#include "host/settings.h"
#include "log.h"

#include <atomic>
#include <cstdint>

#if !defined(BBHOST_HAVE_FSR_VULKAN)
// No FSR-Vulkan in this build (BBHOST_FSR_VULKAN=OFF, or no Vulkan).
int upscaler_fsr3_embedded_modules() { return 0; }
bool upscaler_fsr3_linked() { return false; }
#endif

namespace {

void log_settings(const char* why) {
    const HostSettings s = host_settings();
    std::uint32_t w = 0, h = 0;
    upscaler::render_size(s.upscale_quality, 3840, 2160, &w, &h);
    host_log("upscaler: %s: %s, %s (%ux%u from 3840x2160), sharpness %.1f; motion vectors view %s, GPU time %s "
             "(Phase 1: a preset below Native AA renders the scene smaller, scaled up bilinear; the window still gets FSR 1)",
             why, upscaler::kind_name(s.upscaler), upscaler::preset_name(s.upscale_quality), w, h, static_cast<double>(s.upscale_sharpness),
             s.debug_motion_vectors ? "on" : "off", s.debug_upscaler_time ? "on" : "off");
}

}  // namespace

void upscaler_log_startup() {
    const int modules = upscaler_fsr3_embedded_modules();
    if (upscaler_fsr3_linked()) {
        host_log("upscaler: FSR-Vulkan linked: FSR 3.1.5 and 3.1.4 upscalers, %d of 11 FSR 3.1.5 shader modules embedded%s", modules,
                 modules == 11 ? "" : "  <- the shader tables are incomplete");
    } else {
        host_log("upscaler: FSR-Vulkan not in this build");
    }
    log_settings("settings at start");
}

void upscaler_note_settings() {
    static int last[5] = {-1, -1, -1, -1, -1};
    const HostSettings s = host_settings();
    const int now[5] = {s.upscaler, s.upscale_quality, static_cast<int>(s.upscale_sharpness * 10.0f + 0.5f), s.debug_motion_vectors ? 1 : 0,
                        s.debug_upscaler_time ? 1 : 0};
    bool changed = false;
    for (int i = 0; i < 5; ++i) changed |= now[i] != last[i];
    const bool first = last[0] < 0;
    for (int i = 0; i < 5; ++i) last[i] = now[i];
    if (changed && !first) log_settings("changed");
}

void upscaler_debug_motion_vectors_hook(bool drawn) {
    // Phase 2: the presenter drew the motion image over the picture (drawn),
    // or there was none - the scene is not split, or no scene this frame
    // (menus, loads), or the picture went the magnified (FSR 1) path. Each
    // change is said once.
    static std::atomic<int> said{-1};
    const int now = drawn ? 1 : 0;
    if (said.exchange(now) != now)
        host_log(drawn ? "upscaler: the motion-vector view is drawn over the picture"
                       : "upscaler: the motion-vector view is on, but there are no motion vectors to show (the scene is not split, no scene, "
                         "or the picture is magnified by FSR 1)");
}

ScenePass upscaler_scene_pass() {
    // Phase 1: the split always takes the bilinear copy, whichever upscaler
    // is chosen; Phase 3 adds FSR 3.1 here.
    return scene_split_active() ? ScenePass::Bilinear : ScenePass::Game;
}

const char* upscaler_scene_pass_name(ScenePass p) {
    switch (p) {
    case ScenePass::Game: return "the game's own";
    case ScenePass::Bilinear: return "bilinear";
    }
    return "?";
}
