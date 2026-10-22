#include "host/trophies.h"

#include "host/haptics.h"
#include "host/overlay.h"
#include "host/settings.h"
#include "log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>

namespace {

// The table follows Lantern Engine's launcher (github.com/Awakenchan/LanternEngine,
// GPL), from pyL1nx/bloodborne_pc's trophy research; the ids are the PS4's.
constexpr TrophyInfo kTrophies[] = {
    {0, "Bloodborne", "All trophies acquired. Hats off!", 1, false},
    {1, "Yharnam Sunrise", "You lived through the hunt, and saw another day.", 2, false},
    {2, "Honoring Wishes", "Captivated by the moon presence, you pledge to watch over the hunter's dream.", 2, false},
    {3, "Childhood's Beginning", "You became an infant Great One, lifting humanity into its next childhood.", 2, false},
    {4, "Yharnam, Pthumerian Queen", "Defeat Yharnam, Blood Queen of the Old Labyrinth.", 2, false},
    {5, "Hunter's Essence", "Acquire all hunter weapons.", 2, false},
    {6, "Hunter's Craft", "Acquire all special hunter tools.", 2, false},
    {7, "Weapon Master", "Acquire a weapon of the highest level.", 3, false},
    {8, "Blood Gem Master", "Acquire an extremely precious blood gem.", 3, false},
    {9, "Rune Master", "Acquire an extremely precious Caryll Rune.", 3, false},
    {10, "Cainhurst", "Gain entry to Cainhurst, the lost and ruined castle.", 3, false},
    {11, "The Choir", "Gain entry to the realm of the Choir, the high stratum of the Healing Church.", 3, false},
    {12, "The Source of the Dream", "Discover the abandoned old workshop, the source of the hunter's dream.", 3, false},
    {13, "Nightmare Lecture Building", "Gain entry into the Byrgenwerth lecture building, that drifts within the realm of nightmare.", 3, false},
    {14, "Father Gascoigne", "Defeat the beast that once was Father Gascoigne.", 4, false},
    {15, "Vicar Amelia", "Defeat the beast that once was Vicar Amelia.", 4, false},
    {16, "Shadow of Yharnam", "Defeat the Shadow of Yharnam.", 4, false},
    {17, "Rom, the Vacuous Spider", "Defeat Great One: Rom, the Vacuous Spider.", 4, false},
    {18, "The One Reborn", "Defeat the One Reborn.", 4, false},
    {19, "Micolash, Host of the Nightmare", "Defeat Micolash, Host of the Nightmare.", 4, false},
    {20, "Mergo's Wet Nurse", "Defeat Great One: Mergo's Wet Nurse.", 4, false},
    {21, "Cleric Beast", "Defeat Cleric Beast.", 4, false},
    {22, "Blood-starved Beast", "Defeat Blood-starved Beast.", 4, false},
    {23, "The Witch of Hemwick", "Defeat the Witch of Hemwick.", 4, false},
    {24, "Darkbeast Paarl", "Defeat Darkbeast Paarl.", 4, false},
    {25, "Amygdala", "Defeat Great One: Amygdala.", 4, false},
    {26, "Martyr Logarius", "Defeat Martyr Logarius.", 4, false},
    {27, "Celestial Emissary", "Defeat Great One: Celestial Emissary.", 4, false},
    {28, "Ebrietas, Daughter of the Cosmos", "Defeat Great One: Ebrietas, Daughter of the Cosmos.", 4, false},
    {29, "Blood Gem Contact", "Acquire a blood gem that imbues hunter weapons with special strength.", 4, false},
    {30, "Rune Contact", "Acquire a Caryll Rune that endows hunters with special strength.", 4, false},
    {31, "Chalice of Pthumeru", "Acquire the Chalice of Pthumeru that seals the catacombs that form a web deep below Yharnam.", 4, false},
    {32, "Chalice of Ailing Loran", "Acquire the Chalice of Ailing Loran that seals the tragic land lost to the sands.", 4, false},
    {33, "Chalice of Isz", "Acquire the Great Chalice of Isz that seals the home of the cosmic kin.", 4, false},
    {34, "Old Hunter's Essence", "Acquire all old hunter weapons.", 2, true},
    {35, "Orphan of Kos", "Defeat Great One: Orphan of Kos.", 3, true},
    {36, "Ludwig, the Holy Blade", "Defeat the beast that was once Ludwig, the Holy Blade.", 4, true},
    {37, "Lady Maria of the Astral Clocktower", "Defeat Lady Maria of the Astral Clocktower.", 4, true},
    {38, "Living Failures", "Defeat the failed attempts to become Great Ones.", 4, true},
    {39, "Laurence, the First Vicar", "Defeat the beast that was once Laurence, the First Vicar.", 4, true},
};

struct Popup {
    int id;
    int earned, total;
};
std::mutex g_mu;
std::deque<Popup> g_queue;
bool g_showing = false;
Popup g_current{};
std::chrono::steady_clock::time_point g_since;

constexpr double kIn = 0.45, kHold = 5.5, kOut = 0.5;  // seconds

// The medal's two colours (top, bottom) by grade, as the launcher's.
void medal(int grade, std::uint32_t& top, std::uint32_t& bottom) {
    switch (grade) {
        case 1: top = 0xffffffffu; bottom = 0x8a9bbaffu; break;  // platinum
        case 2: top = 0xf8ecc8ffu; bottom = 0x8a6a36ffu; break;  // gold
        case 3: top = 0xf2f4f7ffu; bottom = 0x6d7584ffu; break;  // silver
        default: top = 0xf1d2b6ffu; bottom = 0x6e4628ffu; break;  // bronze
    }
}

const char* grade_name(int grade) {
    switch (grade) {
        case 1: return "Platinum trophy earned";
        case 2: return "Gold trophy earned";
        case 3: return "Silver trophy earned";
        default: return "Bronze trophy earned";
    }
}

std::uint32_t with_alpha(std::uint32_t rgba, float a) {
    const float base = static_cast<float>(rgba & 0xffu);
    return (rgba & 0xffffff00u) | static_cast<std::uint32_t>(std::lround(std::clamp(base * a, 0.0f, 255.0f)));
}

}  // namespace

