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

float tod_visibility(LightKind kind, float tod_hours) {
    float h = tod_hours;
    if (!std::isfinite(h))
        h = 12.f;
    // 1 at night; ramps down through dawn/dusk; noon is the floor.
    const float night = std::min(1.f, std::max(0.f, std::fabs(h - 12.f) - 5.f) / 4.f);
    const float day = 1.f - night;
    float floor = 0.04f;
    switch (kind) {
    case LightKind::Approach:
    case LightKind::Strobe:
    case LightKind::Flasher:
    case LightKind::Reil:
        floor = 0.62f; // Raster ALS is built to read in daylight
        break;
    case LightKind::Threshold:
    case LightKind::End:
        floor = 0.45f;
        break;
    case LightKind::Vasi:
        floor = 0.88f; // 2W2R must read on a 3° path at noon
        break;
    case LightKind::Night:
    case LightKind::SignGlow:
    case LightKind::TaxiEdge:
    case LightKind::TaxiCenterline:
    case LightKind::LeadOn:
        floor = 0.015f; // street/taxi wash off in day
        break;
    case LightKind::Edge:
    case LightKind::Centerline:
        floor = 0.035f; // daytime runway edge barely visible
        break;
    default:
        floor = 0.05f;
        break;
    }
    return night + day * floor;
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
    case LightKind::LeadOn:
    case LightKind::SignGlow:
    case LightKind::Beacon:
    case LightKind::Night:
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

float night_weight(float tod_hours) {
    float h = tod_hours;
    if (!std::isfinite(h))
        h = 12.f;
    return std::min(1.f, std::max(0.f, std::fabs(h - 12.f) - 6.f) / 3.f);
}

float base_size_px(float tod_hours) {
    float h = tod_hours;
    if (!std::isfinite(h))
        h = 12.f;
    const float noon = 1.f - std::min(1.f, std::fabs(h - 12.f) / 4.f);
    const float night = night_weight(h);
    const float day_sz = kBaseSizePx + (kNoonSizePx - kBaseSizePx) * noon;
    return day_sz + (kNightFarSizePx - day_sz) * night;
}

float point_size_px(float tod_hours, float hat_ft, float range_m) {
    const float base = base_size_px(tod_hours);
    const float near = base * size_mult_hat(hat_ft);
    if (!std::isfinite(range_m) || range_m <= 0.f)
        return near;
    float t = std::min(1.f, range_m / kSizeNearM);
    t = t * t * (3.f - 2.f * t);
    return near + (base - near) * t;
}

float range_visibility(LightKind kind, float range_m) {
    if (!std::isfinite(range_m) || range_m < 0.f)
        range_m = 0.f;
    const float maxr = max_range_m() * type_range_scale(kind);
    if (!(maxr > 1.f) || range_m >= maxr)
        return 0.f;
    // 50 % at ~2 km; approach / PAPI keep a floor so 3 nm ALS still reads.
    const float d0 = 2000.f;
    const float inv = 1.f / (1.f + (range_m / d0) * (range_m / d0));
    float floor = 0.05f;
    switch (kind) {
    case LightKind::Approach:
    case LightKind::Strobe:
    case LightKind::Flasher:
    case LightKind::Reil:
    case LightKind::Vasi:
        floor = 0.78f;
        break;
    case LightKind::Threshold:
    case LightKind::End:
        floor = 0.14f;
        break;
    default:
        floor = 0.05f;
        break;
    }
    const float tail = 1.f - range_m / maxr;
    return std::max(inv, floor * tail);
}

bool papi_box_white(float eye_angle_deg, int box, float gs_deg) {
    if (box < 0)
        box = 0;
    if (box > 3)
        box = 3;
    if (!std::isfinite(eye_angle_deg))
        eye_angle_deg = gs_deg;
    if (!std::isfinite(gs_deg) || gs_deg < 0.5f)
        gs_deg = 3.f;
    const float step = 20.f / 60.f; // FAA 20 arcmin between units
    const float setting = gs_deg + (static_cast<float>(box) - 1.5f) * step;
    return eye_angle_deg > setting;
}

float papi_eye_angle_deg(double eye_lat, double eye_lon, double eye_alt_m, double papi_lat,
                         double papi_lon, double papi_alt_m) {
    const double dist = geo::ground_distance_m(eye_lat, eye_lon, papi_lat, papi_lon);
    if (!(dist > 1.0) || !std::isfinite(dist))
        return 90.f;
    return static_cast<float>(geo::rad2deg(std::atan2(eye_alt_m - papi_alt_m, dist)));
}

float type_size_boost(LightKind kind) {
    if (kind == LightKind::Vasi)
        return 2.3f; // ~6.4 px at the Raster floor; 4 boxes stay distinct
    return 1.f;
}

float type_range_scale(LightKind kind) {
    switch (kind) {
    case LightKind::Approach:
    case LightKind::Strobe:
    case LightKind::Flasher:
    case LightKind::Reil:
    case LightKind::Beacon:
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
    case LightKind::LeadOn:
    case LightKind::SignGlow:
    case LightKind::Night:
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
