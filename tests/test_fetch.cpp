#include "s376/dxt.hpp"
#include "s376/fetch.hpp"
#include "s376/safety.hpp"
#include "s376/terrain.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

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

void test_fetch() {
    auto ksea = s376::lookup_airport_ref("ksea");
    CHECK(ksea.icao == "KSEA");
    CHECK(ksea.conus);
    CHECK_NEAR(ksea.lat, 47.4502, 1e-4);
    auto sfo = s376::lookup_airport_ref("KSFO");
    CHECK(sfo.conus);
    auto egll = s376::lookup_airport_ref("EGLL");
    CHECK(!egll.conus);

    const auto box = s376::box_from_radius(47.45, -122.31, 25);
    CHECK(box.west < -122.31 && box.east > -122.31);
    CHECK(box.south < 47.45 && box.north > 47.45);

    const auto url = s376::glo30_tile_url(47, -123);
    CHECK(url.find("N47") != std::string::npos);
    CHECK(url.find("W123") != std::string::npos);
    CHECK(url.find("Copernicus_DSM_COG_10") != std::string::npos);
    s376::safety::check_data_url(url);

    const auto tiles = s376::glo30_urls_for_box(box);
    CHECK(!tiles.empty());
    CHECK(tiles.size() <= 9);

    bool threw = false;
    try {
        s376::safety::check_data_url("http://10.1.95.1/x");
    } catch (const s376::safety::UnsafeEndpoint&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        s376::safety::check_data_url("https://evil.example/x");
    } catch (const s376::safety::UnsafeEndpoint&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(s376::safety::is_allowed_data_host("copernicus-dem-30m.s3.eu-central-1.amazonaws.com"));
    CHECK(s376::safety::is_allowed_data_host("elevation.nationalmap.gov"));
    CHECK(s376::safety::is_allowed_data_host("tiles.maps.eox.at"));
    CHECK(s376::safety::is_allowed_data_host("dl.polyhaven.org"));
    CHECK(s376::safety::is_allowed_data_host("ambientcg.com"));
    CHECK(s376::safety::is_allowed_data_host("orthos.its.ny.gov"));
    CHECK(s376::safety::is_allowed_data_host("its.ny.gov"));
    CHECK(s376::safety::is_allowed_data_host("imagery.nationalmap.gov"));
    CHECK(!s376::safety::is_allowed_data_host("10.1.95.1"));
    CHECK(s376::imagery_covers_nys(40.64, -73.78));
    CHECK(!s376::imagery_covers_nys(47.45, -122.31));
    s376::GeoBox nyc;
    nyc.west = -73.90;
    nyc.south = 40.60;
    nyc.east = -73.70;
    nyc.north = 40.70;
    const auto nys_url = s376::imagery_export_url("nys", nyc, 512, 512);
    CHECK(nys_url.find("orthos.its.ny.gov") != std::string::npos);
    CHECK(s376::curl_glob_safe(nys_url));
    s376::safety::check_data_url(nys_url);
    const auto hro_url = s376::imagery_export_url("hro", nyc, 512, 512);
    CHECK(hro_url.find("USGSNAIPPlus") != std::string::npos);
    s376::safety::check_data_url(hro_url);

    s376::FetchRequest req;
    req.icao = "KSEA";
    req.radius_km = 25;
    req.dry_run = true;
    auto plan = s376::plan_fetch(req);
    CHECK(plan.dem_kind == "3dep");
    CHECK(plan.img_kind == "naip");
    CHECK(plan.lods.size() >= 2);
    CHECK(plan.lods[0].bc7);
    CHECK(plan.lods[0].tiled);
    CHECK(plan.lods[0].img_m > 0 && plan.lods[0].img_m <= 1.01);
    CHECK(!plan.urls.empty());
    CHECK(plan.estimate.img_tiles >= 4);
    CHECK(plan.estimate.dem_tiles >= 2);
    CHECK(plan.estimate.est_packed_bytes > 1024 * 1024);
    for (const auto& u : plan.urls)
        CHECK(s376::curl_glob_safe(u));

    s376::GeoBox tb;
    tb.west = -122.40;
    tb.east = -122.20;
    tb.south = 47.40;
    tb.north = 47.52;
    const auto rast = s376::tile_raster(tb, 1.0, 4000);
    CHECK(rast.size() >= 4);
    for (const auto& t : rast) {
        CHECK(t.w <= 4000 && t.h <= 4000);
        CHECK(t.w >= 8 && t.h >= 8);
        CHECK(t.box.east > t.box.west);
        CHECK(t.box.north > t.box.south);
    }
    const auto cov = s376::coverage_boxes(ksea, req);
    CHECK(cov.size() >= 2);
    const auto uni = s376::union_boxes(cov);
    CHECK(uni.east > uni.west);
    const auto est = s376::estimate_fetch(req);
    CHECK(est.img_tiles == plan.estimate.img_tiles);
    CHECK(plan.attribution.find("GLO-30") != std::string::npos ||
          plan.attribution.find("3DEP") != std::string::npos);

    req.icao = "EGLL";
    plan = s376::plan_fetch(req);
    CHECK(plan.dem_kind == "glo30");
    CHECK(plan.img_kind == "s2");

    req.icao = "KJFK";
    req.imagery = "auto";
    plan = s376::plan_fetch(req);
    CHECK(plan.dem_kind == "3dep");
    CHECK(plan.img_kind == "nys");
    bool saw_nys = false;
    for (const auto& u : plan.urls) {
        CHECK(s376::curl_glob_safe(u));
        if (u.find("orthos.its.ny.gov") != std::string::npos)
            saw_nys = true;
    }
    CHECK(saw_nys);
    CHECK(plan.attribution.find("NYS") != std::string::npos);

    auto src = s376::generate_ksea_cache();
    const auto tmp = (std::filesystem::temp_directory_path() / "s376-lod-test").string();
    std::filesystem::remove_all(tmp);
    const auto n = s376::pack_lods(src, tmp, s376::default_lods(25), true);
    CHECK(n > 1000);
    CHECK(n < 50ull * 1024 * 1024);
    CHECK(std::filesystem::exists(tmp + "/elev.s376dem"));
    CHECK(std::filesystem::exists(tmp + "/lod0/elev.s376dem"));
    CHECK(std::filesystem::exists(tmp + "/lod1/imagery.dds"));
    const auto dds0 = std::filesystem::file_size(tmp + "/lod0/imagery.dds");
    CHECK(dds0 > 128 + s376::dxt::bc7_size(4, 4)); // BC7 near field + header

    // BC7 encode is larger than BC1 and still far smaller than raw RGB.
    std::vector<std::uint8_t> red(16 * 16 * 3, 200);
    auto b7 = s376::dxt::encode_bc7(red.data(), 16, 16);
    CHECK(b7.size() == s376::dxt::bc7_size(16, 16));
    CHECK(s376::dxt::mip_count(16, 16) == 5);

    std::filesystem::remove_all(tmp);
}
