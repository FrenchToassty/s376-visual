#include "s376/geo.hpp"

#include <cmath>

namespace s376::geo {

Ecef lla_to_ecef(double lat_deg, double lon_deg, double alt_m) {
    const double lat = deg2rad(lat_deg);
    const double lon = deg2rad(lon_deg);
    const double sl = std::sin(lat), cl = std::cos(lat);
    const double e2 = 1.0 - (kWgsB * kWgsB) / (kWgsA * kWgsA);
    const double n = kWgsA / std::sqrt(1.0 - e2 * sl * sl);
    Ecef p;
    p.x = (n + alt_m) * cl * std::cos(lon);
    p.y = (n + alt_m) * cl * std::sin(lon);
    p.z = (n * (1.0 - e2) + alt_m) * sl;
    return p;
}

void ecef_to_enu_axes(double lat_deg, double lon_deg, Ecef& east, Ecef& north, Ecef& up) {
    const double lat = deg2rad(lat_deg);
    const double lon = deg2rad(lon_deg);
    const double sl = std::sin(lat), cl = std::cos(lat);
    const double so = std::sin(lon), co = std::cos(lon);
    east = {-so, co, 0};
    north = {-sl * co, -sl * so, cl};
    up = {cl * co, cl * so, sl};
}

void body_to_enu(double heading_deg, double pitch_deg, double roll_deg, Enu& fwd, Enu& right,
                 Enu& down) {
    const double h = deg2rad(heading_deg);
    const double p = deg2rad(pitch_deg);
    const double r = deg2rad(roll_deg);
    const double ch = std::cos(h), sh = std::sin(h);
    const double cp = std::cos(p), sp = std::sin(p);
    const double cr = std::cos(r), sr = std::sin(r);
    // Heading clockwise from north, pitch nose-up, roll right-wing-down.
    // Forward (body +X)
    fwd.n = cp * ch;
    fwd.e = cp * sh;
    fwd.u = sp;
    // Right (body +Y)
    right.n = sr * sp * ch - cr * sh;
    right.e = sr * sp * sh + cr * ch;
    right.u = -sr * cp;
    // Down (body +Z)
    down.n = cr * sp * ch + sr * sh;
    down.e = cr * sp * sh - sr * ch;
    down.u = -cr * cp;
}

int day_of_year(int year, int month, int day) {
    static const int mdays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int d = day;
    for (int m = 1; m < month && m <= 12; ++m)
        d += mdays[m];
    const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (leap && month > 2)
        ++d;
    if (d < 1)
        d = 1;
    if (d > 366)
        d = 366;
    return d;
}

Ecef sun_ecef(double lat_deg, double lon_deg, double tod_hours, int month, int day,
              double* elev_deg_out) {
    const int doy = day_of_year(2000, month <= 0 ? 6 : month, day <= 0 ? 21 : day);
    const double decl = deg2rad(23.44 * std::sin(2.0 * kPi * (doy - 81) / 365.0));
    // Approximate solar time: UTC hours + longitude/15. Host TOD is treated as UTC.
    const double hour_angle = deg2rad(15.0 * (tod_hours - 12.0) + lon_deg);
    const double lat = deg2rad(lat_deg);
    const double sin_el =
        std::sin(lat) * std::sin(decl) + std::cos(lat) * std::cos(decl) * std::cos(hour_angle);
    const double el = std::asin(std::max(-1.0, std::min(1.0, sin_el)));
    if (elev_deg_out)
        *elev_deg_out = rad2deg(el);
    const double cos_el = std::cos(el);
    double az = 0;
    if (std::fabs(cos_el) > 1e-6) {
        const double cos_az =
            (std::sin(decl) - std::sin(el) * std::sin(lat)) / (cos_el * std::cos(lat));
        az = std::acos(std::max(-1.0, std::min(1.0, cos_az)));
        if (hour_angle > 0)
            az = 2.0 * kPi - az; // afternoon: west
    }
    // Sun direction in ENU: azimuth from north, clockwise.
    Enu s;
    s.n = std::cos(el) * std::cos(az);
    s.e = std::cos(el) * std::sin(az);
    s.u = std::sin(el);
    Ecef east, north, up;
    ecef_to_enu_axes(lat_deg, lon_deg, east, north, up);
    Ecef dir;
    dir.x = s.e * east.x + s.n * north.x + s.u * up.x;
    dir.y = s.e * east.y + s.n * north.y + s.u * up.y;
    dir.z = s.e * east.z + s.n * north.z + s.u * up.z;
    const double mag = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (mag > 0) {
        dir.x /= mag;
        dir.y /= mag;
        dir.z /= mag;
    }
    return dir;
}

} // namespace s376::geo
