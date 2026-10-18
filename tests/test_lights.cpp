#include "s376/lights.hpp"
#include "s376/tod.hpp"

#include <cmath>
#include <fstream>
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
                      << #b << "=" << _b << "\n";                                                  \
        }                                                                                          \
    } while (0)

void test_lights() {
    using namespace s376::lights;
    CHECK(runway_base(1) == 0);
    CHECK(runway_base(2) == 8);
    CHECK(runway_base(3) == 16);
    CHECK(runway_base(4) == 24);
    CHECK(runway_base(5) == 32);
    CHECK(runway_base(6) == 84);
    CHECK(runway_base(7) == 92);
    CHECK(runway_switch(1, kOffApproachA) == 1);
    CHECK(runway_switch(1, kOffStrobeA) == 2);
    CHECK(runway_switch(1, kOffEdge) == 3);
    CHECK(runway_switch(1, kOffCenterline) == 4);
    CHECK(runway_switch(1, kOffVasi) == 5);
    CHECK(runway_switch(1, kOffTdz) == 6);
    CHECK(runway_switch(1, kOffApproachB) == 7);
    CHECK(runway_switch(1, kOffStrobeB) == 8);
    CHECK(runway_switch(2, kOffEdge) == 11);
    CHECK(kSwTaxiEdge == 41 && kSwStopBar == 56 && kSwTaxiCenterline == 60);
    CHECK(s376::kSwRunwayEdge == 3);
    CHECK(s376::kSwCenterline == 4);
    CHECK(s376::kSwApproach == 1);
    CHECK(s376::kSwThreshold == 6);

    CHECK_NEAR(intensity(s376::LightKind::Approach, 0), 0.0, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Approach, 1), 0.27, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Threshold, 3), 0.50, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Approach, 5), 1.0, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Strobe, 1), 0.70, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Strobe, 5), 1.0, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Edge, 1), 0.35, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Edge, 4), 0.92, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::LeadOn, 5), 1.0, 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::SignGlow, 3), intensity(s376::LightKind::Edge, 3), 1e-6);
    CHECK_NEAR(intensity(s376::LightKind::Night, 1), intensity(s376::LightKind::Edge, 1), 1e-6);
    CHECK(s376::lights::type_range_scale(s376::LightKind::Beacon) > 0.9f);
    CHECK(s376::lights::type_range_scale(s376::LightKind::Night) < 0.5f);

    CHECK_NEAR(size_mult_hat(0.f), 12.0, 1e-4);
    CHECK_NEAR(size_mult_hat(500.f), 1.0, 1e-4);
    CHECK(size_mult_hat(250.f) > 1.0 && size_mult_hat(250.f) < 12.0);
    CHECK_NEAR(base_size_px(12.f), 2.8, 1e-4);
    CHECK_NEAR(base_size_px(0.f), 2.5, 1e-4);
    CHECK_NEAR(point_size_px(12.f, 0.f), 2.8 * 12.0, 1e-3);
    CHECK(tod_visibility(s376::LightKind::Edge, 14.5f) < 0.08f);
    CHECK(tod_visibility(s376::LightKind::Edge, 14.5f) > 0.02f);
    CHECK(tod_visibility(s376::LightKind::Night, 14.5f) <
          tod_visibility(s376::LightKind::Vasi, 14.5f));
    CHECK(tod_visibility(s376::LightKind::Approach, 14.5f) > 0.50f);
    CHECK(tod_visibility(s376::LightKind::Vasi, 14.5f) > 0.40f);
    CHECK(tod_visibility(s376::LightKind::Threshold, 14.5f) > 0.35f);
    CHECK(papi_box_white(3.0f, 0, 3.0f) && papi_box_white(3.0f, 1, 3.0f));
    CHECK(!papi_box_white(3.0f, 2, 3.0f) && !papi_box_white(3.0f, 3, 3.0f));
    CHECK(papi_box_white(4.0f, 3, 3.0f));
    CHECK(!papi_box_white(2.0f, 0, 3.0f));
    {
        // Eye 1 km south of a PAPI, 52.4 m above it → atan(52.4/1000) ≈ 3°.
        const float ang = papi_eye_angle_deg(40.63, -73.78, 52.4, 40.639, -73.78, 0.0);
        CHECK_NEAR(ang, 3.0, 0.15);
        CHECK(papi_box_white(ang, 0, 3.0f) && papi_box_white(ang, 1, 3.0f));
        CHECK(!papi_box_white(ang, 2, 3.0f) && !papi_box_white(ang, 3, 3.0f));
    }
    CHECK_NEAR(tod_visibility(s376::LightKind::Edge, 0.5f), 1.0, 1e-3);
    CHECK_NEAR(kMaxRangeFt, 250000.0, 1e-3);
    CHECK(max_range_m() > 70000.f);

    CHECK(beacon_on(0.00) && beacon_on(0.09) && !beacon_on(0.11) && !beacon_on(1.0));
    CHECK(beacon_on(1.50) && !beacon_on(1.61));
    CHECK(strobe_on(0.00) && strobe_on(0.06) && !strobe_on(0.07));
    CHECK(sfl_on(0.0, 0) && !sfl_on(0.0, 1));
    CHECK(sfl_on(0.5, 1)); // 2 flashes / s

    auto tod = s376::default_tod_table();
    CHECK(tod.samples.size() >= 4);
    auto noon = s376::sample_tod(tod, 12.f);
    CHECK(noon.stars < 0.05f);
    CHECK(noon.diffuse[0] > 0.8f);
    auto night = s376::sample_tod(tod, 0.5f);
    CHECK(night.stars > 0.8f);
    CHECK(night.ambient[0] < noon.ambient[0]);

    std::string path = "data/env/tod.toml";
    {
        std::ifstream probe(path);
        if (!probe)
            path = "../data/env/tod.toml";
    }
    s376::TodTable file;
    std::string err;
    CHECK(s376::load_tod_table(path, file, err));
    CHECK(file.samples.size() >= 4);
    auto noon2 = s376::sample_tod(file, 12.f);
    CHECK_NEAR(noon2.diffuse[0], 1.0, 0.05);
}
