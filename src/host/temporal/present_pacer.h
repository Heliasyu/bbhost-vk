#pragma once

// test11, BBHOST_PRESENT_PACE (on by default at a fixed pace: 30/60/90/120):
// presents go out on an even grid of the pace's period.
//
// t12 (test10, post at render on): the GPU was 84-88% busy at 120 fps, so it
// had ~1 ms to spare. Still, 39% of the frames MangoHud saw were over
// 8.42 ms. MangoHud times vkQueuePresentKHR calls, and a present follows its
// flip by the command processor's work on that frame plus the presenter's
// hand-over: 0.9 ms on average, up to 1.2-2 ms. The game's limiter keeps the
// flips even (its sleeps wake <60 us late at p99), so the spread is in
// between. Holding each present to its slot on a grid takes that spread out.
//
//   - The grid runs at the pace's period. A frame that arrives before its
//     slot waits for it (sleep, then spin; the presenter thread, no lock
//     held, after the blit is submitted).
//   - A frame that arrives after its slot goes out at once, and the grid
//     restarts from it. That makes one longer interval, never a short one
//     after it. The wait is never more than the time to the slot, so pacing
//     cannot make a frame later than one period after the last present.
//   - The grid drifts earlier when every frame of a window arrived well
//     ahead of its slot. That keeps the added latency to about the arrivals'
//     spread (a few hundred us), at most kCreepNs a frame.
//   - rc2: vk1-dream's grid sat 3-4 ms behind the arrivals for seconds after
//     every late frame (20 us a frame, decided every 120 frames: a 4 ms
//     excess took ~200 frames to win back). Now decided every 30 frames and
//     up to 100 us a frame: the same excess is gone in ~40 frames (1/3 s).
//   - Under the pace (GPU-bound), every frame is late, so they pass straight
//     through, as before.
// Pure logic, no clocks (tests/present_pacer_test.cpp).

#include <algorithm>
#include <cstdint>

namespace temporal {

class PresentPacer {
public:
    static constexpr std::int64_t kCreepNs = 100000;     // the grid's earliest move a frame
    static constexpr std::int64_t kKeepNs = 150000;      // the margin the earliest arrival keeps
    static constexpr int kWindow = 30;                   // frames between creep decisions

    void set_period(std::int64_t ns) {
        if (ns != period_) {
            period_ = ns;
            next_ = 0;
        }
    }
    std::int64_t period() const { return period_; }

    // A frame ready to present at `now`: how long to wait (0: at once).
    // Then call presented() with when it went.
    std::int64_t wait_ns(std::int64_t now) {
        if (period_ <= 0) return 0;
        if (!next_ || now >= next_ + period_ || now < next_ - period_) {  // first, or far off the grid
            next_ = now;
            ++reanchors_;
            window_min_early_ = -1;
            return 0;
        }
        if (now >= next_) {  // late for its slot: at once, the grid restarts here
            ++late_;
            next_ = now;
            window_min_early_ = 0;
            return 0;
        }
        const std::int64_t early = next_ - now;
        if (window_min_early_ < 0 || early < window_min_early_) window_min_early_ = early;
        ++waited_;
        wait_total_ += early;
        return early;
    }

    // The frame went at `t` (normally the slot): the next slot is a period on,
    // pulled earlier when the last window's frames were all well ahead.
    void presented(std::int64_t t) {
        if (period_ <= 0) return;
        if (t > next_) next_ = t;
        std::int64_t step = period_;
        if (++in_window_ >= kWindow) {
            if (window_min_early_ > kKeepNs) creep_ = std::min<std::int64_t>((window_min_early_ - kKeepNs) / kWindow + 1, kCreepNs);
            else creep_ = 0;
            in_window_ = 0;
            window_min_early_ = -1;
        }
        if (creep_) {
            step -= creep_;
            creep_total_ += creep_;
        }
        next_ += step;
    }

    std::uint64_t reanchors() const { return reanchors_; }
    std::uint64_t late() const { return late_; }
    std::uint64_t waited() const { return waited_; }
    std::int64_t wait_total_ns() const { return wait_total_; }
    std::int64_t creep_total_ns() const { return creep_total_; }

private:
    std::int64_t period_ = 0;
    std::int64_t next_ = 0;  // the next slot; 0: none yet
    std::int64_t window_min_early_ = -1;
    std::int64_t creep_ = 0;
    int in_window_ = 0;
    std::uint64_t reanchors_ = 0, late_ = 0, waited_ = 0;
    std::int64_t wait_total_ = 0, creep_total_ = 0;
};

}  // namespace temporal
