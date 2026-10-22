// bbhost-vk: DualSense / DualSense Edge HD haptics and adaptive triggers
// (host/haptics.h).
#include "host/haptics.h"
#include "host/options.h"
#include "host/settings.h"
#include "log.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(BBHOST_HAVE_SDL3)
#include <SDL3/SDL.h>
#endif

namespace {

constexpr float kRate = 48000.0f;
constexpr float kTwoPi = 6.28318530718f;

// A one-pole follower's coefficient for a time constant in milliseconds.
float follow(float ms) { return 1.0f - std::exp(-1.0f / (ms * 0.001f * kRate)); }

// A second-order low-pass (the RBJ cookbook's), run twice for a steeper edge.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass(float fc, float q) {
        const float w = kTwoPi * fc / kRate, c = std::cos(w), alpha = std::sin(w) / (2.0f * q), a0 = 1.0f + alpha;
        b0 = (1.0f - c) / 2.0f / a0;
        b1 = (1.0f - c) / a0;
        b2 = b0;
        a1 = -2.0f * c / a0;
        a2 = (1.0f - alpha) / a0;
    }
    float run(float x) {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

struct Envelope {
    float attack = 0, release = 0, v = 0;
    float run(float x) {
        v += (x - v) * (x > v ? attack : release);
        return v;
    }
};

// What can be tuned between test rounds without a build:
// BBHOST_HAPTICS_TUNE="cutoff=160,sustain=0.15,floor=0.003,carrier=0.5,gain=3,rumble=0.8"
//   cutoff   the effects' low-pass, Hz (what the motors are given of the sound)
//   sustain  how much of a steady sound stays (0: only hits; 1: everything)
//   floor    the level below which a sound is not felt at all
//   carrier  0: the filtered sound itself; 1: a 150 Hz tone that follows it
//   gain     the effects' level before the soft limit
//   rumble   the converted rumble's level
// BBHOST_HAPTICS_CHANNELS="2,3": the device channels of the left and right motors.
struct Tuning {
    float cutoff = 160.0f, sustain = 0.15f, floor = 0.003f, carrier = 0.5f, gain = 3.0f, rumble = 0.8f;
    int left = 2, right = 3;
};
Tuning read_tuning() {
    Tuning t;
    if (const char* e = std::getenv("BBHOST_HAPTICS_TUNE")) {
        std::string s = e;
        std::size_t at = 0;
        while (at < s.size()) {
            const std::size_t end = std::min(s.find(',', at), s.size());
            const std::string kv = s.substr(at, end - at);
            const std::size_t eq = kv.find('=');
            if (eq != std::string::npos) {
                const std::string k = kv.substr(0, eq);
                const float v = std::strtof(kv.c_str() + eq + 1, nullptr);
                if (k == "cutoff") t.cutoff = std::clamp(v, 40.0f, 1000.0f);
                else if (k == "sustain") t.sustain = std::clamp(v, 0.0f, 1.0f);
                else if (k == "floor") t.floor = std::clamp(v, 0.0f, 1.0f);
                else if (k == "carrier") t.carrier = std::clamp(v, 0.0f, 1.0f);
                else if (k == "gain") t.gain = std::clamp(v, 0.0f, 50.0f);
                else if (k == "rumble") t.rumble = std::clamp(v, 0.0f, 2.0f);
            }
            at = end + 1;
        }
    }
    if (const char* e = std::getenv("BBHOST_HAPTICS_CHANNELS")) {
        int l = 0, r = 0;
        if (std::sscanf(e, "%d,%d", &l, &r) == 2 && l >= 0 && l < 4 && r >= 0 && r < 4) {
            t.left = l;
            t.right = r;
        }
    }
    return t;
}

// One motor's share of the effects: the side's low end, how loud it is now
// (fast) and over the last half second (slow), and how much of it is a hit.
struct Side {
    Biquad lp1, lp2;
    Envelope fast, slow, gate;
    void init(const Tuning& t) {
        lp1.lowpass(t.cutoff, 0.707f);
        lp2.lowpass(t.cutoff, 0.707f);
        fast.attack = follow(1.0f);
        fast.release = follow(40.0f);
        slow.attack = follow(400.0f);
        slow.release = follow(400.0f);
        gate.attack = follow(2.0f);
        gate.release = follow(60.0f);
    }
    // The filtered sound and the envelope the tone follows, both shaped by
    // the transient gate.
    void run(float x, const Tuning& t, float* sound, float* level) {
        const float low = lp2.run(lp1.run(x));
        const float e = fast.run(std::fabs(low));
        const float s = slow.run(std::fabs(low));
        // A hit: louder now than the last half second has been.
        const float hit = std::clamp((e - 1.4f * s) / (e + 1e-6f), 0.0f, 1.0f);
        const float g = gate.run(hit);
        // Under the floor nothing is felt, with a soft knee to it.
        const float audible = std::clamp((e - t.floor) / (t.floor + 1e-6f), 0.0f, 1.0f);
        const float shape = (g + t.sustain) * audible;
        *sound = low * shape;
        *level = e * shape;
    }
};

#if defined(BBHOST_HAVE_SDL3)
std::mutex g_mu;  // everything below, from the game's audio thread and the shutdown
SDL_AudioStream* g_stream = nullptr;
SDL_AudioDeviceID g_device = 0;
bool g_audio_up = false;  // our SDL_InitSubSystem(SDL_INIT_AUDIO), to be paired with a quit
bool g_shut = false;
std::chrono::steady_clock::time_point g_last_scan{};
bool g_said_none = false;
std::vector<float> g_out;
#endif
std::atomic<bool> g_rescan{true};

Tuning g_tune;
bool g_tune_read = false;
Side g_side[2];
float g_carrier_phase = 0.0f, g_large_phase = 0.0f, g_small_phase = 0.0f;
float g_large = 0.0f, g_small = 0.0f;  // the rumble's smoothed amplitudes

// The rumble the game asked for, and until when (its pad thread; a call holds
// for 100 ms, as the SDL rumble did).
std::atomic<std::uint32_t> g_rumble{0};  // small << 8 | large
std::atomic<std::int64_t> g_rumble_until_us{0};
std::atomic<bool> g_hd_live{false};  // an HD output is open and fed

std::int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The settings, re-read when they change.
struct Wanted {
    std::uint64_t serial = 0;
    int mode = 2;
    float strength = 1.0f;
    bool triggers = true;
    int trigger_strength = 1;
};
std::mutex g_wanted_mu;
Wanted g_wanted;
Wanted wanted() {
    const std::uint64_t s = host_opt_serial();
    std::lock_guard<std::mutex> lock(g_wanted_mu);
    if (s != g_wanted.serial) {
        const HostSettings h = host_settings();
        g_wanted.serial = s;
        g_wanted.mode = h.haptics;
        g_wanted.strength = h.haptics_strength;
        g_wanted.triggers = h.adaptive_triggers;
        g_wanted.trigger_strength = h.trigger_strength;
        g_rescan = true;
    }
    return g_wanted;
}

#if defined(BBHOST_HAVE_SDL3)
bool contains_nocase(const char* hay, const char* needle) {
    if (!hay) return false;
    const std::size_t n = std::strlen(needle);
    for (; *hay; ++hay) {
        std::size_t i = 0;
        while (i < n && hay[i] && std::tolower(static_cast<unsigned char>(hay[i])) == std::tolower(static_cast<unsigned char>(needle[i]))) ++i;
        if (i == n) return true;
    }
    return false;
}

void close_locked(const char* why) {
    if (!g_stream) return;
    SDL_DestroyAudioStream(g_stream);
    g_stream = nullptr;
    g_device = 0;
    g_hd_live = false;
    host_log("haptics: HD output closed (%s)", why);
}

// The controller's sound card: a playback device named DualSense with at least
// four channels. Only while plugged in by USB is there one.
void scan_locked(bool want_hd) {
    g_last_scan = std::chrono::steady_clock::now();
    if (!want_hd) {
        close_locked("Haptics is not HD");
        return;
    }
    if (!g_audio_up) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            host_log("haptics: no SDL audio (%s); the classic rumble runs", SDL_GetError());
            return;
        }
        g_audio_up = true;
    }
    int count = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
    SDL_AudioDeviceID found = 0;
    const char* found_name = nullptr;
    for (int i = 0; ids && i < count && !found; ++i) {
        const char* name = SDL_GetAudioDeviceName(ids[i]);
        if (!contains_nocase(name, "dualsense")) continue;
        SDL_AudioSpec spec{};
        int frames = 0;
        if (SDL_GetAudioDeviceFormat(ids[i], &spec, &frames) && spec.channels >= 4) {
            found = ids[i];
            found_name = name;
        }
    }
    if (g_stream && found != g_device) close_locked("the controller's sound card is gone");
    if (found && !g_stream) {
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32;
        spec.channels = 4;
        spec.freq = static_cast<int>(kRate);
        // A short device buffer (SDL's default is 1024 frames, 21 ms): a hit
        // felt after it is heard feels wrong. Only for this device - the hint
        // is read as a device opens, and the game's own output opened long
        // before.
        const char* had = SDL_GetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
        const std::string kept = had ? had : "";
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256");
        g_stream = SDL_OpenAudioDeviceStream(found, &spec, nullptr, nullptr);
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, had ? kept.c_str() : nullptr);
        if (!g_stream) {
            host_log("haptics: %s would not open (%s); the classic rumble runs", found_name ? found_name : "the DualSense", SDL_GetError());
            return;
        }
        SDL_ResumeAudioStreamDevice(g_stream);
        g_device = found;
        g_hd_live = true;
        g_said_none = false;
        host_log("haptics: HD output on \"%s\" (motors on channels %d and %d of 4)", found_name ? found_name : "?", g_tune.left + 1,
                 g_tune.right + 1);
    } else if (!found && !g_said_none) {
        g_said_none = true;
        host_log("haptics: no DualSense sound card (HD haptics need the controller plugged in by USB); the classic rumble runs");
    }
    SDL_free(ids);
}
#endif

