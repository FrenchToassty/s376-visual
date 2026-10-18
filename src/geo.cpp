#include "s376/geo.hpp"

#include <algorithm>
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

ChannelView make_channel_view(double lat_deg, double lon_deg, double alt_m, double true_hdg_deg,
                              double pitch_deg, double roll_deg, double heading_offset_deg,
                              double pitch_offset_deg, double roll_offset_deg, double hfov_deg,
                              double vfov_deg, int width, int height, double eye_fwd_m,
                              double eye_right_m, double eye_up_m) {
    ChannelView v;
    v.eye = lla_to_ecef(lat_deg, lon_deg, alt_m);
    if (eye_fwd_m != 0.0 || eye_right_m != 0.0 || eye_up_m != 0.0) {
        Enu bf, br, bd;
        body_to_enu(true_hdg_deg, pitch_deg, roll_deg, bf, br, bd);
        Ecef east, north, up;
        ecef_to_enu_axes(lat_deg, lon_deg, east, north, up);
        auto add = [&](const Enu& b, double s) {
            v.eye.x += s * (b.e * east.x + b.n * north.x + b.u * up.x);
            v.eye.y += s * (b.e * east.y + b.n * north.y + b.u * up.y);
            v.eye.z += s * (b.e * east.z + b.n * north.z + b.u * up.z);
        };
        add(bf, eye_fwd_m);
        add(br, eye_right_m);
        add(bd, -eye_up_m);
    }
    const double hdg = true_hdg_deg + heading_offset_deg;
    const double pit = pitch_deg + pitch_offset_deg;
    const double rol = roll_deg + roll_offset_deg;
    v.heading_deg = hdg;
    Enu fwd, right, down;
    body_to_enu(hdg, pit, rol, fwd, right, down);
    Ecef east, north, up;
    ecef_to_enu_axes(lat_deg, lon_deg, east, north, up);
    auto to_ecef = [&](const Enu& b) {
        Ecef o;
        o.x = b.e * east.x + b.n * north.x + b.u * up.x;
        o.y = b.e * east.y + b.n * north.y + b.u * up.y;
        o.z = b.e * east.z + b.n * north.z + b.u * up.z;
        return o;
    };
    v.forward = to_ecef(fwd);
    const Ecef d = to_ecef(down);
    v.up = {-d.x, -d.y, -d.z};
    v.hfov_deg = hfov_deg;
    v.vfov_deg = vfov_deg;
    v.aspect = static_cast<double>(std::max(1, width)) / static_cast<double>(std::max(1, height));
    v.near_m = std::max(3.0, alt_m * 0.02);
    v.far_m = 2.0e7;
    return v;
}

