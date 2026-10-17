#pragma once

namespace s376::geo {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kFtToM = 0.3048;
constexpr double kMToFt = 1.0 / kFtToM;
constexpr double kWgsA = 6378137.0;
constexpr double kWgsB = 6356752.314245;

inline double deg2rad(double d) {
    return d * kDeg;
}
inline double rad2deg(double r) {
    return r / kDeg;
}

struct Ecef {
    double x = 0, y = 0, z = 0;
};

struct Enu {
    double e = 0, n = 0, u = 0;
};

Ecef lla_to_ecef(double lat_deg, double lon_deg, double alt_m);
void ecef_to_enu_axes(double lat_deg, double lon_deg, Ecef& east, Ecef& north, Ecef& up);

// Body axes: x forward, y right, z down. Heading clockwise from north.
void body_to_enu(double heading_deg, double pitch_deg, double roll_deg, Enu& fwd, Enu& right,
                 Enu& down);

// Sun unit vector in ECEF (points toward the sun). tod_hours is 0..24 local solar
// approximation using longitude. Returns elevation degrees as well.
Ecef sun_ecef(double lat_deg, double lon_deg, double tod_hours, int month, int day,
              double* elev_deg_out = nullptr);

int day_of_year(int year, int month, int day);

} // namespace s376::geo