// One pass over a buffer: the effects and the rumble, into the motors.
void render(const float* in, unsigned count, float gain, const Wanted& w, float* out) {
    const std::uint32_t r = g_rumble.load(std::memory_order_relaxed);
    const bool held = now_us() < g_rumble_until_us.load(std::memory_order_relaxed);
    const float want_large = held ? std::pow(static_cast<float>(r & 0xff) / 255.0f, 0.7f) : 0.0f;
    const float want_small = held ? std::pow(static_cast<float>((r >> 8) & 0xff) / 255.0f, 0.7f) : 0.0f;
    const float smooth = follow(4.0f);
    const float strength = w.strength;
    const Tuning& t = g_tune;
    for (unsigned i = 0; i < count; ++i) {
        const float* f = in ? in + i * 8 : nullptr;
        float sides[2] = {0.0f, 0.0f}, levels[2] = {0.0f, 0.0f};
        if (f) {
            // The fronts and the surrounds on their side, the LFE in both; the
            // centre (mostly voices) left out.
            const float lfe = f[3];
            const float left = (f[0] + 0.7f * (f[4] + f[6])) * gain + lfe * gain;
            const float right = (f[1] + 0.7f * (f[5] + f[7])) * gain + lfe * gain;
            g_side[0].run(left, t, &sides[0], &levels[0]);
            g_side[1].run(right, t, &sides[1], &levels[1]);
        }
        g_carrier_phase += kTwoPi * 150.0f / kRate;
        if (g_carrier_phase > kTwoPi) g_carrier_phase -= kTwoPi;
        const float tone = std::sin(g_carrier_phase);
        // The rumble: the large motor a 70 Hz tone, the small one 160 Hz.
        g_large += (want_large - g_large) * smooth;
        g_small += (want_small - g_small) * smooth;
        g_large_phase += kTwoPi * 70.0f / kRate;
        g_small_phase += kTwoPi * 160.0f / kRate;
        if (g_large_phase > kTwoPi) g_large_phase -= kTwoPi;
        if (g_small_phase > kTwoPi) g_small_phase -= kTwoPi;
        const float rumble = t.rumble * (0.65f * g_large * std::sin(g_large_phase) + 0.35f * g_small * std::sin(g_small_phase));
        float motor[2];
        for (int s = 0; s < 2; ++s) {
            const float fx = (1.0f - t.carrier) * sides[s] + t.carrier * 2.0f * levels[s] * tone;
            motor[s] = std::tanh((fx * t.gain + rumble) * strength) * 0.95f;
        }
        float* o = out + i * 4;
        o[0] = o[1] = o[2] = o[3] = 0.0f;
        o[t.left] = motor[0];
        o[t.right] = motor[1];
    }
}

}  // namespace

