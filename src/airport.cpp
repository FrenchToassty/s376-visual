#include "s376/airport.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace s376 {
namespace {

constexpr double kR = geo::kWgsA;

double wrap360(double a) {
    a = std::fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
}

void add_tri(AirportMesh& m, const geo::Ecef& a, const geo::Ecef& b, const geo::Ecef& c,
             const geo::Ecef& n, float r, float g, float bl, float mat = 0) {
    MeshVert v;
    v.n = n;
    v.r = r;
    v.g = g;
    v.b = bl;
    v.mat = mat;
    v.p = a;
    m.verts.push_back(v);
    v.p = b;
    m.verts.push_back(v);
    v.p = c;
    m.verts.push_back(v);
}

void strip_quad(AirportMesh& mesh, double lat0, double lon0, double lat1, double lon1,
                double heading_deg, double half_w, double elev0, double elev1, float r, float g,
                float b, float mat = 0) {
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
    add_tri(mesh, A, B, C, u, r, g, b, mat);
    add_tri(mesh, A, C, D, u, r, g, b, mat);
}

// Runway-frame rectangle. along/left in metres from (lat,lon) along hdg.
void mark_rect(AirportMesh& mesh, double lat, double lon, double hdg, double elev, double along0,
               double along1, double left, double right, float r, float g, float b) {
    double a0lat, a0lon, a1lat, a1lon;
    offset_m(lat, lon, hdg, along0, a0lat, a0lon);
    offset_m(lat, lon, hdg, along1, a1lat, a1lon);
    const double hw = std::fabs(right - left) * 0.5;
    const double mid = 0.5 * (left + right);
    double m0lat, m0lon, m1lat, m1lon;
    offset_m(a0lat, a0lon, hdg + 90.0, mid, m0lat, m0lon);
    offset_m(a1lat, a1lon, hdg + 90.0, mid, m1lat, m1lon);
    strip_quad(mesh, m0lat, m0lon, m1lat, m1lon, hdg, hw, elev, elev, r, g, b, 1.f);
}

const char* glyph5x7(char c) {
    switch (c) {
    case '0':
        return "01110100011000110001100011000101110";
    case '1':
        return "00100011000010000100001000010001110";
    case '2':
        return "01110100010000100110010001000011111";
    case '3':
        return "01110100010000100110000011000101110";
    case '4':
        return "00010001100101010010111110001000010";
    case '5':
        return "11111100001111000001000011000101110";
    case '6':
        return "01110100001111010001100011000101110";
    case '7':
        return "11111000010001000100010000100001000";
    case '8':
        return "01110100011000101110100011000101110";
    case '9':
        return "01110100011000101111000010000101110";
    case 'L':
        return "10000100001000010000100001000011111";
    case 'R':
        return "11110100011000111110101001001010001";
    case 'C':
        return "01110100011000010000100001000101110";
    default:
        return nullptr;
    }
}

void paint_glyph(AirportMesh& mesh, double lat, double lon, double hdg, double elev, double along,
                 double left, double tall_m, double wide_m, char ch) {
    const char* g = glyph5x7(ch);
    if (!g)
        return;
    const double pw = wide_m / 5.0;
    const double ph = tall_m / 7.0;
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if (g[row * 5 + col] != '1')
                continue;
            const double a0 = along + (6 - row) * ph;
            const double a1 = a0 + ph * 0.92;
            const double l0 = left + col * pw;
            mark_rect(mesh, lat, lon, hdg, elev, a0, a1, l0, l0 + pw * 0.88, 0.96f, 0.96f, 0.94f);
        }
    }
}

void paint_ident(AirportMesh& mesh, double lat, double lon, double hdg, double elev, double along,
                 const std::string& ident) {
    if (ident.empty())
        return;
    const double tall = 18.0;
    const double wide = 10.0;
    const double gap = 2.5;
    const double total = ident.size() * wide + (ident.size() - 1) * gap;
    double x = -total * 0.5;
    for (char ch : ident) {
        paint_glyph(mesh, lat, lon, hdg, elev, along, x, tall, wide, ch);
        x += wide + gap;
    }
}

