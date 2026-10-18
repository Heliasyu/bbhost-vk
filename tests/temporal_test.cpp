// bbhost-vk Phase 2: the temporal upscaler's inputs, as pure math -
// Halton jitter (host/temporal/halton.h), the scene constants, camera
// history and cuts, and the camera motion vectors (host/temporal/camera.h),
// and the draw classification (host/temporal/motion_classify.h).
#include "host/temporal/camera.h"
#include "host/temporal/fsr3_policy.h"
#include "host/temporal/halton.h"
#include "host/temporal/motion_classify.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                    \
        }                                                                \
    } while (0)

using namespace temporal;

namespace {

bool near(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

// A scene-constant block as the game lays it out (bbport's table): the
// camera at `pos` turned `yaw` degrees about +y, the game's projection
// (x 1.42799, y 2.53865, near 0.05, far 3000).
std::vector<float> block(double pos[3], double yaw_deg, double pitch_deg = 0.0, float fov_scale = 1.0f, float w = 2560, float h = 1440) {
    std::vector<float> f(kSceneConstantFloats, 0.0f);
    f[0] = 3000.0f;
    f[1] = 1.0f / 3000.0f;
    f[4] = w;
    f[5] = h;
    f[6] = 1.0f / w;
    f[7] = 1.0f / h;
    const double ya = yaw_deg * 3.14159265358979323846 / 180.0, pa = pitch_deg * 3.14159265358979323846 / 180.0;
    // Camera-to-world rotation: yaw about y, then pitch about x.
    const double cy = std::cos(ya), sy = std::sin(ya), cp = std::cos(pa), sp = std::sin(pa);
    const double r[9] = {cy, sy * sp, sy * cp, 0, cp, -sp, -sy, cy * sp, cy * cp};  // columns: right, up, forward
    // World-to-view = transpose(R) and -transpose(R) * pos.
    for (int row = 0; row < 3; ++row) {
        const double a = r[0 * 3 + row], b = r[1 * 3 + row], c = r[2 * 3 + row];
        f[8 + row * 4 + 0] = static_cast<float>(a);
        f[8 + row * 4 + 1] = static_cast<float>(b);
        f[8 + row * 4 + 2] = static_cast<float>(c);
        f[8 + row * 4 + 3] = static_cast<float>(-(a * pos[0] + b * pos[1] + c * pos[2]));
    }
    f[52] = 1.42799f * fov_scale;
    f[57] = 2.53865f * fov_scale;
    f[62] = 1.00002f;
    f[63] = -0.0500679f;
    f[67] = 1.0f;
    return f;
}

SceneCamera cam(double x, double y, double z, double yaw, double pitch = 0.0, float fov = 1.0f, float w = 2560, float h = 1440) {
    double p[3] = {x, y, z};
    const std::vector<float> f = block(p, yaw, pitch, fov, w, h);
    SceneCamera c;
    const char* why = nullptr;
    const bool ok = parse_scene_constants(f.data(), f.size(), &c, &why);
    if (!ok) std::fprintf(stderr, "parse failed: %s\n", why ? why : "?");
    CHECK(ok);
    return c;
}

void test_halton() {
    CHECK(near(halton(1, 2), 0.5, 1e-12));
    CHECK(near(halton(2, 2), 0.25, 1e-12));
    CHECK(near(halton(3, 2), 0.75, 1e-12));
    CHECK(near(halton(1, 3), 1.0 / 3, 1e-12));
    CHECK(near(halton(2, 3), 2.0 / 3, 1e-12));
    CHECK(near(halton(4, 3), 4.0 / 9, 1e-12));
    CHECK(halton(0, 2) == 0.0);
    // FidelityFX's phase counts.
    CHECK(jitter_phase_count(2560, 3840) == 18);  // Quality
    CHECK(jitter_phase_count(2258, 3840) == 23);  // Balanced
    CHECK(jitter_phase_count(1920, 3840) == 32);  // Performance
    CHECK(jitter_phase_count(1280, 3840) == 72);  // Ultra Performance
    CHECK(jitter_phase_count(3840, 3840) == 8);   // native
    CHECK(jitter_phase_count(0, 3840) == 1);
    // Offsets: in [-0.5, 0.5), the cycle repeats, the first is (0, -1/6),
    // and over a whole cycle they average near zero.
    const Jitter j0 = jitter_offset(0, 18);
    CHECK(near(j0.x, 0.0, 1e-6) && near(j0.y, 1.0 / 3 - 0.5, 1e-6));
    const Jitter j18 = jitter_offset(18, 18);
    CHECK(j18.x == j0.x && j18.y == j0.y && j18.phase == 0);
    double sx = 0, sy = 0;
    for (std::uint64_t f = 0; f < 32; ++f) {
        const Jitter j = jitter_offset(f, 32);
        CHECK(j.x >= -0.5f && j.x < 0.5f && j.y >= -0.5f && j.y < 0.5f);
        sx += j.x;
        sy += j.y;
    }
    CHECK(std::fabs(sx / 32) < 0.03 && std::fabs(sy / 32) < 0.03);
}

void test_parse() {
    SceneCamera c = cam(100, 5, -40, 30);
    CHECK(c.valid && c.width == 2560 && c.height == 1440 && c.far_plane == 3000.0);
    CHECK(near(c.position[0], 100, 1e-3) && near(c.position[1], 5, 1e-3) && near(c.position[2], -40, 1e-3));
    // Forward is +z rotated 30 degrees about y.
    CHECK(near(c.forward[0], std::sin(30 * 3.14159265358979323846 / 180), 1e-5) && near(c.forward[2], std::cos(30 * 3.14159265358979323846 / 180), 1e-5));
    double p[3] = {0, 0, 0};
    std::vector<float> f = block(p, 0);
    SceneCamera bad;
    f[0] = 0;
    CHECK(!parse_scene_constants(f.data(), f.size(), &bad));
    f = block(p, 0);
    f[6] = 0.5f;
    CHECK(!parse_scene_constants(f.data(), f.size(), &bad));
    f = block(p, 0);
    f[8] = 2.0f;  // a scaled row: not a camera
    CHECK(!parse_scene_constants(f.data(), f.size(), &bad));
    f = block(p, 0);
    CHECK(!parse_scene_constants(f.data(), 100, &bad));
    f[20] = NAN;
    CHECK(!parse_scene_constants(f.data(), f.size(), &bad));
}

void test_history() {
    CameraHistory h;
    CHECK(h.end_frame(10) == Cut::First);  // nothing seen yet
    h.observe(cam(0, 0, 0, 0));
    CHECK(h.end_frame(11) == Cut::Gap);  // the frame before had no camera
    h.observe(cam(0.2, 0, 0, 1));
    CHECK(h.end_frame(12) == Cut::None && h.usable());
    // Majority: two cameras, the second bound by more draws.
    h.observe(cam(500, 0, 0, 0));
    h.observe(cam(0.4, 0, 0, 2));
    h.observe(cam(0.4, 0, 0, 2));
    CHECK(h.cameras_this_frame() == 2);
    CHECK(h.end_frame(13) == Cut::None && near(h.current().position[0], 0.4, 1e-4));
    // Cuts.
    h.observe(cam(30, 0, 0, 2));
    CHECK(h.end_frame(14) == Cut::Teleport && !h.usable());
    h.observe(cam(30, 0, 0, 80));
    CHECK(h.end_frame(15) == Cut::Rotation);
    h.observe(cam(30, 0, 0, 80, 0, 1.2f));
    CHECK(h.end_frame(16) == Cut::Projection);
    h.observe(cam(30, 0, 0, 80, 0, 1.2f, 1920, 1080));
    CHECK(h.end_frame(17) == Cut::Resize);
    h.observe(cam(30, 0, 0, 80, 0, 1.2f, 1920, 1080));
    CHECK(h.end_frame(21) == Cut::Gap);
    h.observe(cam(30.1, 0, 0, 81, 0, 1.2f, 1920, 1080));
    CHECK(h.end_frame(22) == Cut::None);
    CHECK(h.end_frame(23) == Cut::NoCamera && !h.usable());
}

// The motion of a world point between two cameras, computed the way the
// GPU pass does (float reprojection from double), against projecting the
// point with each camera directly.
void check_motion(const SceneCamera& prev, const SceneCamera& cur, const double world[3], const double jit[2], double eps) {
    const std::uint32_t w = cur.width, hh = cur.height;
    double cx, cy, depth, px, py, pd;
    const double none[2] = {0, 0};
    if (!project(cur, world, w, hh, jit, &cx, &cy, &depth) || !project(prev, world, w, hh, none, &px, &py, &pd)) {
        CHECK(false);
        return;
    }
    // The pixel whose centre the jittered point falls in; the expected
    // motion is from where that pixel's surface is without jitter.
    if (cx < 0 || cy < 0 || cx >= w || cy >= hh) {
        std::fprintf(stderr, "test point off screen at (%g, %g)\n", cx, cy);
        CHECK(false);
        return;
    }
    const auto x = static_cast<std::uint32_t>(std::floor(cx)), y = static_cast<std::uint32_t>(std::floor(cy));
    // Re-derive the surface at the pixel centre: move the world point so it
    // projects to the centre exactly (same depth), as a pixel sees it.
    const double ux = (x + 0.5 - jit[0]) / w, uy = (y + 0.5 - jit[1]) / hh;
    const double z = cur.proj.zo / (depth - cur.proj.zs);
    const double vx = (2 * ux - 1) * z / cur.proj.x, vy = (1 - 2 * uy) * z / cur.proj.y;
    Affine inv;
    affine_inverse(cur.view, &inv);
    const double v[3] = {vx, vy, z};
    double wc[3];
    affine_point(inv, v, wc);
    double ex, ey, ed;
    project(prev, wc, w, hh, none, &ex, &ey, &ed);
    const Reprojection r = make_reprojection(prev, cur);
    double dx, dy;
    camera_motion(r, x, y, depth, w, hh, jit, &dx, &dy);
    const double want_x = ex - (x + 0.5 - jit[0]), want_y = ey - (y + 0.5 - jit[1]);
    if (!near(dx, want_x, eps) || !near(dy, want_y, eps)) {
        std::fprintf(stderr, "motion (%g, %g), want (%g, %g)\n", dx, dy, want_x, want_y);
        CHECK(false);
    }
}

void test_motion() {
    const double none[2] = {0, 0};
    // A still camera: zero motion, with or without jitter.
    {
        const SceneCamera a = cam(10, 2, 10, 20), b = cam(10, 2, 10, 20);
        const Reprojection r = make_reprojection(a, b);
        for (const double d : {0.2, 0.9, 0.999, 1.0}) {
            double dx, dy;
            const double jit[2] = {0.3, -0.2};
            camera_motion(r, 700, 300, d, 2560, 1440, jit, &dx, &dy);
            CHECK(near(dx, 0, 1e-3) && near(dy, 0, 1e-3));
        }
    }
    // Strafing right: near points move left on screen more than far ones.
    {
        const SceneCamera prev = cam(0, 1.5, 0, 0), cur = cam(0.1, 1.5, 0, 0);
        const double nearp[3] = {0.5, 1.5, 3}, farp[3] = {0.5, 1.5, 300};
        check_motion(prev, cur, nearp, none, 1e-2);
        check_motion(prev, cur, farp, none, 1e-2);
        double cx, cy, d;
        project(cur, nearp, 2560, 1440, none, &cx, &cy, &d);
        double dx, dy;
        camera_motion(make_reprojection(prev, cur), static_cast<std::uint32_t>(cx), static_cast<std::uint32_t>(cy), d, 2560, 1440, none, &dx, &dy);
        CHECK(dx > 10 && std::fabs(dy) < 0.05);  // the point slid left as the camera went right: back to the right is where it was
    }
    // Turning, walking, jittered; far from the map origin (float noise).
    {
        const double jit[2] = {-0.37, 0.21};
        const SceneCamera prev = cam(1834.25, 42.5, -2210.75, 47.0, 4.0), cur = cam(1834.31, 42.52, -2210.70, 47.9, 4.2);
        const double pts[][3] = {{1840, 43, -2205}, {1835.5, 42.3, -2208.6}, {1900, 60, -2100}};
        for (const auto& p : pts) check_motion(prev, cur, p, jit, 2e-2);
    }
    // The sky (depth 1) rotates with the camera and ignores translation.
    {
        const SceneCamera prev = cam(0, 0, 0, 0), cur = cam(50, 0, 0, 2);  // a big move, a 2 degree turn
        const Reprojection r = make_reprojection(prev, cur);
        double dx, dy;
        camera_motion(r, 1280, 720, 1.0, 2560, 1440, none, &dx, &dy);
        // 2 degrees of yaw at the centre: x scale 1.42799 -> tan(2 deg) * 1.42799 * 1280 px.
        const double want = std::tan(2 * 3.14159265358979323846 / 180) * 1.42799 * 1280;
        CHECK(near(std::fabs(dx), want, 0.5) && std::fabs(dy) < 0.05);
    }
}

void test_classify() {
    CHECK(parse_hash("ccbf44a6") == kScenePassPs);
    CHECK(parse_hash("34BC187C") == kOpaqueVelocityPs);
    CHECK(parse_hash("zz") == 0);
    CHECK(is_velocity_ps(0x749e4f9eu) && is_velocity_ps(0xb25e4faeu) && !is_velocity_ps(kScenePassPs));
    CHECK(velocity_scale_buffer(kOpaqueVelocityPs) == 0 && velocity_scale_buffer(kAlphaVelocityPsA) == 1 && velocity_scale_buffer(1) == -1);
    CHECK(classify_buffer(864) == BufferKind::Scene);
    CHECK(classify_buffer(640) == BufferKind::Skeleton && classify_buffer(16384) == BufferKind::Skeleton);
    CHECK(classify_buffer(96) == BufferKind::SmallSkeleton && classify_buffer(384) == BufferKind::SmallSkeleton);
    CHECK(classify_buffer(416) == BufferKind::Other);  // bbport: changes with the camera, not a palette
    CHECK(classify_buffer(100) == BufferKind::Other && classify_buffer(16400) == BufferKind::Other);
    DrawFacts d;
    d.scene_depth = true;
    d.index_count = 3;
    CHECK(!jitter_eligible(d));  // a full-screen triangle
    d.index_count = 6;
    CHECK(!jitter_eligible(d));  // a full-screen quad
    d.index_count = 7;
    CHECK(jitter_eligible(d));
    d.index_count = 3;
    d.instances = 4;
    CHECK(jitter_eligible(d));
    d.instances = 1;
    d.indirect = true;
    CHECK(jitter_eligible(d));
    d.ui = true;
    CHECK(!jitter_eligible(d));
    d.ui = false;
    d.scene_depth = false;
    CHECK(!jitter_eligible(d));
    CHECK(blend_enabled(1u << 30) && !blend_enabled(0x20010001u));
    MovingGate g;
    CHECK(g.moved(1, 0xaa, 100));   // first sight
    CHECK(!g.moved(1, 0xaa, 101));  // same palette next frame: static
    CHECK(g.moved(1, 0xab, 102));   // changed
    CHECK(g.moved(1, 0xab, 110));   // a gap: no history to trust
    g.sweep(110 + MovingGate::kForgetFrames + 1);
    CHECK(g.size() == 0);
}

// bbhost-vk Phase 3: FSR 3.1's policy (host/temporal/fsr3_policy.h).
void test_fsr3_policy() {
    // Sizes: render <= output <= 3x render (+8 for rounding).
    CHECK(fsr3_sizes_ok({2560, 1440, 3840, 2160}));   // Quality at 4K
    CHECK(fsr3_sizes_ok({1920, 1080, 3840, 2160}));   // Performance
    CHECK(fsr3_sizes_ok({1280, 720, 3840, 2160}));    // Ultra Performance, 3x
    CHECK(fsr3_sizes_ok({3840, 2160, 3840, 2160}));   // Native AA
    CHECK(!fsr3_sizes_ok({1000, 720, 3840, 2160}));   // past 3x
    CHECK(!fsr3_sizes_ok({3840, 2160, 2560, 1440}));  // down-scaling
    CHECK(!fsr3_sizes_ok({0, 1440, 3840, 2160}));
    CHECK(fsr3_context_fits({2560, 1440, 3840, 2160}, {2560, 1440, 3840, 2160}));
    CHECK(!fsr3_context_fits({2560, 1440, 3840, 2160}, {1920, 1080, 3840, 2160}));  // a preset change: a new context
    // Resets: a new context, then cuts, gaps and requests; a plain next frame keeps the history.
    Fsr3History h;
    CHECK(fsr3_reset(h, 10, false, false) == Fsr3Reset::NewContext);
    CHECK(fsr3_reset(h, 11, false, false) == Fsr3Reset::None);
    CHECK(fsr3_reset(h, 12, true, false) == Fsr3Reset::Cut);
    CHECK(fsr3_reset(h, 13, false, false) == Fsr3Reset::None);
    CHECK(fsr3_reset(h, 15, false, false) == Fsr3Reset::None);  // a dropped flip or two
    CHECK(fsr3_reset(h, 30, false, false) == Fsr3Reset::Gap);   // frames without the upscaler
    CHECK(fsr3_reset(h, 31, false, true) == Fsr3Reset::Requested);
    CHECK(fsr3_reset(h, 5, false, false) == Fsr3Reset::Gap);    // the counter went back
    CHECK(fsr3_reset(h, 6, true, true) == Fsr3Reset::Cut);      // a cut names itself first
    h = Fsr3History{};
    CHECK(fsr3_reset(h, 7, true, false) == Fsr3Reset::NewContext);
    // Sharpening: the setting, clamped; 0 is off.
    CHECK(!fsr3_sharpen(0.0f).enable);
    CHECK(fsr3_sharpen(0.8f).enable && near(fsr3_sharpen(0.8f).sharpness, 0.8, 1e-6));
    CHECK(fsr3_sharpen(3.0f).sharpness == 1.0f && !fsr3_sharpen(-1.0f).enable);
    // Conventions: bbhost's jitter is FSR's jitterOffset; pixel motion vectors at scale 1.
    const Fsr3Conventions c = fsr3_conventions(0.25f, -0.125f);
    CHECK(c.jitter_x == 0.25f && c.jitter_y == -0.125f && c.mv_scale_x == 1.0f && c.mv_scale_y == 1.0f);
    const Fsr3Conventions f = fsr3_conventions(0.25f, -0.125f, -1, 1);
    CHECK(f.jitter_x == -0.25f && f.jitter_y == -0.125f);
    // The camera: the game's values, defaults for nonsense.
    const Fsr3Camera cam = fsr3_camera(0.05f, 3000.0f, 0.8f);
    CHECK(cam.near_plane == 0.05f && cam.far_plane == 3000.0f && cam.fov_y == 0.8f);
    const Fsr3Camera bad = fsr3_camera(0.0f, NAN, 0.0f);
    CHECK(bad.near_plane > 0.0f && bad.far_plane > bad.near_plane && bad.fov_y > 0.0f);
}

}  // namespace

int main() {
    test_halton();
    test_parse();
    test_history();
    test_motion();
    test_classify();
    test_fsr3_policy();
    if (g_fail) {
        std::fprintf(stderr, "temporal_test: %d failures\n", g_fail);
        return 1;
    }
    std::printf("temporal_test: Halton jitter, scene constants, camera cuts, camera motion, draw classes and FSR 3.1 policy ok\n");
    return 0;
}