int haptics_mode() { return wanted().mode; }

bool haptics_is_dualsense(std::uint16_t vendor, std::uint16_t product) {
    return vendor == 0x054c && (product == 0x0ce6 || product == 0x0df2);
}

void haptics_devices_changed() { g_rescan = true; }

bool haptics_rumble(std::uint8_t small, std::uint8_t large) {
    if (!g_hd_live.load(std::memory_order_relaxed) || wanted().mode != 2) return false;
    g_rumble.store(static_cast<std::uint32_t>(small) << 8 | large, std::memory_order_relaxed);
    g_rumble_until_us.store(now_us() + 100000, std::memory_order_relaxed);
    return true;
}

void haptics_game_audio(const float* frames, unsigned count, float gain) {
#if defined(BBHOST_HAVE_SDL3)
    const Wanted w = wanted();
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_shut) return;
    if (!g_tune_read) {
        g_tune_read = true;
        g_tune = read_tuning();
        for (Side& s : g_side) s.init(g_tune);
        if (std::getenv("BBHOST_HAPTICS_TUNE") || std::getenv("BBHOST_HAPTICS_CHANNELS")) {
            host_log("haptics: tuning cutoff %.0f Hz, sustain %.2f, floor %.4f, carrier %.2f, gain %.2f, rumble %.2f; motors on channels %d,%d",
                     static_cast<double>(g_tune.cutoff), static_cast<double>(g_tune.sustain), static_cast<double>(g_tune.floor),
                     static_cast<double>(g_tune.carrier), static_cast<double>(g_tune.gain), static_cast<double>(g_tune.rumble),
                     g_tune.left + 1, g_tune.right + 1);
        }
    }
    const bool want_hd = w.mode == 2;
    // Looked for when something changed, and every 3 s while HD is wanted
    // and there is no output yet (the controller may be plugged in later).
    if (g_rescan.exchange(false) || (want_hd && !g_stream && std::chrono::steady_clock::now() - g_last_scan > std::chrono::seconds(3))) {
        scan_locked(want_hd);
    }
    if (!g_stream || count == 0) return;
    g_out.resize(static_cast<std::size_t>(count) * 4);
    render(frames, count, gain, w, g_out.data());
    // Felt late is felt wrong: past 30 ms queued (the device stalled or the
    // game's audio ran ahead), the queue starts over.
    const int bytes = static_cast<int>(g_out.size() * sizeof(float));
    if (SDL_GetAudioStreamQueued(g_stream) > static_cast<int>(0.030f * kRate) * 4 * static_cast<int>(sizeof(float))) {
        SDL_ClearAudioStream(g_stream);
    }
    SDL_PutAudioStreamData(g_stream, g_out.data(), bytes);