void add_faa_markings(AirportMesh& mesh, const Runway& rw) {
    const double elev = std::max(rw.le_elev_m, rw.he_elev_m) + 0.48;
    const double half = rw.width_m * 0.5;
    // Edge + centerline once from the LE end.
    mark_rect(mesh, rw.le_lat, rw.le_lon, rw.heading_deg, elev, 0, rw.length_m, half - 1.1,
              half - 0.15, 0.95f, 0.95f, 0.93f);
    mark_rect(mesh, rw.le_lat, rw.le_lon, rw.heading_deg, elev, 0, rw.length_m, -(half - 0.15),
              -(half - 1.1), 0.95f, 0.95f, 0.93f);
    for (double d = 12; d < rw.length_m - 20; d += 60.0)
        mark_rect(mesh, rw.le_lat, rw.le_lon, rw.heading_deg, elev, d,
                  std::min(d + 36.0, rw.length_m - 8), -0.22, 0.22, 0.96f, 0.96f, 0.94f);

    auto one_end = [&](double lat, double lon, double hdg, const std::string& ident) {
        // Threshold bars: 8 (150 ft) or 12 (200 ft) stripes, 45 m × 1.8 m.
        const int nb = rw.width_m >= 55.0 ? 12 : 8;
        const double bar_w = 1.8;
        const double span = half - 2.0;
        for (int i = 0; i < nb; ++i) {
            const double t = (i + 0.5) / nb;
            const double x = -span + t * 2.0 * span;
            if (std::fabs(x) < 3.0)
                continue; // keep the centerline gap
            mark_rect(mesh, lat, lon, hdg, elev, 3.0, 48.0, x - bar_w * 0.5, x + bar_w * 0.5, 0.97f,
                      0.97f, 0.95f);
        }
        // Designators just past the bars (photo: big "4R").
        paint_ident(mesh, lat, lon, hdg, elev, 58.0, ident);
        // Aiming points ~400 m (or 300 m if short).
        const double aim = rw.length_m >= 2400.0 ? 400.0 : 300.0;
        if (rw.length_m > aim + 80.0) {
            mark_rect(mesh, lat, lon, hdg, elev, aim, aim + 45.0, 6.0, 15.0, 0.96f, 0.96f, 0.94f);
            mark_rect(mesh, lat, lon, hdg, elev, aim, aim + 45.0, -15.0, -6.0, 0.96f, 0.96f, 0.94f);
        }
        // TDZ pairs every 150 m for the first 900 m on instrument runways.
        for (int k = 1; k <= 5; ++k) {
            const double d = 150.0 * k;
            if (d + 20.0 >= rw.length_m * 0.45)
                break;
            mark_rect(mesh, lat, lon, hdg, elev, d, d + 22.0, 8.0, 14.0, 0.95f, 0.95f, 0.93f);
            mark_rect(mesh, lat, lon, hdg, elev, d, d + 22.0, -14.0, -8.0, 0.95f, 0.95f, 0.93f);
        }
    };
    one_end(rw.le_lat, rw.le_lon, rw.heading_deg, rw.ident_le);
    one_end(rw.he_lat, rw.he_lon, wrap360(rw.heading_deg + 180.0), rw.ident_he);
    // Rubber deposits in the first 900 m each end (darker asphalt, not a marking).
    strip_quad(mesh, rw.le_lat, rw.le_lon, rw.he_lat, rw.he_lon, rw.heading_deg, half * 0.35,
               rw.le_elev_m + 0.42, rw.he_elev_m + 0.42, 0.10f, 0.09f, 0.09f, 0.f);
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
        if (sys.empty() || sys == "NONE")
            return;
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

void ring_box(std::vector<std::pair<double, double>>& ring, double lat, double lon, double hdg,
              double len_m, double wid_m) {
    const double hl = len_m * 0.5, hw = wid_m * 0.5;
    const double corners[4][2] = {{-hl, -hw}, {hl, -hw}, {hl, hw}, {-hl, hw}};
    for (int i = 0; i < 4; ++i) {
        double p1lat, p1lon, p2lat, p2lon;
        offset_m(lat, lon, hdg, corners[i][0], p1lat, p1lon);
        offset_m(p1lat, p1lon, hdg + 90.0, corners[i][1], p2lat, p2lon);
        ring.push_back({p2lat, p2lon});
    }
}

Apron box_apron(const char* name, double lat, double lon, double hdg, double len_m, double wid_m,
                double elev_unused) {
    (void)elev_unused;
    Apron a;
    a.name = name;
    ring_box(a.ring, lat, lon, hdg, len_m, wid_m);
    return a;
}

Building box_building(const char* name, double lat, double lon, double hdg, double len_m,
                      double wid_m, float height_m) {
    Building b;
    b.name = name;
    b.height_m = height_m;
    ring_box(b.ring, lat, lon, hdg, len_m, wid_m);
    return b;
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
    a.aprons.push_back(box_apron("main", a.arp_lat, a.arp_lon + 0.004, 90.0, 800, 400, a.elev_m));
    a.buildings.push_back(box_building("main-term", a.arp_lat, a.arp_lon + 0.004, 90.0, 420, 90, 22.f));
    generate_lights(a);
    return a;
}

Airport ksfo_public() {
    Airport a;
    a.icao = "KSFO";
    a.name = "San Francisco International";
    a.attribution =
        "OurAirports runways.csv (public domain) + FAA charted ALS (ALSF-2 28R, "
        "MALSR 28L/19L/10L); taxi/apron schematic + OSM aeroway=* (ODbL) when fetched";
    a.arp_lat = 37.6188;
    a.arp_lon = -122.3754;
    a.elev_m = 13.0 * geo::kFtToM;
    a.runways.push_back(make_runway("10L", "28R", 37.62855, -122.39335, 13.0, 118.0, 11870.0, 200.0,
                                    "MALSR", "ALSF-2"));
    a.runways.push_back(make_runway("10R", "28L", 37.62540, -122.39250, 13.0, 118.0, 11381.0, 200.0,
                                    "MALSR", "MALSR"));
    a.runways.push_back(make_runway("01L", "19R", 37.61350, -122.38950, 11.0, 18.0, 7650.0, 200.0,
                                    "MALSR", "MALSR"));
    a.runways.push_back(make_runway("01R", "19L", 37.61180, -122.38680, 11.0, 18.0, 8650.0, 200.0,
                                    "MALSR", "MALSR"));
    a.taxiways.push_back(parallel_taxi("A", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("B", a.runways[1], -80.0));
    a.taxiways.push_back(parallel_taxi("C", a.runways[2], 70.0));
    a.aprons.push_back(box_apron("terminal", 37.6165, -122.3860, 30.0, 900, 500, a.elev_m));
    a.buildings.push_back(box_building("ITB", 37.6165, -122.3860, 30.0, 380, 80, 24.f));
    generate_lights(a);
    return a;
}

Airport klas_public() {
    Airport a;
    a.icao = "KLAS";
    a.name = "Harry Reid / McCarran International";
    a.attribution =
        "OurAirports runways.csv (public domain) + FAA charted ALS (ALSF-2 26R/26L, "
        "MALSR 19s/08s); taxi/apron schematic + OSM aeroway=* (ODbL) when fetched";
    a.arp_lat = 36.0801;
    a.arp_lon = -115.1523;
    a.elev_m = 2181.0 * geo::kFtToM;
    a.runways.push_back(make_runway("08L", "26R", 36.08620, -115.17450, 2181.0, 90.0, 14511.0, 150.0,
                                    "MALSR", "ALSF-2"));
    a.runways.push_back(make_runway("08R", "26L", 36.08050, -115.16800, 2181.0, 90.0, 10526.0, 150.0,
                                    "MALSR", "ALSF-2"));
    a.runways.push_back(make_runway("01L", "19R", 36.07400, -115.16300, 2171.0, 18.0, 8985.0, 150.0,
                                    "MALSR", "MALSR"));
    a.runways.push_back(make_runway("01R", "19L", 36.07200, -115.15800, 2171.0, 18.0, 9771.0, 150.0,
                                    "MALSR", "MALSR"));
    a.taxiways.push_back(parallel_taxi("A", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("B", a.runways[1], -80.0));
    a.taxiways.push_back(parallel_taxi("S", a.runways[2], 70.0));
    a.aprons.push_back(box_apron("D-gates", 36.0845, -115.1520, 90.0, 1100, 450, a.elev_m));
    a.buildings.push_back(box_building("D", 36.0845, -115.1520, 90.0, 500, 80, 20.f));
    generate_lights(a);
    return a;
}

Airport klax_public() {
    Airport a;
    a.icao = "KLAX";
    a.name = "Los Angeles International";
    a.attribution =
        "OurAirports runways.csv (public domain) + FAA charted ALS (ALSF-2 24R/24L/25R/25L, "
        "MALSR 06s/07s); taxi/apron schematic + OSM aeroway=* (ODbL) when fetched";
    a.arp_lat = 33.9425;
    a.arp_lon = -118.4081;
    a.elev_m = 128.0 * geo::kFtToM;
    a.runways.push_back(make_runway("06L", "24R", 33.95020, -118.43190, 117.0, 68.9, 8926.0, 150.0,
                                    "MALSR", "ALSF-2"));
    a.runways.push_back(make_runway("06R", "24L", 33.94700, -118.43140, 115.0, 68.9, 10285.0, 150.0,
                                    "MALSR", "ALSF-2"));
    a.runways.push_back(make_runway("07L", "25R", 33.93620, -118.41990, 115.0, 68.9, 12923.0, 150.0,
                                    "MALSR", "ALSF-2"));
    a.runways.push_back(make_runway("07R", "25L", 33.93350, -118.41870, 102.0, 68.9, 11095.0, 200.0,
                                    "MALSR", "ALSF-2"));
    a.taxiways.push_back(parallel_taxi("B", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("C", a.runways[1], -80.0));
    a.taxiways.push_back(parallel_taxi("S", a.runways[2], 70.0));
    a.aprons.push_back(box_apron("TBIT", 33.9435, -118.4085, 69.0, 1200, 500, a.elev_m));
    a.buildings.push_back(box_building("TBIT", 33.9435, -118.4085, 69.0, 520, 90, 28.f));
    generate_lights(a);
    return a;
}

Airport kjfk_public() {
    Airport a;
    a.icao = "KJFK";
    a.name = "John F Kennedy International";
    a.attribution =
        "OurAirports runways.csv (public domain) + FAA charted ALS (ALSF-2 04L/22R/13L/13R/31L, "
        "MALSR 04R/31R); taxi/apron schematic + OSM aeroway=* (ODbL) when fetched";
    a.arp_lat = 40.6399;
    a.arp_lon = -73.7787;
    a.elev_m = 13.0 * geo::kFtToM;
    a.runways.push_back(make_runway("04L", "22R", 40.62205, -73.78558, 12.0, 31.0, 12079.0, 200.0,
                                    "ALSF-2", "ALSF-2"));
    a.runways.push_back(make_runway("04R", "22L", 40.62524, -73.77626, 12.0, 31.0, 8400.0, 200.0,
                                    "MALSR", "ALSF-2"));
    a.runways.push_back(make_runway("13L", "31R", 40.64822, -73.79026, 12.0, 121.0, 10000.0, 150.0,
                                    "ALSF-2", "MALSR"));
    a.runways.push_back(make_runway("13R", "31L", 40.64096, -73.78864, 13.0, 121.0, 14511.0, 200.0,
                                    "ALSF-2", "ALSF-2"));
    a.taxiways.push_back(parallel_taxi("A", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("B", a.runways[1], -80.0));
    a.taxiways.push_back(parallel_taxi("Z", a.runways[3], 80.0));
    a.aprons.push_back(box_apron("T4", 40.6445, -73.7780, 40.0, 1000, 450, a.elev_m));
    a.buildings.push_back(box_building("T4", 40.6445, -73.7780, 40.0, 380, 80, 22.f));
    a.buildings.push_back(box_building("T5", 40.6410, -73.7870, 121.0, 280, 70, 18.f));
    a.buildings.push_back(box_building("T8", 40.6468, -73.7895, 121.0, 260, 70, 20.f));
    generate_lights(a);
    return a;
}

Airport kbos_public() {
    Airport a;
    a.icao = "KBOS";
    a.name = "Boston Logan International";
    a.attribution =
        "OurAirports runways.csv (public domain) + FAA charted ALS (ALSF-2 04R/15R, "
        "MALSR 04L/33L/27); taxi/apron schematic + OSM aeroway=* / building=* (ODbL)";
    a.arp_lat = 42.3656;
    a.arp_lon = -71.0096;
    a.elev_m = 20.0 * geo::kFtToM;
    a.runways.push_back(make_runway("04L", "22R", 42.35157, -71.01625, 19.0, 35.0, 7864.0, 150.0,
                                    "MALSR", "MALSR"));
    a.runways.push_back(make_runway("04R", "22L", 42.35106, -71.01153, 19.0, 35.0, 10006.0, 150.0,
                                    "ALSF-2", "MALSR"));
    a.runways.push_back(
        make_runway("09", "27", 42.36056, -71.02361, 19.0, 93.0, 7001.0, 150.0, "MALSR", "MALSR"));
    a.runways.push_back(make_runway("15R", "33L", 42.37444, -71.02389, 16.0, 150.0, 10083.0, 150.0,
                                    "ALSF-2", "MALSR"));
    a.runways.push_back(make_runway("15L", "33R", 42.37278, -71.01694, 16.0, 150.0, 2557.0, 100.0,
                                    "NONE", "NONE"));
    a.taxiways.push_back(parallel_taxi("N", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("E", a.runways[1], -80.0));
    a.aprons.push_back(box_apron("A-gates", 42.3668, -71.0205, 50.0, 900, 420, a.elev_m));
    a.buildings.push_back(box_building("A", 42.3668, -71.0205, 50.0, 360, 80, 22.f));
    a.buildings.push_back(box_building("B", 42.3642, -71.0178, 50.0, 240, 70, 18.f));
    a.buildings.push_back(box_building("E", 42.3695, -71.0228, 150.0, 280, 70, 20.f));
    generate_lights(a);
    return a;
}

Airport airport_by_icao(const std::string& icao) {
    std::string id = icao;
    for (char& c : id)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (id == "KSEA")
        return ksea_public();
    if (id == "KSFO")
        return ksfo_public();
    if (id == "KLAS")
        return klas_public();
    if (id == "KLAX")
        return klax_public();
    if (id == "KJFK")
        return kjfk_public();
    if (id == "KBOS")
        return kbos_public();
    throw std::runtime_error("no built-in layout for " + icao);
}

std::vector<Airport> all_public_airports() {
    return {ksea_public(), ksfo_public(), klas_public(), klax_public(), kjfk_public(),
            kbos_public()};
}

bool runway_shot_pose(const Airport& a, const std::string& ident, double dist_nm, ShotPose& out) {
    std::string id = ident;
    for (char& c : id)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const auto& rw : a.runways) {
        const bool le = rw.ident_le == id;
        const bool he = rw.ident_he == id;
        if (!le && !he)
            continue;
        const double lat0 = le ? rw.le_lat : rw.he_lat;
        const double lon0 = le ? rw.le_lon : rw.he_lon;
        const double hdg = le ? rw.heading_deg : wrap360(rw.heading_deg + 180.0);
        const double elev_ft = (le ? rw.le_elev_m : rw.he_elev_m) * geo::kMToFt;
        out.icao = a.icao;
        out.rwy = id;
        out.dist_nm = dist_nm;
        out.hdg_true_deg = hdg;
        if (dist_nm <= 0.01) {
            // Lined up looking at the designators (threshold bars + 4R ahead).
            offset_m(lat0, lon0, hdg, 12.0, out.lat, out.lon);
            out.alt_ft = elev_ft + 14.0;
            out.pitch_deg = 0;
        } else {
            const double back = wrap360(hdg + 180.0);
            offset_m(lat0, lon0, back, dist_nm * 1852.0, out.lat, out.lon);
            out.alt_ft = elev_ft + 14.0 + dist_nm * 318.0; // 3°
            out.pitch_deg = -3.0;
        }
        return true;
    }
    return false;
}

ShotPose parse_shot_at(const std::string& spec) {
    std::string s = spec;
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    ShotPose p;
    const auto c1 = s.find(':');
    if (c1 == std::string::npos) {
        p.icao = s;
        const Airport a = airport_by_icao(p.icao);
        p.lat = a.arp_lat;
        p.lon = a.arp_lon;
        p.alt_ft = a.elev_m * geo::kMToFt + 50.0;
        p.hdg_true_deg = a.runways.empty() ? 0.0 : a.runways[0].heading_deg;
        p.pitch_deg = 0;
        p.roll_deg = 0;
        return p;
    }
    p.icao = s.substr(0, c1);
    const auto c2 = s.find(':', c1 + 1);
    p.rwy = c2 == std::string::npos ? s.substr(c1 + 1) : s.substr(c1 + 1, c2 - c1 - 1);
    p.dist_nm = 0;
    if (c2 != std::string::npos)
        p.dist_nm = std::stod(s.substr(c2 + 1));
    const Airport a = airport_by_icao(p.icao);
    if (!runway_shot_pose(a, p.rwy, p.dist_nm, p))
        throw std::runtime_error("no runway " + p.rwy + " at " + p.icao);
    return p;
}

void apply_shot_overrides(ShotPose& p, const ShotOverrides& o) {
    if (o.have_hdg)
        p.hdg_true_deg = wrap360(o.hdg_deg);
    if (o.have_alt)
        p.alt_ft = o.alt_ft;
    if (o.have_bank)
        p.roll_deg = o.bank_deg;
    if (o.have_pitch)
        p.pitch_deg = o.pitch_deg;
    // ICAO-only + high alt: stand off opposite heading so the field is ahead
    // (e.g. --at KJFK --alt 12000 --hdg 036).
    if (p.rwy.empty() && o.have_alt && p.alt_ft > 400.0) {
        const Airport a = airport_by_icao(p.icao);
        const double elev_ft = a.elev_m * geo::kMToFt;
        const double agl = p.alt_ft - elev_ft;
        if (agl > 400.0) {
            const double back_nm = std::min(18.0, agl / 800.0);
            offset_m(a.arp_lat, a.arp_lon, wrap360(p.hdg_true_deg + 180.0), back_nm * 1852.0, p.lat,
                     p.lon);
            if (!o.have_pitch)
                p.pitch_deg = -std::min(12.0, agl / 1400.0);
        }
    }
}

std::string overpass_aeroway_query(double south, double west, double north, double east) {
    std::ostringstream o;
    o << "[out:json][timeout:40];(way[\"aeroway\"=\"taxiway\"](" << south << "," << west << ","
      << north << "," << east << ");way[\"aeroway\"=\"apron\"](" << south << "," << west << ","
      << north << "," << east << ");way[\"building\"](" << south << "," << west << "," << north
      << "," << east << ");way[\"natural\"=\"water\"](" << south << "," << west << "," << north
      << "," << east << ");way[\"natural\"=\"coastline\"](" << south << "," << west << "," << north
      << "," << east << "););out geom;";
    return o.str();
}

std::string overpass_aeroway_url(double south, double west, double north, double east) {
    const std::string q = overpass_aeroway_query(south, west, north, east);
    std::ostringstream o;
    o << "https://overpass-api.de/api/interpreter?data=";
    for (unsigned char c : q) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '=' ||
            c == ';' || c == ',' || c == '(' || c == ')' || c == '/' || c == ':') {
            o << c;
        } else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            o << buf;
        }
    }
    return o.str();
}

int merge_osm_json(Airport& a, const std::string& json) {
    int added = 0;
    std::size_t pos = 0;
    while (true) {
        auto g = json.find("\"geometry\"", pos);
        if (g == std::string::npos)
            break;
        std::string kind = "taxiway";
        float bldg_h = 12.f;
        const auto window = json.rfind("{", g);
        const std::string tags =
            window != std::string::npos && g > window ? json.substr(window, g - window) : "";
        if (tags.find("\"building\"") != std::string::npos)
            kind = "building";
        else if (tags.find("apron") != std::string::npos)
            kind = "apron";
        else if (tags.find("\"water\"") != std::string::npos ||
                 tags.find("coastline") != std::string::npos)
            kind = "skip";
        auto read_num = [&](std::size_t key) -> float {
            auto colon = tags.find(':', key);
            if (colon == std::string::npos)
                return 0.f;
            const char* p = tags.c_str() + colon + 1;
            while (*p == ' ' || *p == '\t' || *p == '"')
                ++p;
            return static_cast<float>(std::strtod(p, nullptr));
        };
        auto hp = tags.find("\"height\"");
        if (hp != std::string::npos) {
            bldg_h = read_num(hp);
        } else {
            auto lp = tags.find("building:levels");
            if (lp != std::string::npos)
                bldg_h = read_num(lp) * 3.5f;
        }
        if (bldg_h < 3.f)
            bldg_h = 12.f;
        auto arr = json.find('[', g);
        if (arr == std::string::npos)
            break;
        int depth = 0;
        std::size_t end = arr;
        for (; end < json.size(); ++end) {
            if (json[end] == '[')
                ++depth;
            else if (json[end] == ']') {
                --depth;
                if (depth == 0)
                    break;
            }
        }
        const std::string geom = json.substr(arr, end - arr + 1);
        std::vector<std::pair<double, double>> pts;
        std::size_t p = 0;
        while (true) {
            auto latp = geom.find("\"lat\"", p);
            auto lonp = geom.find("\"lon\"", p);
            if (latp == std::string::npos || lonp == std::string::npos)
                break;
            auto lat_colon = geom.find(':', latp);
            auto lon_colon = geom.find(':', lonp);
            if (lat_colon == std::string::npos || lon_colon == std::string::npos)
                break;
            pts.push_back({std::strtod(geom.c_str() + lat_colon + 1, nullptr),
                           std::strtod(geom.c_str() + lon_colon + 1, nullptr)});
            p = std::max(latp, lonp) + 5;
        }
        if (pts.size() >= 2 && kind != "skip") {
            if (kind == "building") {
                Building b;
                b.name = "osm";
                b.ring = std::move(pts);
                b.height_m = bldg_h;
                a.buildings.push_back(std::move(b));
            } else if (kind == "apron") {
                Apron ap;
                ap.name = "osm-apron";
                ap.ring = std::move(pts);
                a.aprons.push_back(std::move(ap));
            } else {
                Taxiway t;
                t.name = "osm";
                t.path = std::move(pts);
                a.taxiways.push_back(std::move(t));
            }
            ++added;
        }
        pos = end + 1;
    }
    return added;
}

int try_merge_osm_file(Airport& a, const std::string& path) {
    if (path.empty() || !std::filesystem::exists(path) || !std::filesystem::is_regular_file(path))
        return 0;
    std::ifstream in(path);
    if (!in)
        return 0;
    const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return merge_osm_json(a, json);
}

int try_load_osm(Airport& a, const std::string& cache_dir) {
    std::string id = a.icao;
    for (char& c : id)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::vector<std::string> paths;
    if (!cache_dir.empty()) {
        paths.push_back(cache_dir + "/osm.json");
        const auto parent = std::filesystem::path(cache_dir).parent_path();
        if (!parent.empty())
            paths.push_back((parent / id / "osm.json").string());
    }
    paths.push_back("cache/" + id + "/osm.json");
    paths.push_back(std::string("data/airports/") + id + ".osm.json");
    for (const auto& p : paths) {
        const int n = try_merge_osm_file(a, p);
        if (n > 0)
            return n;
    }
    return 0;
}

void generate_lights(Airport& a) {
    a.lights.clear();
    for (const auto& rw : a.runways)
        lights_for_runway(a, rw);
}

std::string airport_geojson(const Airport& a) {
    std::ostringstream o;
    o << std::fixed;
    o << "{\"type\":\"FeatureCollection\",\"name\":\"" << a.icao
      << "\",\"attribution\":\"" << a.attribution << "\",\"features\":[";
    bool first = true;
    auto feat = [&](const char* kind, const char* name,
                    const std::vector<std::pair<double, double>>& pts, bool ring) {
        if (pts.size() < 2)
            return;
        if (!first)
            o << ',';
        first = false;
        o << "{\"type\":\"Feature\",\"properties\":{\"aeroway\":\"" << kind << "\",\"name\":\""
          << name << "\"},\"geometry\":{\"type\":\"" << (ring ? "Polygon" : "LineString")
          << "\",\"coordinates\":";
        if (ring)
            o << '[';
        o << '[';
        for (std::size_t i = 0; i < pts.size(); ++i) {
            if (i)
                o << ',';
            o << '[' << pts[i].second << ',' << pts[i].first << ']';
        }
        if (ring && (pts.front().first != pts.back().first || pts.front().second != pts.back().second))
            o << ",[" << pts.front().second << ',' << pts.front().first << ']';
        o << ']';
        if (ring)
            o << ']';
        o << "}}";
    };
    for (const auto& rw : a.runways) {
        const std::string nm = rw.ident_le + "/" + rw.ident_he;
        std::vector<std::pair<double, double>> line = {{rw.le_lat, rw.le_lon}, {rw.he_lat, rw.he_lon}};
        feat("runway", nm.c_str(), line, false);
    }
    for (const auto& tx : a.taxiways)
        feat("taxiway", tx.name.c_str(), tx.path, false);
    for (const auto& ap : a.aprons)
        feat("apron", ap.name.c_str(), ap.ring, true);
    o << "]}";
    return o.str();
}

int write_airport_geojson(const Airport& a, const std::string& path) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);
    std::ofstream out(path);
    if (!out)
        return 0;
    out << airport_geojson(a);
    return out ? 1 : 0;
}

