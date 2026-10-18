#pragma once

#include "s376/config.hpp"
#include "s376/state.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace s376 {

enum class CheckStatus { Pass, Warn, Fail, Skip };

struct Check {
    std::string name;
    CheckStatus status = CheckStatus::Pass;
    std::string detail;
    std::string fix;
};

struct DoctorReport {
    std::vector<Check> checks;
    int fails = 0;
    int warns = 0;
};

struct AirportCacheSize {
    std::string icao;
    std::string path;
    std::uint64_t bytes = 0;
};

struct ChannelFrameTime {
    std::string name;
    double cpu_ms = 0;  // make_channel_view cost
    double gpu_ms = -1; // CompositeViewer share; <0 if not measured
};

DoctorReport run_doctor(const AppConfig& cfg, const std::string& config_path, bool listen,
                        bool bench = true);
std::string format_report(const DoctorReport& r);
std::string diagnostics_dump(const AppConfig& cfg, const HostState* host = nullptr);

std::string format_byte_count(std::uint64_t n);
// Known public airports plus any extra packed dirs under `root`.
std::vector<AirportCacheSize> scan_airport_caches(const std::string& root,
                                                  const std::string& extra_dir = {});
// CPU view cost always; GPU frames only when `try_gpu` and DISPLAY is set.
std::vector<ChannelFrameTime> bench_channel_times(const AppConfig& cfg, bool try_gpu);

} // namespace s376
