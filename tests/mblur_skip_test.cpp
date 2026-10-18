// test8: the motion-blur skip's policy (src/host/temporal/mblur_skip.h).
#include "host/temporal/mblur_skip.h"

#include <cstdio>

using temporal::MotionBlurSkip;
using A = MotionBlurSkip::Action;

static int failures = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++failures;                                                  \
        }                                                                \
    } while (0)

constexpr std::uint64_t kA = 0x1000, kB = 0x2000, kC = 0x3000;

// One normal frame: A -> B, B -> A. Returns what the two draws were told.
static void frame(MotionBlurSkip& p, std::uint64_t f, A* r1 = nullptr, A* r2 = nullptr) {
    const A x = p.draw(f, kA, kB, false, true);
    const A y = p.draw(f, kB, kA, false, true);
    if (r1) *r1 = x;
    if (r2) *r2 = y;
}

int main() {
    {  // Learns, then skips both; never before kLearnFrames good frames.
        MotionBlurSkip p;
        std::uint64_t f = 1;
        for (; f <= MotionBlurSkip::kLearnFrames; ++f) {
            A x, y;
            frame(p, f, &x, &y);
            CHECK(x == A::Draw && y == A::Draw);
        }
        A x, y;
        frame(p, f++, &x, &y);  // judged the 120th on entering this one
        CHECK(p.armed());
        CHECK(x == A::Skip && y == A::Skip);
        CHECK(p.pairs_skipped() == 1);
        frame(p, f++, &x, &y);
        CHECK(x == A::Skip && y == A::Skip && p.trips() == 0);
    }
    {  // Other work between the two draws: draw 1 happens after all, disarmed.
        MotionBlurSkip p;
        std::uint64_t f = 1;
        for (; f <= MotionBlurSkip::kLearnFrames + 1; ++f) frame(p, f);
        CHECK(p.armed());
        CHECK(p.draw(f, kA, kB, false, true) == A::Skip);
        CHECK(p.other_work());  // the caller copies A into B
        CHECK(!p.armed() && p.materialized() == 1 && p.trips() == 1);
        CHECK(p.draw(f, kB, kA, false, true) == A::Draw);
        CHECK(!p.other_work());
    }
    {  // A second draw that does not match: materialize, then draw it.
        MotionBlurSkip p;
        std::uint64_t f = 1;
        for (; f <= MotionBlurSkip::kLearnFrames + 1; ++f) frame(p, f);
        CHECK(p.draw(f, kA, kB, false, true) == A::Skip);
        CHECK(p.draw(f, kB, kC, false, true) == A::MaterializeThenDraw);
        CHECK(!p.armed());
    }
    {  // B used elsewhere: while learning the frame does not count; armed, it trips.
        MotionBlurSkip p;
        std::uint64_t f = 1;
        for (; f <= 60; ++f) frame(p, f);
        p.b_touched(kB);
        for (; f <= MotionBlurSkip::kLearnFrames + 1; ++f) frame(p, f);
        CHECK(!p.armed());  // the touched frame reset the count
        for (int k = 0; k < 70; ++k) frame(p, f++);
        CHECK(p.armed());
        p.b_touched(kA);  // A is not watched
        CHECK(p.armed());
        p.b_touched(kB);
        CHECK(!p.armed() && p.trips() == 1);
    }
    {  // Blending, ineligible (setting on), a third draw, one draw: never armed.
        for (int mode = 0; mode < 4; ++mode) {
            MotionBlurSkip p;
            for (std::uint64_t f = 1; f <= 3 * MotionBlurSkip::kLearnFrames; ++f) {
                if (mode == 0) {
                    p.draw(f, kA, kB, true, true);
                    p.draw(f, kB, kA, true, true);
                } else if (mode == 1) {
                    p.draw(f, kA, kB, false, false);
                    p.draw(f, kB, kA, false, false);
                } else if (mode == 2) {
                    frame(p, f);
                    p.draw(f, kA, kB, false, true);
                } else {
                    p.draw(f, kA, kB, false, true);
                }
            }
            CHECK(!p.armed() && p.pairs_skipped() == 0);
        }
    }
    {  // The setting turned on while armed: the draws come ineligible and run.
        MotionBlurSkip p;
        std::uint64_t f = 1;
        for (; f <= MotionBlurSkip::kLearnFrames + 1; ++f) frame(p, f);
        CHECK(p.armed());
        CHECK(p.draw(f, kA, kB, false, false) == A::Draw);
        CHECK(p.draw(f, kB, kA, false, false) == A::Draw);
        CHECK(!p.armed());
    }
    {  // New targets (a resolution change): learns them from scratch.
        MotionBlurSkip p;
        std::uint64_t f = 1;
        for (; f <= MotionBlurSkip::kLearnFrames + 1; ++f) frame(p, f);
        CHECK(p.armed());
        CHECK(p.draw(f, kC, kB, false, true) == A::Draw);
        p.draw(f, kB, kC, false, true);
        ++f;
        CHECK(!p.armed());
        for (int k = 0; k < MotionBlurSkip::kLearnFrames + 1; ++k, ++f) {
            p.draw(f, kC, kB, false, true);
            p.draw(f, kB, kC, false, true);
        }
        CHECK(p.armed() && p.a() == kC);
    }
    {  // A frame that ends with draw 1 held (draw 2 never came): judged broken.
        MotionBlurSkip p;
        std::uint64_t f = 1;
        for (; f <= MotionBlurSkip::kLearnFrames + 1; ++f) frame(p, f);
        CHECK(p.draw(f, kA, kB, false, true) == A::Skip);
        CHECK(p.pending());
        // Normally the UI's draws come next (other_work). Here the next frame's
        // first draw: draw 1 is done first.
        CHECK(p.draw(f + 1, kA, kB, false, true) == A::MaterializeThenDraw);
        CHECK(!p.armed() && p.materialized() == 1);
    }
    if (failures) {
        std::fprintf(stderr, "mblur_skip_test: %d failures\n", failures);
        return 1;
    }
    std::printf("mblur_skip_test: ok\n");
    return 0;
}
