#include "s376/config.hpp"
#include "s376/diagnostics.hpp"
#include "s376/terrain.hpp"

#include <cmath>
#include <cstdlib>
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

void test_diagnostics() {
    CHECK(s376::format_byte_count(0) == "0 B");
    CHECK(s376::format_byte_count(512) == "512 B");
    const auto kib = s376::format_byte_count(2048);
    CHECK(kib.find("KiB") != std::string::npos);
    const auto mib = s376::format_byte_count(2ull * 1024 * 1024);
    CHECK(mib.find("MiB") != std::string::npos);

    const auto tmp = (std::filesystem::temp_directory_path() / "s376-doctor-cache").string();
    std::filesystem::remove_all(tmp);
    auto empty = s376::scan_airport_caches(tmp, "");
    CHECK(empty.size() >= 3);
    bool saw_sea = false, saw_sfo = false, saw_las = false;
    for (const auto& s : empty) {
        if (s.icao == "KSEA") {
            saw_sea = true;
            CHECK(s.bytes == 0);
        }
        if (s.icao == "KSFO")
            saw_sfo = true;
        if (s.icao == "KLAS")
            saw_las = true;
    }
    CHECK(saw_sea && saw_sfo && saw_las);

    auto region = s376::generate_ksea_cache();
    s376::write_cache(region, tmp + "/ksea");
    std::filesystem::create_directories(tmp + "/egll");
    {
        std::ofstream extra(tmp + "/egll/elev.s376dem");
        extra << "not-a-real-dem";
    }
    auto packed = s376::scan_airport_caches(tmp, "");
    bool ksea_ok = false, egll_ok = false;
    for (const auto& s : packed) {
        if (s.icao == "KSEA") {
            ksea_ok = s.bytes > 100;
            CHECK(s.path.find("ksea") != std::string::npos);
        }
        if (s.icao == "EGLL")
            egll_ok = s.bytes > 0;
    }
    CHECK(ksea_ok);
    CHECK(egll_ok);
    std::filesystem::remove_all(tmp);

    auto cfg = s376::default_config();
    auto times = s376::bench_channel_times(cfg, false);
    CHECK(times.size() == 4);
    bool fl = false, ml = false, mr = false, fr = false;
    for (const auto& t : times) {
        CHECK(t.cpu_ms >= 0);
        CHECK(t.gpu_ms < 0);
        if (t.name == "far_left")
            fl = true;
        if (t.name == "mid_left")
            ml = true;
        if (t.name == "mid_right")
            mr = true;
        if (t.name == "far_right")
            fr = true;
    }
    CHECK(fl && ml && mr && fr);

    const char* old_dpy = std::getenv("DISPLAY");
    std::string saved_dpy = old_dpy ? old_dpy : "";
    unsetenv("DISPLAY");
    auto report = s376::run_doctor(cfg, "", false, true);
    if (!saved_dpy.empty())
        setenv("DISPLAY", saved_dpy.c_str(), 1);
    const auto text = s376::format_report(report);
    CHECK(text.find("cache-ksea") != std::string::npos);
    CHECK(text.find("cache-ksfo") != std::string::npos);
    CHECK(text.find("cache-klas") != std::string::npos);
    CHECK(text.find("mesh-build") != std::string::npos);
    CHECK(text.find("frame-far_left") != std::string::npos);
    CHECK(text.find("frame-mid_left") != std::string::npos);
    CHECK(text.find("frame-mid_right") != std::string::npos);
    CHECK(text.find("frame-far_right") != std::string::npos);
    CHECK(report.fails == 0);
}
