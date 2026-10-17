#include "s376/config.hpp"
#include "s376/geo.hpp"

#include <cmath>
#include <iostream>

extern int g_fails;
extern int g_passes;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (cond) {                                                                                \
            ++g_passes;                                                                            \
        } else {                                                                                   \
            ++g_fails;                                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n";          \
        }                                                                                          \
    } while (0)
#define CHECK_NEAR(a, b, eps)                                                                      \
    do {                                                                                           \
        const double _a = static_cast<double>(a);                                                  \
        const double _b = static_cast<double>(b);                                                  \
        if (std::fabs(_a - _b) <= (eps)) {                                                         \
            ++g_passes;                                                                            \
        } else {                                                                                   \
            ++g_fails;                                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #a << "=" << _a         \
                      << " " << #b << "=" << _b << "\n";                                           \
        }                                                                                          \
    } while (0)

void test_channels() {
    auto cfg = s376::default_config();
    auto all = s376::select_channels(cfg, "");
    CHECK(all.size() == 3);
    CHECK(all[0].name == "front" && all[1].name == "left" && all[2].name == "right");
    auto one = s376::select_channels(cfg, "left");
    CHECK(one.size() == 1 && one[0].name == "left");
    CHECK_NEAR(one[0].heading_offset_deg, 35.515, 1e-6);
    auto none = s376::select_channels(cfg, "bogus");
    CHECK(none.empty());
    auto all2 = s376::select_channels(cfg, "all");
    CHECK(all2.size() == 3);

    cfg.channels[1].enabled = false;
    CHECK(s376::select_channels(cfg, "").size() == 2);

    // Single display: tile left | front | right, scaled to fit.
    auto tiled = s376::layout_channels(all, 1, 1920, 1080);
    CHECK(tiled.size() == 3);
    CHECK(tiled[0].name == "left");
    CHECK(tiled[1].name == "front");
    CHECK(tiled[2].name == "right");
    CHECK(tiled[0].x < tiled[1].x && tiled[1].x < tiled[2].x);
    CHECK(tiled[0].w + tiled[1].w + tiled[2].w <= 1920 + 3);
    CHECK(!tiled[0].fullscreen);

    // Three monitors: each channel on its configured screen, native size.
    auto multi = s376::layout_channels(all, 3, 1920, 1080);
    CHECK(multi.size() == 3);
    int screens_used = 0;
    for (const auto& p : multi) {
        if (p.name == "front")
            CHECK(p.screen == 0 && p.w == 1920);
        if (p.name == "left")
            CHECK(p.screen == 1);
        if (p.name == "right")
            CHECK(p.screen == 2);
        screens_used += p.screen;
    }
    CHECK(screens_used == 0 + 1 + 2);

    // Explicit window_x/y wins.
    all[0].window_x = 40;
    all[0].window_y = 50;
    auto ex = s376::layout_channels(all, 1, 1920, 1080);
    bool found = false;
    for (const auto& p : ex) {
        if (p.name == "front") {
            CHECK(p.x == 40 && p.y == 50);
            found = true;
        }
    }
    CHECK(found);

    // Shared eyepoint, left channel looks ~35.5° to the left of front.
    const auto front = s376::geo::make_channel_view(47.46, -122.31, 136.0, 180.34, 0, 0, 0, 0, 0,
                                                    50, 32.5, 1920, 1080);
    const auto left = s376::geo::make_channel_view(47.46, -122.31, 136.0, 180.34, 0, 0, 35.515, 0, 0,
                                                   50, 32.5, 1920, 1080);
    const auto right = s376::geo::make_channel_view(47.46, -122.31, 136.0, 180.34, 0, 0, -35.25, 0, 0,
                                                    50, 32.5, 1920, 1080);
    CHECK_NEAR(front.eye.x, left.eye.x, 1e-6);
    CHECK_NEAR(front.eye.x, right.eye.x, 1e-6);
    CHECK_NEAR(left.heading_deg, 180.34 + 35.515, 1e-6);
    CHECK_NEAR(right.heading_deg, 180.34 - 35.25, 1e-6);
    // Forward vectors should differ (not parallel).
    const double dot = front.forward.x * left.forward.x + front.forward.y * left.forward.y +
                       front.forward.z * left.forward.z;
    CHECK(dot < 0.95);
}
