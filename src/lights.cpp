#include "s376/lights.hpp"

#include "s376/geo.hpp"

#include <algorithm>
#include <cmath>

namespace s376::lights {

int runway_base(int k) {
    if (k < 1)
        k = 1;
    if (k > 7)
        k = 7;
    return kRunwayBases[static_cast<std::size_t>(k - 1)];
}

int runway_switch(int k, int offset) {
    return runway_base(k) + offset;
}

bool is_runway_switch(int sw, int offset) {
    for (int k = 1; k <= 7; ++k) {
        if (runway_switch(k, offset) == sw)
            return true;
    }
    return false;
}

float intensity(LightKind kind, int step) {
    if (step <= 0)
        return 0.f;
    const int i = std::max(0, std::min(kIntensitySteps - 1, step - 1));
    switch (kind) {
    case LightKind::Approach:
    case LightKind::Threshold:
        return kApproachTdz[static_cast<std::size_t>(i)];
    case LightKind::Strobe:
    case LightKind::Flasher:
    case LightKind::Reil:
        return kStrobeReil[static_cast<std::size_t>(i)];
    case LightKind::Edge:
    case LightKind::Centerline:
    case LightKind::Vasi:
    case LightKind::TaxiEdge:
    case LightKind::StopBar:
    case LightKind::TaxiCenterline:
    case LightKind::Rwsl:
    case LightKind::End:
        return kEdge[static_cast<std::size_t>(i)];
    }
    return kEdge[static_cast<std::size_t>(i)];
}

float size_mult_hat(float hat_ft) {
    if (!std::isfinite(hat_ft) || hat_ft <= 0.f)
        return kSizeMultNear;
    if (hat_ft >= kSizeEaseFt)
        return kSizeMultFar;
    float t = hat_ft / kSizeEaseFt;
    t = t * t * (3.f - 2.f * t);
    return kSizeMultNear + (kSizeMultFar - kSizeMultNear) * t;
}

float base_size_px(float tod_hours) {
    float h = tod_hours;
    if (!std::isfinite(h))
        h = 12.f;
    const float noon = 1.f - std::min(1.f, std::fabs(h - 12.f) / 4.f);
    return kBaseSizePx + (kNoonSizePx - kBaseSizePx) * noon;
}

float point_size_px(float tod_hours, float hat_ft) {
    return base_size_px(tod_hours) * size_mult_hat(hat_ft);
}

float type_range_scale(LightKind kind) {
    switch (kind) {
    case LightKind::Approach:
    case LightKind::Strobe:
    case LightKind::Flasher:
    case LightKind::Reil:
        return 1.f;
    case LightKind::Edge:
    case LightKind::Centerline:
    case LightKind::Threshold:
    case LightKind::Vasi:
    case LightKind::End:
        return 0.70f;
    case LightKind::TaxiEdge:
    case LightKind::StopBar:
    case LightKind::TaxiCenterline:
    case LightKind::Rwsl:
        return 0.35f;
    }
    return 0.70f;
}

float max_range_m() {
    return kMaxRangeFt * static_cast<float>(geo::kFtToM);
}

bool beacon_on(double t_s) {
    if (!std::isfinite(t_s) || t_s < 0.0)
        t_s = 0.0;
    const double p = std::fmod(t_s, kBeaconPeriodS);
    return p >= 0.0 && p < kBeaconOnS;
}

bool strobe_on(double t_s) {
    if (!std::isfinite(t_s) || t_s < 0.0)
        t_s = 0.0;
    const double p = std::fmod(t_s, kStrobePeriodS);
    return p >= 0.0 && p < kStrobeOnS;
}

bool sfl_on(double t_s, int index) {
    if (index < 0)
        return true;
    if (!std::isfinite(t_s) || t_s < 0.0)
        t_s = 0.0;
    constexpr int kCycle = 15;
    const int slot = static_cast<int>(std::floor(std::fmod(t_s * kSflHz, kCycle)));
    return slot == (index % kCycle);
}

} // namespace s376::lights
