#pragma once

#include "s376/airport.hpp"

#include <array>

namespace s376::lights {

// Our packet-112 switch map (not a vendor table). Runway k = 1…7 uses a
// base, then offsets 1…8 for the eight groups on that runway. Taxi groups
// are global. See data/lights/switches.toml.

constexpr std::array<int, 7> kRunwayBases = {0, 8, 16, 24, 32, 84, 92};

constexpr int kOffApproachA = 1;
constexpr int kOffStrobeA = 2;
constexpr int kOffEdge = 3;
constexpr int kOffCenterline = 4;
constexpr int kOffVasi = 5;
constexpr int kOffTdz = 6;
constexpr int kOffApproachB = 7;
constexpr int kOffStrobeB = 8;

constexpr int kSwTaxiEdge = 41;
constexpr int kSwStopBar = 56;
constexpr int kSwTaxiCenterline = 60;

constexpr int kIntensitySteps = 5;

// Step 1…5 (step 0 is off). Approach and TDZ share one curve; strobe/REIL
// another; edge (and everything else) a third.
constexpr std::array<float, kIntensitySteps> kApproachTdz = {0.27f, 0.35f, 0.50f, 0.74f, 1.0f};
constexpr std::array<float, kIntensitySteps> kStrobeReil = {0.70f, 0.75f, 0.80f, 0.85f, 1.0f};
constexpr std::array<float, kIntensitySteps> kEdge = {0.35f, 0.55f, 0.68f, 0.92f, 1.0f};

constexpr float kBaseSizePx = 2.5f;
constexpr float kNoonSizePx = 2.8f;
constexpr float kSizeMultNear = 12.f; // at 0 ft HAT
constexpr float kSizeMultFar = 1.f;   // by 500 ft HAT
constexpr float kSizeEaseFt = 500.f;
constexpr float kMaxRangeFt = 250000.f;

constexpr double kBeaconPeriodS = 1.5;
constexpr double kBeaconOnS = 0.1;
constexpr double kStrobePeriodS = 1.5;
constexpr double kStrobeOnS = 0.066;
// Sequenced flasher rate is unknown on the original, so use the FAA default.
constexpr double kSflHz = 2.0;

int runway_base(int k); // k = 1…7
int runway_switch(int k, int offset);
bool is_runway_switch(int sw, int offset);

float intensity(LightKind kind, int step); // 0 if step <= 0
// 1 at night; daytime edge/taxi/night-layer barely visible (~0.04).
// Approach / TDZ / PAPI stay readable in daylight (Raster step curve).
float tod_visibility(LightKind kind, float tod_hours);
float size_mult_hat(float hat_ft);
float base_size_px(float tod_hours);
float point_size_px(float tod_hours, float hat_ft);
float type_range_scale(LightKind kind); // 1 = full kMaxRangeFt
float max_range_m();

// PAPI 4-box: box 0 inboard (lowest angle) … box 3 outboard. 20' of arc
// between units. White when the eye is above that unit's setting.
bool papi_box_white(float eye_angle_deg, int box, float gs_deg);
float papi_eye_angle_deg(double eye_lat, double eye_lon, double eye_alt_m, double papi_lat,
                         double papi_lon, double papi_alt_m);

bool beacon_on(double t_s);
bool strobe_on(double t_s);
bool sfl_on(double t_s, int index);

} // namespace s376::lights