AirportMesh build_airport_mesh(const Airport& a, bool buildings, bool markings) {
    AirportMesh mesh;
    for (const auto& rw : a.runways) {
        strip_quad(mesh, rw.le_lat, rw.le_lon, rw.he_lat, rw.he_lon, rw.heading_deg,
                   rw.width_m * 0.5, rw.le_elev_m + 0.4, rw.he_elev_m + 0.4, 0.16f, 0.16f, 0.17f,
                   0.f);
        if (markings)
            add_faa_markings(mesh, rw);
    }
    for (const auto& ap : a.aprons) {
        if (ap.ring.size() < 3)
            continue;
        double clat = 0, clon = 0;
        for (const auto& p : ap.ring) {
            clat += p.first;
            clon += p.second;
        }
        clat /= static_cast<double>(ap.ring.size());
        clon /= static_cast<double>(ap.ring.size());
        const geo::Ecef C = geo::lla_to_ecef(clat, clon, a.elev_m + 0.3);
        geo::Ecef e, n, u;
        geo::ecef_to_enu_axes(clat, clon, e, n, u);
        for (std::size_t i = 0; i < ap.ring.size(); ++i) {
            const auto& p0 = ap.ring[i];
            const auto& p1 = ap.ring[(i + 1) % ap.ring.size()];
            add_tri(mesh, C, geo::lla_to_ecef(p0.first, p0.second, a.elev_m + 0.3),
                    geo::lla_to_ecef(p1.first, p1.second, a.elev_m + 0.3), u, 0.28f, 0.27f, 0.25f,
                    2.f);
        }
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
                       a.elev_m + 0.35, 0.22f, 0.22f, 0.20f, 2.f);
            if (markings) {
                mark_rect(mesh, lat0, lon0, hdg, a.elev_m + 0.46, 78.0, 79.2, -tx.width_m * 0.45,
                          tx.width_m * 0.45, 0.92f, 0.78f, 0.12f);
                mark_rect(mesh, lat0, lon0, hdg, a.elev_m + 0.46, 80.4, 81.6, -tx.width_m * 0.45,
                          tx.width_m * 0.45, 0.92f, 0.78f, 0.12f);
            }
        }
    }
    constexpr int kMaxBuildings = 280;
    int nb = 0;
    for (const auto& b : a.buildings) {
        if (!buildings || nb >= kMaxBuildings)
            break;
        if (b.ring.size() < 3)
            continue;
        double clat = 0, clon = 0;
        for (const auto& p : b.ring) {
            clat += p.first;
            clon += p.second;
        }
        clat /= static_cast<double>(b.ring.size());
        clon /= static_cast<double>(b.ring.size());
        const float h = std::max(6.f, std::min(b.height_m, 80.f));
        const double z0 = a.elev_m + 0.5;
        const double z1 = z0 + h;
        geo::Ecef e, n, u;
        geo::ecef_to_enu_axes(clat, clon, e, n, u);
        const geo::Ecef roof_c = geo::lla_to_ecef(clat, clon, z1);
        for (std::size_t i = 0; i < b.ring.size(); ++i) {
            const auto& p0 = b.ring[i];
            const auto& p1 = b.ring[(i + 1) % b.ring.size()];
            const geo::Ecef A = geo::lla_to_ecef(p0.first, p0.second, z0);
            const geo::Ecef B = geo::lla_to_ecef(p1.first, p1.second, z0);
            const geo::Ecef C = geo::lla_to_ecef(p1.first, p1.second, z1);
            const geo::Ecef D = geo::lla_to_ecef(p0.first, p0.second, z1);
            geo::Ecef wn{B.x - A.x, B.y - A.y, B.z - A.z};
            // Cheap wall normal: cross(edge, up)
            geo::Ecef nn{wn.y * u.z - wn.z * u.y, wn.z * u.x - wn.x * u.z, wn.x * u.y - wn.y * u.x};
            add_tri(mesh, A, B, C, nn, 0.62f, 0.58f, 0.52f, 3.f);
            add_tri(mesh, A, C, D, nn, 0.62f, 0.58f, 0.52f, 3.f);
            add_tri(mesh, roof_c, D, C, u, 0.28f, 0.30f, 0.32f, 4.f);
        }
        ++nb;
    }
    return mesh;
}

} // namespace s376