const TrophyInfo* trophy_info(int id) {
    for (const TrophyInfo& t : kTrophies)
        if (t.id == id) return &t;
    return nullptr;
}

int trophy_count() { return static_cast<int>(sizeof(kTrophies) / sizeof(kTrophies[0])); }

int trophy_count_of_grade(int grade) {
    int n = 0;
    for (const TrophyInfo& t : kTrophies) n += t.grade == grade;
    return n;
}

void trophy_popup(int id, int earned, int total) {
    const TrophyInfo* t = trophy_info(id);
    host_log("trophy: %s (%d) earned - %d of %d", t ? t->name : "?", id, earned, total);
    if (!host_settings().trophy_popups) return;
    std::lock_guard<std::mutex> lk(g_mu);
    g_queue.push_back({id, earned, total});
}

void trophy_popup_frame(float display_w, float /*display_h*/) {
    // BBHOST_TROPHY_TEST=<id>: that trophy's pop-up once, a few seconds in, to see it.
    static bool tested = false;
    static const auto started = std::chrono::steady_clock::now();
    if (!tested && std::chrono::steady_clock::now() - started > std::chrono::seconds(8)) {
        tested = true;
        if (const char* e = std::getenv("BBHOST_TROPHY_TEST"); e && *e) {
            std::lock_guard<std::mutex> lk(g_mu);
            g_queue.push_back({std::atoi(e), 27, 40});
        }
    }
    Popup p;
    double t = 0.0;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        const auto now = std::chrono::steady_clock::now();
        if (g_showing && std::chrono::duration<double>(now - g_since).count() > kIn + kHold + kOut) g_showing = false;
        if (!g_showing && !g_queue.empty()) {
            g_current = g_queue.front();
            g_queue.pop_front();
            g_showing = true;
            g_since = now;
            haptics_menu_tick(1);  // a firm tap on a DualSense, as a console's notification buzz
        }
        if (!g_showing) return;
        p = g_current;
        t = std::chrono::duration<double>(now - g_since).count();
    }
    const TrophyInfo* info = trophy_info(p.id);
    if (!info) return;
    // Slides in from the right and fades, holds, then fades out.
    const double in = std::clamp(t / kIn, 0.0, 1.0), out = std::clamp((t - kIn - kHold) / kOut, 0.0, 1.0);
    const float ease = static_cast<float>(1.0 - std::pow(1.0 - in, 3.0));
    const float a = static_cast<float>(std::min(in * 1.4, 1.0) * (1.0 - out));
    const float w = 460.0f, h = 92.0f;
    const float x = display_w - w - 28.0f + (1.0f - ease) * 60.0f, y = 70.0f;

    host_overlay_glass(x, y, w, h, 22.0f, 22.0f, 0.72f);
    host_overlay_round_rect(x, y, w, h, 22.0f, with_alpha(0x0c0f1655u, a), with_alpha(0x0c0f1677u, a));
    host_overlay_round_frame(x, y, w, h, 22.0f, 1.0f, with_alpha(0xffffff66u, a), with_alpha(0xffffff1au, a));
    // The medal: a lit disc in the grade's colours, with a darker ring.
    std::uint32_t top, bottom;
    medal(info->grade, top, bottom);
    const float m = 58.0f, mx = x + 18.0f, my = y + (h - m) * 0.5f;
    host_overlay_round_rect(mx - 2.0f, my - 2.0f, m + 4.0f, m + 4.0f, (m + 4.0f) * 0.5f, with_alpha(0x00000099u, a), with_alpha(0x00000099u, a));
    host_overlay_round_rect(mx, my, m, m, m * 0.5f, with_alpha(top, a), with_alpha(bottom, a));
    host_overlay_round_rect(mx + 12.0f, my + 8.0f, m * 0.36f, m * 0.22f, m * 0.11f, with_alpha(0xffffff8cu, a), with_alpha(0xffffff00u, a));
    const float tx = mx + m + 18.0f;
    host_overlay_ui_text(kOverlayFontBold, tx, y + 14.0f, 13.0f, with_alpha(0xc2d3efffu, a), grade_name(info->grade));
    // The name, shortened to the card.
    std::string name = info->name;
    const float room = x + w - 20.0f - tx;
    while (name.size() > 4 && host_overlay_ui_text_width(kOverlayFontTitle, 27.0f, name.c_str()) > room) name = name.substr(0, name.size() - 4) + "...";
    host_overlay_ui_text(kOverlayFontTitle, tx, y + 31.0f, 27.0f, with_alpha(0xf2f5faffu, a), name.c_str());
    char line[64];
    std::snprintf(line, sizeof(line), "%d of %d  -  %d%%", p.earned, p.total, p.total ? p.earned * 100 / p.total : 0);
    host_overlay_ui_text(kOverlayFontBody, tx, y + 64.0f, 14.0f, with_alpha(0xb9c1cfffu, a), line);
}
