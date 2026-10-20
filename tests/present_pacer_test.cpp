#include "host/temporal/present_pacer.h"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using temporal::PresentPacer;

static int failures = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
            ++failures;                                                  \
        }                                                                \
    } while (0)

// Frames arriving at flip k*P + jitter; returns the present times.
static std::vector<std::int64_t> run(PresentPacer& p, int n, std::int64_t P, std::int64_t base_lat, std::int64_t jitter,
                                     unsigned seed, int late_at = -1, std::int64_t late_by = 0) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::int64_t> j(0, jitter);
    std::vector<std::int64_t> out;
    std::int64_t last = 0;
    for (int k = 0; k < n; ++k) {
        std::int64_t arrive = 1000000000 + k * P + base_lat + j(rng) + (k == late_at ? late_by : 0);
        if (arrive < last) arrive = last;  // one presenter thread
        const std::int64_t w = p.wait_ns(arrive);
        CHECK(w >= 0 && w <= P);
        const std::int64_t t = arrive + w;
        p.presented(t);
        out.push_back(t);
        last = t;
    }
    return out;
}

int main() {
    const std::int64_t P = 8333333;
    {  // jittery arrivals (0.9 ms + up to 0.6 ms) come out even after the first window
        PresentPacer p;
        p.set_period(P);
        auto t = run(p, 3000, P, 900000, 600000, 1);
        int off = 0;
        for (std::size_t k = 300; k < t.size(); ++k) off += std::llabs((t[k] - t[k - 1]) - P) > 25000;
        std::printf("even: %d of %zu intervals off by >25 us; late %llu, reanchors %llu, avg wait %.0f us\n", off, t.size() - 300,
                    static_cast<unsigned long long>(p.late()), static_cast<unsigned long long>(p.reanchors()),
                    p.waited() ? static_cast<double>(p.wait_total_ns()) / 1e3 / static_cast<double>(p.waited()) : 0.0);
        CHECK(off < 60);  // a few late frames while the grid settles
        CHECK(p.wait_total_ns() / static_cast<std::int64_t>(p.waited() ? p.waited() : 1) < 600000);
    }
    {  // a late frame: one long interval, no short one after it
        PresentPacer p;
        p.set_period(P);
        auto t = run(p, 1000, P, 900000, 0, 2, 500, 3000000);
        CHECK(t[500] - t[499] > P);
        for (std::size_t k = 501; k < 520; ++k) CHECK(t[k] - t[k - 1] >= P - PresentPacer::kCreepNs - 1);
    }
    {  // under the pace: every frame passes straight through (no wait)
        PresentPacer p;
        p.set_period(P);
        std::int64_t now = 1000000000, waits = 0;
        for (int k = 0; k < 1000; ++k) {
            now += P + 300000;
            waits += p.wait_ns(now);
            p.presented(now);
        }
        CHECK(waits == 0);
    }
    {  // off (no period): never waits
        PresentPacer p;
        CHECK(p.wait_ns(5) == 0);
    }
    {  // an even, early stream: the grid creeps toward the arrivals, then holds
        PresentPacer p;
        p.set_period(P);
        // the first frame anchors late (2 ms), the rest arrive at 0.9 ms
        std::int64_t t0 = 1000000000;
        p.wait_ns(t0 + 2000000);
        p.presented(t0 + 2000000);
        std::int64_t last_wait = 0;
        for (int k = 1; k < 6000; ++k) {
            const std::int64_t a = t0 + k * P + 900000;
            last_wait = p.wait_ns(a);
            p.presented(a + last_wait);
        }
        std::printf("creep: wait at the end %.0f us\n", static_cast<double>(last_wait) / 1e3);
        CHECK(last_wait <= PresentPacer::kKeepNs + PresentPacer::kCreepNs);
    }
    {  // rc2: after a late frame re-anchors the grid 4 ms behind the arrivals, the wait is back under 0.3 ms within 90 frames (3/4 s)
        PresentPacer p;
        p.set_period(P);
        std::int64_t t0 = 1000000000;
        p.wait_ns(t0 + 4900000);
        p.presented(t0 + 4900000);
        std::int64_t w60 = 0;
        for (int k = 1; k <= 90; ++k) {
            const std::int64_t a = t0 + k * P + 900000;
            const std::int64_t w = p.wait_ns(a);
            p.presented(a + w);
            if (k == 90) w60 = w;
        }
        std::printf("settle: wait 90 frames after a 4 ms re-anchor %.0f us\n", static_cast<double>(w60) / 1e3);
        CHECK(w60 < 300000);
    }
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("present_pacer_test: ok\n");
    return 0;
}
