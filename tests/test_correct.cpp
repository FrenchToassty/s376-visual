#include "s376/accuracy.hpp"
#include "s376/airport.hpp"
#include "s376/correct.hpp"
#include "s376/faa.hpp"
#include "s376/image.hpp"
#include "s376/terrain.hpp"

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

void test_correct() {
    const auto jfk = s376::kjfk_public();
    const auto& faa = s376::faa_kjfk();
    const auto plan = s376::plan_corrections(jfk, faa);
    CHECK(plan.ok());
    CHECK(plan.items.size() >= 10);
    bool saw_surface = false, saw_disp = false, saw_emas = false;
    for (const auto& c : plan.items) {
        if (c.feature.find("runway-surface") != std::string::npos)
            saw_surface = true;
        if (c.feature.find("22R") != std::string::npos &&
            c.feature.find("displaced") != std::string::npos)
            saw_disp = true;
        if (c.feature.find("EMAS") != std::string::npos)
            saw_emas = true;
        CHECK(!c.official.empty());
        CHECK(!c.action.empty());
    }
    CHECK(saw_surface && saw_disp && saw_emas);
    const auto text = s376::format_correction_report(plan);
    CHECK(text.find("CORRECT") != std::string::npos);
    CHECK(text.find("official") != std::string::npos);

    s376::GeoBox box;
    box.west = -73.85;
    box.south = 40.60;
    box.east = -73.72;
    box.north = 40.68;
    const auto img = s376::check_imagery_vs_official(jfk, faa, &box, 4000, 4000);
    CHECK(img.ok());

    s376::Airport drifted = jfk;
    drifted.runways[1].he_lat += 0.01;
    const auto bad = s376::check_imagery_vs_official(drifted, faa, &box, 4000, 4000);
    CHECK(!bad.ok());

    s376::RgbImage im = s376::make_solid_rgb(64, 64, 180, 40, 40); // stale red paint
    const int n = s376::apply_correction_overlay(im, box, jfk);
    CHECK(n >= 4);
    bool saw_asphalt = false;
    for (std::size_t i = 0; i + 2 < im.rgb.size(); i += 3) {
        if (im.rgb[i] < 80 && im.rgb[i + 1] < 80 && im.rgb[i + 2] < 80)
            saw_asphalt = true;
    }
    CHECK(saw_asphalt);

    for (const auto* ref : s376::faa_all()) {
        const auto a = s376::airport_by_icao(ref->icao);
        const auto acc = s376::check_imagery_vs_official(a, *ref);
        if (!acc.ok())
            std::cerr << s376::format_acc_report(acc);
        CHECK(acc.ok());
        const auto cr = s376::plan_corrections(a, *ref);
        CHECK(!cr.items.empty());
    }
}
