#include "s376/airport.hpp"
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
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #a << "=" << _a << " "  \
                      << #b << "=" << _b << " eps=" << (eps) << "\n";                              \
        }                                                                                          \
    } while (0)

void test_airport() {
    auto a = s376::ksea_public();
    CHECK(a.icao == "KSEA");
    CHECK(a.runways.size() == 3);
    CHECK(a.runways[0].ident_le == "16L");
    CHECK(a.runways[0].ident_he == "34R");
    CHECK_NEAR(a.runways[0].le_lat, 47.463684, 1e-6);
    CHECK_NEAR(a.runways[0].le_lon, -122.307751, 1e-6);
    CHECK_NEAR(a.runways[0].length_m, 11901.0 * s376::geo::kFtToM, 1.0);
    CHECK_NEAR(a.runways[0].width_m, 150.0 * s376::geo::kFtToM, 0.2);

    // Far end is ~length metres south of 16L.
    double dlat = (a.runways[0].he_lat - a.runways[0].le_lat) * s376::geo::kDeg * s376::geo::kWgsA;
    double dlon = (a.runways[0].he_lon - a.runways[0].le_lon) * s376::geo::kDeg * s376::geo::kWgsA *
                  std::cos(s376::geo::deg2rad(a.runways[0].le_lat));
    const double len = std::sqrt(dlat * dlat + dlon * dlon);
    CHECK_NEAR(len, a.runways[0].length_m, 8.0);
    CHECK(a.runways[0].he_lat < a.runways[0].le_lat);

    CHECK(!a.lights.empty());
    int n_edge = 0, n_cl = 0, n_app = 0, n_th = 0;
    bool near_16l = false;
    for (const auto& L : a.lights) {
        if (L.kind == s376::LightKind::Edge) {
            ++n_edge;
            CHECK(L.switch_num == s376::kSwRunwayEdge);
        } else if (L.kind == s376::LightKind::Centerline) {
            ++n_cl;
            CHECK(L.switch_num == s376::kSwCenterline);
        } else if (L.kind == s376::LightKind::Approach) {
            ++n_app;
            CHECK(L.switch_num == s376::kSwApproach);
        } else if (L.kind == s376::LightKind::Threshold) {
            ++n_th;
            CHECK(L.switch_num == s376::kSwThreshold);
        }
        const double d = std::fabs(L.lat - 47.463684) + std::fabs(L.lon + 122.307751);
        if (d < 0.002)
            near_16l = true;
    }
    CHECK(n_edge > 100);
    CHECK(n_cl > 200);
    CHECK(n_app > 50);
    CHECK(n_th >= 16);
    CHECK(near_16l);

    auto mesh = s376::build_airport_mesh(a);
    CHECK(mesh.verts.size() >= 18); // 3 runways × 2 quads × 2 tris × 3 verts, plus taxi
    CHECK(mesh.verts.size() % 3 == 0);

    const char* kn = s376::light_kind_name(s376::LightKind::Approach);
    CHECK(kn[0] == 'a');
}
