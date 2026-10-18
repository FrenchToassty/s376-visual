#include "s376/airport.hpp"

#include <algorithm>
#include <cmath>

namespace s376 {
namespace {

constexpr double kR = geo::kWgsA;

double wrap360(double a) {
    a = std::fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
}

void add_tri(AirportMesh& m, const geo::Ecef& a, const geo::Ecef& b, const geo::Ecef& c,
             const geo::Ecef& n, float r, float g, float bl) {
    MeshVert v;
    v.n = n;
    v.r = r;
    v.g = g;
    v.b = bl;
    v.p = a;
    m.verts.push_back(v);
    v.p = b;
    m.verts.push_back(v);
    v.p = c;
    m.verts.push_back(v);
}

void strip_quad(AirportMesh& mesh, double lat0, double lon0, double lat1, double lon1,
                double heading_deg, double half_w, double elev0, double elev1, float r, float g,
                float b) {
    double l0lat, l0lon, r0lat, r0lon, l1lat, l1lon, r1lat, r1lon;
    offset_m(lat0, lon0, heading_deg - 90.0, half_w, l0lat, l0lon);
    offset_m(lat0, lon0, heading_deg + 90.0, half_w, r0lat, r0lon);
    offset_m(lat1, lon1, heading_deg - 90.0, half_w, l1lat, l1lon);
    offset_m(lat1, lon1, heading_deg + 90.0, half_w, r1lat, r1lon);
    const geo::Ecef A = geo::lla_to_ecef(l0lat, l0lon, elev0);
    const geo::Ecef B = geo::lla_to_ecef(r0lat, r0lon, elev0);
    const geo::Ecef C = geo::lla_to_ecef(r1lat, r1lon, elev1);
    const geo::Ecef D = geo::lla_to_ecef(l1lat, l1lon, elev1);
    geo::Ecef e, n, u;
    geo::ecef_to_enu_axes((lat0 + lat1) * 0.5, (lon0 + lon1) * 0.5, e, n, u);
    add_tri(mesh, A, B, C, u, r, g, b);
    add_tri(mesh, A, C, D, u, r, g, b);
}

void push_light(Airport& a, double lat, double lon, double alt_m, float r, float g, float b,
                int sw, LightKind kind) {
    LightPoint p;
    p.lat = lat;
    p.lon = lon;
    p.alt_m = alt_m + 0.6;
    p.r = r;
    p.g = g;
    p.b = b;
    p.switch_num = sw;
    p.kind = kind;
    a.lights.push_back(p);
}

void bar(Airport& a, double lat, double lon, double hdg, double alt_m, double half_span, int n,
         float r, float g, float b, int sw, LightKind kind) {
    if (n < 1)
        return;
    for (int i = 0; i < n; ++i) {
        const double t = n == 1 ? 0.0 : (i / static_cast<double>(n - 1) - 0.5) * 2.0;
        double plat, plon;
        offset_m(lat, lon, hdg + 90.0, t * half_span, plat, plon);
        push_light(a, plat, plon, alt_m, r, g, b, sw, kind);
    }
}

void lights_for_runway(Airport& a, const Runway& rw) {
    const double step_edge = 60.0;
    const double step_cl = 15.0;
    const int n_edge = std::max(2, static_cast<int>(std::floor(rw.length_m / step_edge)));
    for (int i = 0; i <= n_edge; ++i) {
        const double d = rw.length_m * i / n_edge;
        const bool last = d > rw.length_m - 600.0 || d < 600.0;
        const float er = last ? 1.f : 1.f;
        const float eg = last ? 0.78f : 1.f;
        const float eb = last ? 0.15f : 0.85f;
        double clat, clon;
        offset_m(rw.le_lat, rw.le_lon, rw.heading_deg, d, clat, clon);
        const double elev = rw.le_elev_m + (rw.he_elev_m - rw.le_elev_m) * (d / rw.length_m);
        double llat, llon, rlat, rlon;
        offset_m(clat, clon, rw.heading_deg - 90.0, rw.width_m * 0.5, llat, llon);
        offset_m(clat, clon, rw.heading_deg + 90.0, rw.width_m * 0.5, rlat, rlon);
        push_light(a, llat, llon, elev, er, eg, eb, a.sw_edge, LightKind::Edge);
        push_light(a, rlat, rlon, elev, er, eg, eb, a.sw_edge, LightKind::Edge);
    }

    const int n_cl = std::max(2, static_cast<int>(std::floor(rw.length_m / step_cl)));
    for (int i = 0; i <= n_cl; ++i) {
        const double d = rw.length_m * i / n_cl;
        double clat, clon;
        offset_m(rw.le_lat, rw.le_lon, rw.heading_deg, d, clat, clon);
        const double elev = rw.le_elev_m + (rw.he_elev_m - rw.le_elev_m) * (d / rw.length_m);
        const bool red = d > rw.length_m - 300.0;
        push_light(a, clat, clon, elev, 1.f, red ? 0.2f : 1.f, red ? 0.15f : 0.95f, a.sw_centerline,
                   LightKind::Centerline);
    }

    bar(a, rw.le_lat, rw.le_lon, rw.heading_deg, rw.le_elev_m, rw.width_m * 0.5, 8, 0.15f, 1.f,
        0.25f, a.sw_threshold, LightKind::Threshold);
    bar(a, rw.he_lat, rw.he_lon, rw.heading_deg, rw.he_elev_m, rw.width_m * 0.5, 8, 1.f, 0.12f,
        0.1f, a.sw_threshold, LightKind::Threshold);

    auto approach = [&](double tlat, double tlon, double thdg, double telev, const std::string& sys) {
        const double back = wrap360(thdg + 180.0);
        const double reach = (sys == "ALSF-2") ? 732.0 : 427.0; // 2400 / 1400 ft
        const double step = 30.0;
        const int n = std::max(2, static_cast<int>(std::floor(reach / step)));
        for (int i = 1; i <= n; ++i) {
            const double d = reach * i / n;
            double plat, plon;
            offset_m(tlat, tlon, back, d, plat, plon);
            push_light(a, plat, plon, telev, 1.f, 1.f, 0.92f, a.sw_approach, LightKind::Approach);
        }
        double xlat, xlon;
        offset_m(tlat, tlon, back, std::min(305.0, reach * 0.7), xlat, xlon);
        bar(a, xlat, xlon, thdg, telev, 18.0, 5, 1.f, 1.f, 0.92f, a.sw_approach,
            LightKind::Approach);
    };
    approach(rw.le_lat, rw.le_lon, rw.heading_deg, rw.le_elev_m, rw.approach_le);
    approach(rw.he_lat, rw.he_lon, wrap360(rw.heading_deg + 180.0), rw.he_elev_m, rw.approach_he);
}

Runway make_runway(const char* le, const char* he, double le_lat, double le_lon, double le_elev_ft,
                   double heading, double length_ft, double width_ft, const char* als_le,
                   const char* als_he) {
    Runway r;
    r.ident_le = le;
    r.ident_he = he;
    r.le_lat = le_lat;
    r.le_lon = le_lon;
    r.le_elev_m = le_elev_ft * geo::kFtToM;
    r.heading_deg = heading;
    r.length_m = length_ft * geo::kFtToM;
    r.width_m = width_ft * geo::kFtToM;
    r.he_elev_m = r.le_elev_m;
    r.approach_le = als_le;
    r.approach_he = als_he;
    offset_m(r.le_lat, r.le_lon, r.heading_deg, r.length_m, r.he_lat, r.he_lon);
    return r;
}

Taxiway parallel_taxi(const char* name, const Runway& rw, double perp_m) {
    Taxiway t;
    t.name = name;
    t.width_m = 23;
    double a_lat, a_lon, b_lat, b_lon;
    offset_m(rw.le_lat, rw.le_lon, rw.heading_deg + 90.0, perp_m, a_lat, a_lon);
    offset_m(rw.he_lat, rw.he_lon, rw.heading_deg + 90.0, perp_m, b_lat, b_lon);
    t.path = {{a_lat, a_lon}, {b_lat, b_lon}};
    return t;
}

} // namespace

void offset_m(double lat, double lon, double heading_deg, double dist_m, double& olat,
              double& olon) {
    const double n = dist_m * std::cos(geo::deg2rad(heading_deg));
    const double e = dist_m * std::sin(geo::deg2rad(heading_deg));
    olat = lat + geo::rad2deg(n / kR);
    const double cl = std::max(std::cos(geo::deg2rad(lat)), 1e-6);
    olon = lon + geo::rad2deg(e / (kR * cl));
}

const char* light_kind_name(LightKind k) {
    switch (k) {
    case LightKind::Edge:
        return "edge";
    case LightKind::Centerline:
        return "centerline";
    case LightKind::Approach:
        return "approach";
    case LightKind::Threshold:
        return "threshold";
    }
    return "?";
}

Airport ksea_public() {
    Airport a;
    a.icao = "KSEA";
    a.name = "Seattle-Tacoma International";
    a.attribution =
        "OurAirports runways.csv (public domain) + FAA charted ALS; "
        "taxi connectors schematic from runway geometry (ODbL OSM aeroway=* later)";
    a.arp_lat = 47.4502;
    a.arp_lon = -122.3088;
    a.elev_m = 433.0 * geo::kFtToM;
    // 16L threshold matches s376-synth. Lengths/widths from OurAirports.
    a.runways.push_back(make_runway("16L", "34R", 47.463684, -122.307751, 429.0, 180.34, 11901.0,
                                    150.0, "ALSF-2", "MALSR"));
    a.runways.push_back(make_runway("16C", "34C", 47.45583, -122.31155, 429.0, 180.3, 9426.0, 150.0,
                                    "MALSR", "MALSR"));
    a.runways.push_back(make_runway("16R", "34L", 47.45349, -122.31775, 429.0, 180.3, 8500.0, 150.0,
                                    "MALSR", "MALSR"));
    a.taxiways.push_back(parallel_taxi("T", a.runways[0], 90.0));
    a.taxiways.push_back(parallel_taxi("B", a.runways[1], 80.0));
    generate_lights(a);
    return a;
}

void generate_lights(Airport& a) {
    a.lights.clear();
    for (const auto& rw : a.runways)
        lights_for_runway(a, rw);
}

AirportMesh build_airport_mesh(const Airport& a) {
    AirportMesh mesh;
    for (const auto& rw : a.runways) {
        strip_quad(mesh, rw.le_lat, rw.le_lon, rw.he_lat, rw.he_lon, rw.heading_deg,
                   rw.width_m * 0.5, rw.le_elev_m + 0.4, rw.he_elev_m + 0.4, 0.16f, 0.16f, 0.17f);
        strip_quad(mesh, rw.le_lat, rw.le_lon, rw.he_lat, rw.he_lon, rw.heading_deg, 0.6,
                   rw.le_elev_m + 0.45, rw.he_elev_m + 0.45, 0.75f, 0.75f, 0.70f);
    }
    for (const auto& tx : a.taxiways) {
        if (tx.path.size() < 2)
            continue;
        for (std::size_t i = 0; i + 1 < tx.path.size(); ++i) {
            const double lat0 = tx.path[i].first, lon0 = tx.path[i].second;
            const double lat1 = tx.path[i + 1].first, lon1 = tx.path[i + 1].second;
            const double dlat = (lat1 - lat0) * geo::kDeg * kR;
            const double dlon =
                (lon1 - lon0) * geo::kDeg * kR * std::cos(geo::deg2rad((lat0 + lat1) * 0.5));
            const double hdg = wrap360(geo::rad2deg(std::atan2(dlon, dlat)));
            strip_quad(mesh, lat0, lon0, lat1, lon1, hdg, tx.width_m * 0.5, a.elev_m + 0.35,
                       a.elev_m + 0.35, 0.22f, 0.22f, 0.20f);
        }
    }
    return mesh;
}

} // namespace s376
