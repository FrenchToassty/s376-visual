#include "s376/airport.hpp"
#include "s376/fetch.hpp"
#include "s376/geo.hpp"
#include "s376/lights.hpp"

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
    int n_edge = 0, n_cl = 0, n_app = 0, n_end = 0;
    for (const auto& L : a.lights) {
        if (L.kind == s376::LightKind::Edge) {
            ++n_edge;
            CHECK(s376::lights::is_runway_switch(L.switch_num, s376::lights::kOffEdge));
        } else if (L.kind == s376::LightKind::Centerline) {
            ++n_cl;
            CHECK(s376::lights::is_runway_switch(L.switch_num, s376::lights::kOffCenterline));
        } else if (L.kind == s376::LightKind::Approach) {
            ++n_app;
            CHECK(s376::lights::is_runway_switch(L.switch_num, s376::lights::kOffApproachA) ||
                  s376::lights::is_runway_switch(L.switch_num, s376::lights::kOffApproachB));
        } else if (L.kind == s376::LightKind::End || L.kind == s376::LightKind::Threshold) {
            ++n_end;
        }
    }
    CHECK(n_edge >= min_edge);
    CHECK(n_cl >= min_cl);
    CHECK(n_app > 50);
    CHECK(n_end >= 8);
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
    CHECK(jfk.runways[0].approach_le == "NONE");
    CHECK(jfk.runways[0].approach_he == "NONE");
    CHECK(jfk.runways[1].approach_le == "ALSF-2");
    CHECK(jfk.runways[1].approach_he == "ALSF-2");
    CHECK(jfk.runways[2].ident_le == "13L" && jfk.runways[2].ident_he == "31R");
    CHECK(jfk.runways[2].approach_le == "ALSF-2");
    CHECK(jfk.runways[2].approach_he == "MALSR");
    CHECK_NEAR(jfk.runways[2].width_m, 200.0 * s376::geo::kFtToM, 0.2);
    CHECK(jfk.runways[3].ident_le == "13R");
    CHECK(jfk.runways[3].approach_le == "RLLS");
    CHECK(jfk.runways[3].approach_he == "NONE");
    CHECK_NEAR(jfk.runways[0].he_displaced_ft, 3424.0, 0.5);
    CHECK_NEAR(jfk.runways[3].le_displaced_ft, 2044.0, 0.5);
    CHECK_NEAR(jfk.runways[3].he_displaced_ft, 3264.0, 0.5);
    CHECK(jfk.runways[1].emas_le && jfk.runways[1].emas_he);
    CHECK(jfk.runways[2].papi_he == "P4R");
    CHECK(jfk.runways[3].papi_count_le == 2);
    CHECK_NEAR(jfk.arp_lat, 40.639928, 1e-5);
    CHECK_NEAR(jfk.runways[3].length_m, 14511.0 * s376::geo::kFtToM, 8.0);
    CHECK(!jfk.source.name.empty());
    CHECK(jfk.source.effective == "2026-10-02");
    bool saw_c1 = false, saw_u1 = false, saw_cc = false, saw_t5 = false;
    for (const auto& t : jfk.taxiways) {
        if (t.name == "C1")
            saw_c1 = true;
        if (t.name == "U1")
            saw_u1 = true;
        if (t.name == "CC")
            saw_cc = true;
    }
    for (const auto& b : jfk.buildings)
        if (b.name == "T5")
            saw_t5 = true;
    CHECK(saw_c1 && saw_u1 && !saw_cc && saw_t5);
    CHECK(jfk.has_beacon && jfk.tower_h_m > 10.f);
    CHECK(jfk.routes.size() >= 12);
    CHECK(jfk.night.spacing_m == 15);
    check_runway_length(jfk.runways[0]);
    check_runway_length(jfk.runways[3]);
    check_lights(jfk, 200, 400);
    check_mesh(jfk);

    auto ewr = s376::kewr_public();
    CHECK(ewr.icao == "KEWR");
    CHECK(ewr.runways.size() == 3);
    CHECK(ewr.runways[0].ident_le == "04L" && ewr.runways[0].ident_he == "22R");
    CHECK(ewr.runways[0].approach_le == "MALSR" && ewr.runways[0].approach_he == "MALSR");
    CHECK(ewr.runways[1].approach_le == "ALSF-2" && ewr.runways[1].approach_he == "ALSF-2");
    CHECK(ewr.runways[2].ident_le == "11" && ewr.runways[2].ident_he == "29");
    CHECK(ewr.runways[2].reil_le && ewr.runways[2].reil_he);
    CHECK(ewr.runways[2].emas_le && ewr.runways[2].emas_he);
    CHECK_NEAR(ewr.runways[0].le_displaced_ft, 2540.6, 0.2);
    CHECK_NEAR(ewr.runways[0].he_displaced_ft, 1440.9, 0.2);
    CHECK_NEAR(ewr.runways[1].papi_deg_le, 2.95, 0.01);
    CHECK(s376::airport_by_icao("kewr").icao == "KEWR");
    check_runway_length(ewr.runways[0]);
    check_lights(ewr, 150, 300);
    check_mesh(ewr);

    bool threw = false;
    try {
        (void)s376::airport_by_icao("XXXX");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);

    auto bos = s376::kbos_public();
    CHECK(bos.icao == "KBOS");
    CHECK(bos.runways.size() == 5);
    CHECK(bos.runways[1].ident_le == "04R" && bos.runways[1].ident_he == "22L");
    CHECK(bos.runways[1].approach_le == "ALSF-2");
    CHECK(bos.runways[3].ident_le == "15R" && bos.runways[3].approach_le == "ALSF-2");
    CHECK_NEAR(bos.arp_lat, 42.3656, 1e-4);
    CHECK_NEAR(bos.runways[1].length_m, 10006.0 * s376::geo::kFtToM, 1.0);
    CHECK(bos.buildings.size() >= 2);
    check_runway_length(bos.runways[1]);
    check_lights(bos, 200, 400);
    check_mesh(bos);
    CHECK(s376::airport_by_icao("kbos").icao == "KBOS");

    const auto all = s376::all_public_airports();
    CHECK(all.size() == 7);
    CHECK(all[0].icao == "KSEA" && all[1].icao == "KSFO" && all[2].icao == "KLAS");
    CHECK(all[3].icao == "KLAX" && all[4].icao == "KJFK" && all[5].icao == "KEWR");
    CHECK(all[6].icao == "KBOS");

    {
        auto p0 = s376::parse_shot_at("KJFK:04R");
        CHECK(p0.icao == "KJFK" && p0.rwy == "04R");
        CHECK_NEAR(p0.dist_nm, 0.0, 1e-9);
        CHECK_NEAR(p0.hdg_true_deg, 31.0, 0.6);
        CHECK_NEAR(p0.pitch_deg, 0.0, 1e-9);
        auto p3 = s376::parse_shot_at("kjfk:04r:3");
        CHECK_NEAR(p3.dist_nm, 3.0, 1e-9);
        CHECK_NEAR(p3.pitch_deg, -3.0, 1e-6);
        CHECK(p3.alt_ft > p0.alt_ft + 800.0);
        auto lined = s376::parse_shot_at("KBOS:04R:0");
        CHECK(lined.icao == "KBOS" && lined.rwy == "04R");
        CHECK_NEAR(lined.hdg_true_deg, 35.0, 0.6);
        auto icao = s376::parse_shot_at("KJFK");
        CHECK(icao.icao == "KJFK" && icao.rwy.empty());
        CHECK_NEAR(icao.lat, 40.6399, 1e-3);
        CHECK_NEAR(icao.lon, -73.7787, 1e-3);
        auto bank = s376::parse_shot_at("KJFK:04R:8");
        s376::ShotOverrides o;
        o.have_alt = true;
        o.alt_ft = 3000;
        o.have_bank = true;
        o.bank_deg = 30;
        s376::apply_shot_overrides(bank, o);
        CHECK_NEAR(bank.alt_ft, 3000.0, 1e-6);
        CHECK_NEAR(bank.roll_deg, 30.0, 1e-6);
        CHECK_NEAR(bank.dist_nm, 8.0, 1e-9);
        CHECK_NEAR(bank.pitch_deg, -3.0, 1e-6);
        auto high = s376::parse_shot_at("KJFK");
        s376::ShotOverrides h;
        h.have_alt = true;
        h.alt_ft = 12000;
        h.have_hdg = true;
        h.hdg_deg = 36;
        s376::apply_shot_overrides(high, h);
        CHECK_NEAR(high.alt_ft, 12000.0, 1e-6);
        CHECK_NEAR(high.hdg_true_deg, 36.0, 1e-6);
        CHECK(high.lat < 40.58); // south of Rockaway, ocean in the foreground
        CHECK(high.pitch_deg <= 0.0 && high.pitch_deg > -3.0); // nearly level, lots of sky
        CHECK(jfk.buildings.size() >= 6);
        bool bad = false;
        try {
            (void)s376::parse_shot_at("XXXX");
        } catch (const std::exception&) {
            bad = true;
        }
        CHECK(bad);
    }

    {
        const auto marked = s376::build_airport_mesh(jfk, true, true);
        const auto bare = s376::build_airport_mesh(jfk, false, false);
        CHECK(marked.verts.size() > bare.verts.size());
        CHECK(marked.verts.size() % 3 == 0);
        float saw_mark = 0;
        for (const auto& v : marked.verts)
            saw_mark = std::max(saw_mark, v.mat);
        CHECK(saw_mark >= 1.f);
    }

    const auto url = s376::overpass_aeroway_url(37.60, -122.40, 37.64, -122.35);
    CHECK(url.find("overpass-api.de") != std::string::npos);
    CHECK(url.find("taxiway") != std::string::npos);
    CHECK(url.find("apron") != std::string::npos);
    CHECK(s376::curl_glob_safe(url));
    CHECK(url.find('[') == std::string::npos);
    CHECK(url.find(']') == std::string::npos);
    CHECK(url.find("%5B") != std::string::npos);
    const auto q = s376::overpass_aeroway_query(37.60, -122.40, 37.64, -122.35);
    CHECK(q.find("[out:json]") != std::string::npos);
    CHECK(q.find("building") != std::string::npos);
    CHECK(q.find("natural") != std::string::npos);
    CHECK(q.find("water") != std::string::npos);
    CHECK(!s376::curl_glob_safe(q));
    const auto cmd = s376::http_get_command(url, "/tmp/s376-osm-test.json");
    CHECK(cmd.find(" -g ") != std::string::npos);
    CHECK(cmd.find("--retry") != std::string::npos);
    const auto post = s376::http_post_command("https://overpass-api.de/api/interpreter",
                                              "/tmp/s376-osm-test.post", "/tmp/s376-osm-test.json");
    CHECK(post.find(" -g ") != std::string::npos);
    CHECK(post.find("--data-urlencode") != std::string::npos);

    s376::Airport osm = s376::ksfo_public();
    const std::size_t taxis0 = osm.taxiways.size();
    const std::size_t aprons0 = osm.aprons.size();
    const char* sample =
        "{\"elements\":["
        "{\"type\":\"way\",\"tags\":{\"aeroway\":\"taxiway\"},\"geometry\":["
        "{\"lat\":37.620,\"lon\":-122.380},{\"lat\":37.621,\"lon\":-122.379}]},"
        "{\"type\":\"way\",\"tags\":{\"aeroway\":\"apron\"},\"geometry\":["
        "{\"lat\":37.616,\"lon\":-122.386},{\"lat\":37.617,\"lon\":-122.386},"
        "{\"lat\":37.617,\"lon\":-122.385},{\"lat\":37.616,\"lon\":-122.385}]},"
        "{\"type\":\"way\",\"tags\":{\"building\":\"yes\",\"height\":\"18\"},\"geometry\":["
        "{\"lat\":37.618,\"lon\":-122.384},{\"lat\":37.6185,\"lon\":-122.384},"
        "{\"lat\":37.6185,\"lon\":-122.383},{\"lat\":37.618,\"lon\":-122.383}]}"
        "]}";
    CHECK(s376::merge_osm_json(osm, sample) == 3);
    CHECK(osm.taxiways.size() == taxis0 + 1);
    CHECK(osm.aprons.size() == aprons0 + 1);
    CHECK(!osm.buildings.empty());
    CHECK_NEAR(osm.buildings.back().height_m, 18.0, 0.1);
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
    CHECK(s376::try_merge_osm_file(from_file, tmp + "/osm.json") == 3);
    CHECK(s376::try_merge_osm_file(from_file, tmp + "/missing.json") == 0);
    CHECK(s376::merge_osm_file_stream(from_file, tmp + "/osm.json", 0) == 3);
    // Runtime path never opens osm.json — only osm.bin.
    CHECK(s376::try_load_osm(from_file, tmp) == 0);
    CHECK(s376::bake_osm_bin(tmp + "/osm.json", tmp + "/osm.bin") == 3);
    CHECK(std::filesystem::exists(tmp + "/osm.bin"));
    s376::Airport from_bin;
    from_bin.icao = "KSFO";
    CHECK(s376::try_load_osm(from_bin, tmp) == 3);
    CHECK(from_bin.taxiways.size() == 1);
    CHECK(from_bin.aprons.size() == 1);
    CHECK(from_bin.buildings.size() == 1);
    CHECK_NEAR(from_bin.buildings.back().height_m, 18.0, 0.1);
    {
        const auto jfk = tmp + "/KJFK";
        std::filesystem::create_directories(jfk);
        std::filesystem::copy_file(tmp + "/osm.bin", jfk + "/osm.bin",
                                   std::filesystem::copy_options::overwrite_existing);
        s376::Airport sea;
        sea.icao = "KSEA";
        CHECK(s376::try_load_osm(sea, jfk) == 0);
        s376::Airport jf;
        jf.icao = "KJFK";
        CHECK(s376::try_load_osm(jf, jfk) == 3);
    }
    {
        const auto huge = tmp + "/huge";
        std::filesystem::create_directories(huge);
        {
            std::ofstream out(huge + "/osm.json");
            out << std::string(3 * 1024 * 1024, 'x');
            out << sample;
        }
        s376::Airport ignore;
        ignore.icao = "KSFO";
        CHECK(s376::try_merge_osm_file(ignore, huge + "/osm.json") == 0);
        CHECK(s376::try_load_osm(ignore, huge) == 0);
        CHECK(ignore.taxiways.empty());
    }

    const auto gj = s376::airport_geojson(s376::ksea_public());
    CHECK(gj.find("FeatureCollection") != std::string::npos);
    CHECK(gj.find("16L/34R") != std::string::npos);
    CHECK(gj.find("\"aeroway\":\"taxiway\"") != std::string::npos);
    CHECK(s376::write_airport_geojson(s376::ksea_public(), tmp + "/osm.geojson") == 1);
    CHECK(std::filesystem::exists(tmp + "/osm.geojson"));
    std::filesystem::remove_all(tmp);
}
