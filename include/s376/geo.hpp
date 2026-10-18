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

// Shared-eyepoint channel view: the same geodetic eye, camera rotated by the
// channel heading/pitch/roll offsets (RSI ViewAngles model). Symmetric
// perspective; overlap comes from the 50° FOV vs ~35° heading split.
struct ChannelView {
    Ecef eye;
    Ecef forward;
    Ecef up;
    double heading_deg = 0;
    double vfov_deg = 32.5;
    double hfov_deg = 50.0;
    double aspect = 16.0 / 9.0;
    double near_m = 3.0;
    double far_m = 2.0e7;
};

ChannelView make_channel_view(double lat_deg, double lon_deg, double alt_m, double true_hdg_deg,
                              double pitch_deg, double roll_deg, double heading_offset_deg,
                              double pitch_offset_deg, double roll_offset_deg, double hfov_deg,
                              double vfov_deg, int width, int height, double eye_fwd_m = 0,
                              double eye_right_m = 0, double eye_up_m = 0);

} // namespace s376::geo
