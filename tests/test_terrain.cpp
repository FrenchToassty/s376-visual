#include "s376/dxt.hpp"
#include "s376/geo.hpp"
#include "s376/terrain.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
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

void test_terrain() {
    auto dem = s376::DemGrid::synth_ksea(64, 64);
    CHECK(dem.w == 64 && dem.h == 64);
    CHECK(dem.in_bounds(47.4479, -122.3103));
    CHECK(!dem.in_bounds(0, 0));

    double z_m = 0, z_ft = 0;
    CHECK(dem.sample_m(47.4479, -122.3103, z_m));
    CHECK_NEAR(z_m, 132.0, 0.6); // flattened airport pad
    CHECK(dem.sample_ft(47.4479, -122.3103, z_ft));
    CHECK_NEAR(z_ft, 132.0 * s376::geo::kMToFt, 2.0);
    CHECK_NEAR(z_ft, 433.0, 3.0);

    // 16L threshold is on the pad.
    CHECK(dem.sample_ft(47.463684, -122.307751, z_ft));
    CHECK_NEAR(z_ft, 433.0, 8.0);

    const auto tmp = std::filesystem::temp_directory_path() / "s376-terrain-test";
    std::filesystem::create_directories(tmp);
    const auto dem_path = (tmp / "elev.s376dem").string();
    dem.save(dem_path);
    auto loaded = s376::DemGrid::load(dem_path);
    CHECK(loaded.w == dem.w && loaded.h == dem.h);
    double z2 = 0;
    CHECK(loaded.sample_m(47.4479, -122.3103, z2));
    CHECK_NEAR(z2, z_m, 1e-4);

    s376::DemHeightSource src(std::make_shared<s376::DemGrid>(dem));
    double elev = -1;
    CHECK(src.sample_ft(47.4479, -122.3103, elev));
    CHECK_NEAR(elev, 433.0, 3.0);
    CHECK(!src.sample_ft(0, 0, elev));

    auto hs = s376::ImageRgb::hillshade(dem);
    CHECK(hs.w == dem.w && hs.h == dem.h);
    CHECK(hs.rgb.size() == static_cast<std::size_t>(hs.w * hs.h * 3));

    // Solid red 8×8 → BC1 round-trip stays reddish.
    std::vector<std::uint8_t> red(8 * 8 * 3);
    for (std::size_t i = 0; i < red.size(); i += 3) {
        red[i] = 220;
        red[i + 1] = 20;
        red[i + 2] = 20;
    }
    auto blocks = s376::dxt::encode_bc1(red.data(), 8, 8);
    CHECK(blocks.size() == s376::dxt::bc1_size(8, 8));
    CHECK(blocks.size() == 32); // 2×2 blocks × 8 bytes
    auto back = s376::dxt::decode_bc1(blocks.data(), 8, 8);
    CHECK(back.size() == red.size());
    CHECK(back[0] > 160 && back[1] < 80 && back[2] < 80);

    const auto dds = (tmp / "red.dds").string();
    s376::dxt::write_dds_bc1(dds, red.data(), 8, 8);
    CHECK(std::filesystem::file_size(dds) == 128 + 32);

    auto region = s376::generate_ksea_cache();
    const auto cdir = (tmp / "ksea").string();
    s376::write_cache(region, cdir);
    CHECK(std::filesystem::exists(cdir + "/elev.s376dem"));
    CHECK(std::filesystem::exists(cdir + "/imagery.dds"));
    CHECK(std::filesystem::exists(cdir + "/region.toml"));
    const auto packed = std::filesystem::file_size(cdir + "/elev.s376dem") +
                        std::filesystem::file_size(cdir + "/imagery.dds");
    CHECK(packed < 200000);          // hundreds of KiB, not 857 GB
    CHECK(packed > 64);              // not empty
    auto again = s376::load_cache(cdir);
    CHECK(again.dem.w == region.dem.w);
    CHECK(!again.imagery_dds.empty());

    const auto hat = s376::sample_hat(&src, 47.463684, -122.307751, 447.0);
    CHECK(hat.covered);
    CHECK_NEAR(hat.elev_ft, 433.0, 8.0);
    CHECK_NEAR(hat.hat_ft, 447.0 - hat.elev_ft, 0.05);
    const auto miss = s376::sample_hat(&src, 0.0, 0.0, 1000.0);
    CHECK(!miss.covered);
    CHECK_NEAR(miss.hat_ft, 1000.0, 0.01);
    CHECK_NEAR(miss.elev_ft, 0.0, 0.01);
    const auto standin = s376::sample_hat(nullptr, 47.46, -122.31, 500.0);
    CHECK(!standin.covered);
    CHECK_NEAR(standin.hat_ft, 500.0, 0.01);

#ifdef S376_HAS_GDAL
    CHECK(s376::gdal_available());
    const auto tif = (tmp / "ksea.tif").string();
    dem.save_geotiff(tif);
    auto from_tif = s376::DemGrid::load_geotiff(tif);
    CHECK(from_tif.w == dem.w && from_tif.h == dem.h);
    double zt = 0;
    CHECK(from_tif.sample_m(47.4479, -122.3103, zt));
    CHECK_NEAR(zt, z_m, 1.0);
#else
    CHECK(!s376::gdal_available());
#endif

    std::filesystem::remove_all(tmp);
}