#else
    (void)frames;
    (void)count;
    (void)gain;
#endif
}

void haptics_shutdown() {
#if defined(BBHOST_HAVE_SDL3)
    std::lock_guard<std::mutex> lock(g_mu);
    g_shut = true;
    close_locked("shutting down");
    if (g_audio_up) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        g_audio_up = false;
    }
#endif
}

// The trigger effect blocks. Ten zones along the travel, 0 at rest and 9 at
// the end; each mode's bytes as the firmware reads them (the layout every
// DualSense tool uses, after Nielk1's trigger effect generator).
void haptics_trigger_off(std::uint8_t out[11]) {
    std::memset(out, 0, 11);
    out[0] = 0x05;
}

// Resistance from `start` to the end of the travel, 1..8.
void haptics_trigger_feedback(std::uint8_t out[11], int start, int strength) {
    if (start < 0 || start > 9 || strength < 1) return haptics_trigger_off(out);
    strength = std::min(strength, 8);
    std::memset(out, 0, 11);
    std::uint32_t force = 0;
    std::uint16_t active = 0;
    for (int i = start; i < 10; ++i) {
        force |= static_cast<std::uint32_t>(strength - 1) << (3 * i);
        active |= static_cast<std::uint16_t>(1u << i);
    }
    out[0] = 0x21;
    out[1] = static_cast<std::uint8_t>(active & 0xff);
    out[2] = static_cast<std::uint8_t>(active >> 8);
    for (int b = 0; b < 4; ++b) out[3 + b] = static_cast<std::uint8_t>(force >> (8 * b));
}