namespace {

float clampf(float x, float a, float b) {
    return x < a ? a : (x > b ? b : x);
}

float smooth01(float e0, float e1, float x) {
    if (e1 <= e0)
        return x >= e1 ? 1.f : 0.f;
    const float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float soft_box(double lat, double lon, double lat0, double lat1, double lon0, double lon1,
               double fade) {
    const float a = smooth01(static_cast<float>(lat0 - fade), static_cast<float>(lat0),
                             static_cast<float>(lat)) *
                    (1.f - smooth01(static_cast<float>(lat1), static_cast<float>(lat1 + fade),
                                    static_cast<float>(lat)));
    const float b = smooth01(static_cast<float>(lon0 - fade), static_cast<float>(lon0),
                             static_cast<float>(lon)) *
                    (1.f - smooth01(static_cast<float>(lon1), static_cast<float>(lon1 + fade),
                                    static_cast<float>(lon)));
    return a * b;
}

double lerp_coast(double lat, const double* lats, const double* lons, int n) {
    if (lat <= lats[0])
        return lons[0];
    if (lat >= lats[n - 1])
        return lons[n - 1];
    for (int i = 0; i < n - 1; ++i) {
        if (lat <= lats[i + 1]) {
            const double t = (lat - lats[i]) / (lats[i + 1] - lats[i]);
            return lons[i] + t * (lons[i + 1] - lons[i]);
        }
    }
    return lons[n - 1];
}

} // namespace

float land_coverage(double lat, double lon) {
    float land = 0.f;
    land = std::max(land, soft_box(lat, lon, -55.0, 14.0, -82.0, -34.0, 0.7)); // S America
    land = std::max(land, soft_box(lat, lon, 35.0, 71.5, -10.0, 40.0, 0.5));   // Europe
    land = std::max(land, soft_box(lat, lon, -35.0, 37.0, -18.0, 52.0, 0.6));  // Africa
    land = std::max(land, soft_box(lat, lon, 5.0, 75.0, 40.0, 180.0, 0.6));    // Asia
    land = std::max(land, soft_box(lat, lon, -45.0, -10.0, 112.0, 154.0, 0.5)); // Australia
    land = std::max(land, soft_box(lat, lon, 50.0, 72.0, -140.0, -55.0, 0.8)); // Canada shield

    // North America west of a soft east-coast polyline (so the Atlantic is ocean).
    static const double clat[] = {15.0, 24.5, 29.8, 33.0, 35.6, 38.6, 40.15, 40.85, 42.35, 44.6, 47.2, 51.0};
    static const double clon[] = {-97.8, -80.5, -81.5, -79.1, -75.5, -75.05, -74.08, -73.92, -70.75, -67.9, -67.6, -55.0};
    if (lat > 14.0 && lat < 73.0 && lon > -170.0 && lon < -50.0) {
        const double coast = lerp_coast(lat, clat, clon, 12);
        float na = 1.f - smooth01(static_cast<float>(coast - 0.14), static_cast<float>(coast + 0.14),
                                  static_cast<float>(lon));
        na *= smooth01(15.f, 16.5f, static_cast<float>(lat)) *
              (1.f - smooth01(71.5f, 73.5f, static_cast<float>(lat)));
        na *= smooth01(-168.f, -164.f, static_cast<float>(lon));
        land = std::max(land, na);
    }

    // Long Island + Rockaway barrier (KJFK photos). Soft so coasts are not stairs.
    land = std::max(land, soft_box(lat, lon, 40.58, 41.18, -74.06, -71.86, 0.03));
    land = std::min(land, 1.f - 0.96f * soft_box(lat, lon, 40.598, 40.652, -73.900, -73.778, 0.008));
    land = std::max(land, soft_box(lat, lon, 40.545, 40.588, -73.96, -73.73, 0.008)); // Rockaway
    land = std::max(land, soft_box(lat, lon, 40.626, 40.662, -73.828, -73.752, 0.006)); // JFK field
    land = std::max(land, soft_box(lat, lon, 41.52, 42.10, -70.62, -69.90, 0.04));     // Cape Cod
    land = std::max(land, soft_box(lat, lon, 42.20, 42.48, -71.20, -70.85, 0.03));     // Boston neck

    // West-coast bays (keep the airport pads as land).
    float sf = soft_box(lat, lon, 37.48, 38.10, -122.53, -122.20, 0.025);
    sf *= 1.f - soft_box(lat, lon, 37.598, 37.640, -122.402, -122.348, 0.008);
    land = std::min(land, 1.f - 0.88f * sf);
    float ps = soft_box(lat, lon, 47.32, 48.18, -122.58, -122.30, 0.03);
    ps *= 1.f - soft_box(lat, lon, 47.428, 47.478, -122.325, -122.288, 0.008);
    land = std::min(land, 1.f - 0.72f * ps);

    land = std::max(land, smooth01(73.f, 77.f, static_cast<float>(lat)));
    land = std::max(land, 1.f - smooth01(-62.f, -56.f, static_cast<float>(lat)));
    return clampf(land, 0.f, 1.f);
}

} // namespace s376::geo
