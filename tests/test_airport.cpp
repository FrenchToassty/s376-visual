#include "s376/airport.hpp"
#include "s376/geo.hpp"

#include <cmath>
#include <exception>
#include <filesystem>
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
                      << #b << "=" << _b << " eps=" << (eps) << "\n";                              \
        }                                                                                          \
    } while (0)

namespace {

void check_runway_length(const s376::Runway& rw) {
    double dlat = (rw.he_lat - rw.le_lat) * s376::geo::kDeg * s376::geo::kWgsA;
    double dlon = (rw.he_lon - rw.le_lon) * s376::geo::kDeg * s376::geo::kWgsA *
                  std::cos(s376::geo::deg2rad(rw.le_lat));
    const double len = std::sqrt(dlat * dlat + dlon * dlon);
    CHECK_NEAR(len, rw.length_m, 12.0);
}

void check_lights(const s376::Airport& a, int min_edge, int min_cl) {
    CHECK(!a.lights.empty());
    int n_edge = 0, n_cl = 0, n_app = 0, n_th = 0;
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
    }
    CHECK(n_edge >= min_edge);
    CHECK(n_cl >= min_cl);
    CHECK(n_app > 50);
    CHECK(n_th >= 16);
}

void check_mesh(const s376::Airport& a) {
    auto mesh = s376::build_airport_mesh(a);
    CHECK(mesh.verts.size() >= 18);
    CHECK(mesh.verts.size() % 3 == 0);
}

} // namespace

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
    CHECK(a.runways[0].approach_le == "ALSF-2");
    CHECK(!a.aprons.empty());

    check_runway_length(a.runways[0]);
    CHECK(a.runways[0].he_lat < a.runways[0].le_lat);

    bool near_16l = false;
    for (const auto& L : a.lights) {
        const double d = std::fabs(L.lat - 47.463684) + std::fabs(L.lon + 122.307751);
        if (d < 0.002)
            near_16l = true;
    }
    CHECK(near_16l);
    check_lights(a, 100, 200);
    check_mesh(a);

    const char* kn = s376::light_kind_name(s376::LightKind::Approach);
    CHECK(kn[0] == 'a');

    auto sfo = s376::ksfo_public();
    CHECK(sfo.icao == "KSFO");
    CHECK(sfo.runways.size() == 4);
    CHECK(sfo.runways[0].ident_le == "10L");
    CHECK(sfo.runways[0].ident_he == "28R");
    CHECK(sfo.runways[0].approach_he == "ALSF-2");
    CHECK(sfo.runways[0].approach_le == "MALSR");
    CHECK(sfo.runways[1].ident_le == "10R");
    CHECK(sfo.runways[2].ident_le == "01L");
    CHECK(sfo.runways[3].ident_le == "01R");
    CHECK_NEAR(sfo.arp_lat, 37.6188, 1e-4);
    CHECK_NEAR(sfo.runways[0].length_m, 11870.0 * s376::geo::kFtToM, 1.0);
    CHECK_NEAR(sfo.runways[0].width_m, 200.0 * s376::geo::kFtToM, 0.2);
    CHECK(sfo.taxiways.size() >= 3);
    CHECK(!sfo.aprons.empty());
    check_runway_length(sfo.runways[0]);
    check_runway_length(sfo.runways[2]);
    check_lights(sfo, 200, 400);
    check_mesh(sfo);

    auto las = s376::klas_public();
    CHECK(las.icao == "KLAS");
    CHECK(las.runways.size() == 4);
    CHECK(las.runways[0].ident_le == "08L");
    CHECK(las.runways[0].ident_he == "26R");
    CHECK(las.runways[0].approach_he == "ALSF-2");
    CHECK(las.runways[1].ident_le == "08R");
    CHECK(las.runways[1].approach_he == "ALSF-2");
    CHECK(las.runways[2].ident_le == "01L");
    CHECK(las.runways[3].ident_le == "01R");
    CHECK_NEAR(las.arp_lat, 36.0801, 1e-4);
    CHECK_NEAR(las.elev_m, 2181.0 * s376::geo::kFtToM, 0.5);
    CHECK_NEAR(las.runways[0].length_m, 14511.0 * s376::geo::kFtToM, 1.0);
    CHECK(las.taxiways.size() >= 3);
    CHECK(!las.aprons.empty());
    check_runway_length(las.runways[0]);
    check_lights(las, 200, 400);
    check_mesh(las);

    CHECK(s376::airport_by_icao("ksfo").icao == "KSFO");
    CHECK(s376::airport_by_icao("KLAS").runways.size() == 4);

    auto lax = s376::klax_public();
    CHECK(lax.icao == "KLAX");
    CHECK(lax.runways.size() == 4);
    CHECK(lax.runways[0].ident_le == "06L" && lax.runways[0].ident_he == "24R");
    CHECK(lax.runways[0].approach_he == "ALSF-2");
    CHECK(lax.runways[3].ident_le == "07R");
    CHECK_NEAR(lax.arp_lat, 33.9425, 1e-4);
    CHECK_NEAR(lax.runways[2].length_m, 12923.0 * s376::geo::kFtToM, 1.0);
    CHECK(lax.taxiways.size() >= 3);
    check_runway_length(lax.runways[0]);
    check_lights(lax, 200, 400);
    check_mesh(lax);

    auto jfk = s376::kjfk_public();
    CHECK(jfk.icao == "KJFK");
    CHECK(jfk.runways.size() == 4);
    CHECK(jfk.runways[0].ident_le == "04L" && jfk.runways[0].ident_he == "22R");
    CHECK(jfk.runways[0].approach_le == "ALSF-2");
    CHECK(jfk.runways[0].approach_he == "ALSF-2");
    CHECK(jfk.runways[3].ident_le == "13R");
    CHECK_NEAR(jfk.arp_lat, 40.6399, 1e-4);
    CHECK_NEAR(jfk.runways[3].length_m, 14511.0 * s376::geo::kFtToM, 1.0);
    check_runway_length(jfk.runways[0]);
    check_runway_length(jfk.runways[3]);
    check_lights(jfk, 200, 400);
    check_mesh(jfk);

    bool threw = false;
    try {
        (void)s376::airport_by_icao("XXXX");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);

    const auto all = s376::all_public_airports();
    CHECK(all.size() == 5);
    CHECK(all[0].icao == "KSEA" && all[1].icao == "KSFO" && all[2].icao == "KLAS");
    CHECK(all[3].icao == "KLAX" && all[4].icao == "KJFK");

    const auto url = s376::overpass_aeroway_url(37.60, -122.40, 37.64, -122.35);
    CHECK(url.find("overpass-api.de") != std::string::npos);
    CHECK(url.find("taxiway") != std::string::npos);
    CHECK(url.find("apron") != std::string::npos);

    s376::Airport osm = s376::ksfo_public();
    const std::size_t taxis0 = osm.taxiways.size();
    const std::size_t aprons0 = osm.aprons.size();
    const char* sample =
        "{\"elements\":["
        "{\"type\":\"way\",\"tags\":{\"aeroway\":\"taxiway\"},\"geometry\":["
        "{\"lat\":37.620,\"lon\":-122.380},{\"lat\":37.621,\"lon\":-122.379}]},"
        "{\"type\":\"way\",\"tags\":{\"aeroway\":\"apron\"},\"geometry\":["
        "{\"lat\":37.616,\"lon\":-122.386},{\"lat\":37.617,\"lon\":-122.386},"
        "{\"lat\":37.617,\"lon\":-122.385},{\"lat\":37.616,\"lon\":-122.385}]}"
        "]}";
    CHECK(s376::merge_osm_json(osm, sample) == 2);
    CHECK(osm.taxiways.size() == taxis0 + 1);
    CHECK(osm.aprons.size() == aprons0 + 1);
    CHECK(osm.taxiways.back().path.size() == 2);
    CHECK(osm.aprons.back().ring.size() == 4);
    auto osm_mesh = s376::build_airport_mesh(osm);
    CHECK(osm_mesh.verts.size() > s376::build_airport_mesh(s376::ksfo_public()).verts.size());

    const auto tmp = (std::filesystem::temp_directory_path() / "s376-osm-test").string();
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    {
        std::ofstream out(tmp + "/osm.json");
        out << sample;
    }
    s376::Airport from_file;
    from_file.icao = "KSFO";
    CHECK(s376::try_merge_osm_file(from_file, tmp + "/osm.json") == 2);
    CHECK(s376::try_merge_osm_file(from_file, tmp + "/missing.json") == 0);
    CHECK(s376::try_load_osm(from_file, tmp) >= 0);

    const auto gj = s376::airport_geojson(s376::ksea_public());
    CHECK(gj.find("FeatureCollection") != std::string::npos);
    CHECK(gj.find("16L/34R") != std::string::npos);
    CHECK(gj.find("\"aeroway\":\"taxiway\"") != std::string::npos);
    CHECK(s376::write_airport_geojson(s376::ksea_public(), tmp + "/osm.geojson") == 1);
    CHECK(std::filesystem::exists(tmp + "/osm.geojson"));
    std::filesystem::remove_all(tmp);
}