// A trigger break: resistance from `start` that gives way at `end`.
void haptics_trigger_weapon(std::uint8_t out[11], int start, int end, int strength) {
    if (start < 2 || start > 7 || end <= start || end > 8 || strength < 1) return haptics_trigger_off(out);
    strength = std::min(strength, 8);
    std::memset(out, 0, 11);
    const std::uint16_t zones = static_cast<std::uint16_t>((1u << start) | (1u << end));
    out[0] = 0x25;
    out[1] = static_cast<std::uint8_t>(zones & 0xff);
    out[2] = static_cast<std::uint8_t>(zones >> 8);
    out[3] = static_cast<std::uint8_t>(strength - 1);
}

// A hum from `position` to the end of the travel: amplitude 1..8, Hz.
void haptics_trigger_vibration(std::uint8_t out[11], int position, int amplitude, int frequency) {
    if (position < 0 || position > 9 || amplitude < 1 || frequency < 1) return haptics_trigger_off(out);
    amplitude = std::min(amplitude, 8);
    std::memset(out, 0, 11);
    std::uint32_t amps = 0;
    std::uint16_t active = 0;
    for (int i = position; i < 10; ++i) {
        amps |= static_cast<std::uint32_t>(amplitude - 1) << (3 * i);
        active |= static_cast<std::uint16_t>(1u << i);
    }
    out[0] = 0x26;
    out[1] = static_cast<std::uint8_t>(active & 0xff);
    out[2] = static_cast<std::uint8_t>(active >> 8);
    for (int b = 0; b < 4; ++b) out[3 + b] = static_cast<std::uint8_t>(amps >> (8 * b));
    out[9] = static_cast<std::uint8_t>(std::min(frequency, 255));
}

TriggerEffects haptics_triggers(std::uint32_t id, float l2, float r2, bool relaxed) {
    TriggerEffects t;
    const Wanted w = wanted();
    if (!w.triggers || relaxed) {
        haptics_trigger_off(t.left);
        haptics_trigger_off(t.right);
        return t;
    }
    // Light, Medium, Strong.
    static constexpr int kResist[3] = {2, 3, 5}, kBreak[3] = {3, 5, 8}, kHum[3] = {4, 6, 8};
    const int s = std::clamp(w.trigger_strength, 0, 2);
    // L2, the gun: a break two-fifths of the way down.
    haptics_trigger_weapon(t.left, 3, 6, kBreak[s]);
    // R2, the heavy attack: light resistance; held past 350 ms (a charge),
    // a hum that grows over the next second.
    struct Charge {
        bool down = false;
        std::chrono::steady_clock::time_point since{};
    };
    static std::unordered_map<std::uint32_t, Charge> charges;  // the pump thread only
    Charge& c = charges[id];
    const auto now = std::chrono::steady_clock::now();
    if (r2 > 0.6f && !c.down) {
        c.down = true;
        c.since = now;
    } else if (r2 < 0.3f) {
        c.down = false;
    }
    const float held_ms = c.down ? std::chrono::duration<float, std::milli>(now - c.since).count() : 0.0f;
    if (held_ms > 350.0f) {
        const float grow = std::clamp((held_ms - 350.0f) / 1000.0f, 0.0f, 1.0f);
        const int amp = 1 + static_cast<int>(std::lround(grow * static_cast<float>(kHum[s] - 1)));
        haptics_trigger_vibration(t.right, 0, amp, 40);
    } else {
        haptics_trigger_feedback(t.right, 3, kResist[s]);
    }
    return t;
}
