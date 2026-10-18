#include "s376/geo.hpp"
#include "s376/landmarks.hpp"

#include <cmath>
#include <iostream>
#include <string>

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
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #a << "=" << _a << " "  \
                      << #b << "=" << _b << " eps=" << (eps) << "\n";                              \
        }                                                                                          \
    } while (0)

void test_landmarks() {
    CHECK(s376::era_unlock_toast(0).empty());
    CHECK(s376::era_unlock_toast(3).empty());
    CHECK(s376::era_unlock_toast(4) ==
          "Click 3 more times to unlock Classic NYC skyline (pre-2001)");
    CHECK(s376::era_unlock_toast(5) ==
          "Click 2 more times to unlock Classic NYC skyline (pre-2001)");
    CHECK(s376::era_unlock_toast(6) ==
          "Click 1 more time to unlock Classic NYC skyline (pre-2001)");
    CHECK(s376::era_unlock_toast(7) == "Classic skyline unlocked");

    s376::EraUnlock u;
    CHECK(!u.click(1000));
    CHECK(u.toast.empty());
    CHECK(!u.click(1100));
    CHECK(u.toast.empty());
    CHECK(!u.click(1200));
    CHECK(u.toast.empty());
    CHECK(!u.click(1300));
    CHECK(u.toast == "Click 3 more times to unlock Classic NYC skyline (pre-2001)");
    CHECK(!u.click(1400));
    CHECK(u.toast == "Click 2 more times to unlock Classic NYC skyline (pre-2001)");
    CHECK(!u.click(1500));
    CHECK(u.toast == "Click 1 more time to unlock Classic NYC skyline (pre-2001)");
    CHECK(u.click(1600));
    CHECK(u.unlocked);
    CHECK(u.toast == "Classic skyline unlocked");

    s376::EraUnlock timeout;
    timeout.click(0);
    timeout.click(100);
    timeout.click(200);
    timeout.click(300);
    CHECK(timeout.clicks == 4);
    timeout.click(300 + s376::EraUnlock::kWindowMs + 50);
    CHECK(timeout.clicks == 1);
    CHECK(!timeout.unlocked);

    CHECK(s376::instructor_hit(20, 20, false) == s376::InstructorHit::Version);
    CHECK(s376::instructor_hit(20, 50, false) == s376::InstructorHit::None);
    CHECK(s376::instructor_hit(20, 50, true) == s376::InstructorHit::EraCurrent);
    CHECK(s376::instructor_hit(200, 50, true) == s376::InstructorHit::EraClassic);

    CHECK(s376::in_nyc_geofence(40.71, -74.01));
    CHECK(s376::in_nyc_geofence(40.64, -73.78)); // JFK
    CHECK(!s376::in_nyc_geofence(47.45, -122.31));

    CHECK(s376::find_landmark("wtc1-1973"));
    CHECK(s376::find_landmark("wtc2-1973"));
    CHECK(s376::find_landmark("onewtc"));
    CHECK(s376::find_landmark("432park"));
    CHECK(s376::find_landmark("hudson-30"));

    s376::SolidBvh classic;
    s376::fill_solids(classic, s376::SkylineEra::Classic, {});
    CHECK(classic.volume_count() >= 7);
    double roof = 0;
    // Offset from the antenna so the query is the roof, not the tip.
    CHECK(classic.roof_msl(40.7117, -74.0131, &roof));
    CHECK(roof > 350.0 && roof < 480.0);
    const double roof_ft = roof * s376::geo::kMToFt;
    auto hs = s376::sample_hat_solids(nullptr, &classic, 40.7117, -74.0131, roof_ft + 80.0);
    CHECK(hs.building);
    CHECK_NEAR(hs.elev_ft, roof_ft, 25.0);
    CHECK_NEAR(hs.hat_ft, 80.0, 25.0);

    s376::SolidBvh current;
    s376::fill_solids(current, s376::SkylineEra::Current, {});
    double cur_roof = 0;
    const bool on_north = current.roof_msl(40.7117, -74.0131, &cur_roof);
    if (on_north)
        CHECK(cur_roof < 200.0);
    auto hs_now = s376::sample_hat_solids(nullptr, &current, 40.7117, -74.0131, roof_ft + 80.0);
    CHECK(!hs_now.building || hs_now.elev_ft < 800.0);
    CHECK(hs_now.elev_ft < roof_ft - 200.0);

    const auto hit = classic.segment(40.7117, -74.0148, 200.0, 40.7117, -74.0120, 200.0);
    CHECK(hit.hit);
    const auto miss = current.segment(40.7117, -74.0148, 200.0, 40.7117, -74.0120, 200.0);
    CHECK(!miss.hit);

    const auto mesh = s376::mesh_landmarks(s376::SkylineEra::Classic);
    CHECK(mesh.verts.size() >= 24);
    const auto lamps = s376::landmark_warning_lights(s376::SkylineEra::Classic);
    CHECK(lamps.size() >= 8);

    CHECK(s376::visible_skyline_era(s376::SkylineEra::Classic, 40.71, -74.01) ==
          s376::SkylineEra::Classic);
    CHECK(s376::visible_skyline_era(s376::SkylineEra::Classic, 47.45, -122.31) ==
          s376::SkylineEra::Current);
    CHECK(s376::visible_skyline_era(s376::SkylineEra::Current, 40.71, -74.01) ==
          s376::SkylineEra::Current);

    // Session stays Classic across slew / leave-NYC. Only refresh/reset clears it.
    s376::refresh_channels();
    CHECK(s376::session_era() == s376::SkylineEra::Current);
    CHECK(!s376::session_era_unlocked());
    s376::tick_session_view(40.71, -74.01);
    s376::set_session_era(s376::SkylineEra::Classic, true);
    CHECK(s376::session_era() == s376::SkylineEra::Classic);
    CHECK(s376::active_skyline_era() == s376::SkylineEra::Classic);

    s376::tick_session_view(47.45, -122.31); // reposition to KSEA
    CHECK(s376::session_era() == s376::SkylineEra::Classic);
    CHECK(s376::session_era_unlocked());
    CHECK(s376::active_skyline_era() == s376::SkylineEra::Current);
    {
        double z = 0;
        CHECK(!s376::session_solids().roof_msl(40.7117, -74.0131, &z));
    }

    // Leave the geofence without a far-airport slew; setting still holds.
    s376::tick_session_view(40.20, -74.01);
    CHECK(s376::session_era() == s376::SkylineEra::Classic);
    CHECK(s376::session_era_unlocked());
    CHECK(s376::active_skyline_era() == s376::SkylineEra::Current);

    s376::tick_session_view(40.71, -74.01); // back to NYC, no reset
    CHECK(s376::session_era() == s376::SkylineEra::Classic);
    CHECK(s376::session_era_unlocked());
    CHECK(s376::active_skyline_era() == s376::SkylineEra::Classic);
    {
        double z = 0;
        CHECK(s376::session_solids().roof_msl(40.7117, -74.0131, &z));
        CHECK(z > 350.0);
    }

    s376::refresh_channels();
    CHECK(s376::session_era() == s376::SkylineEra::Current);
    CHECK(s376::active_skyline_era() == s376::SkylineEra::Current);
    CHECK(!s376::session_era_unlocked());
}
