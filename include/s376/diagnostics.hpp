#pragma once

#include "s376/config.hpp"
#include "s376/state.hpp"

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

DoctorReport run_doctor(const AppConfig& cfg, const std::string& config_path, bool listen);
std::string format_report(const DoctorReport& r);
std::string diagnostics_dump(const AppConfig& cfg, const HostState* host = nullptr);

} // namespace s376
