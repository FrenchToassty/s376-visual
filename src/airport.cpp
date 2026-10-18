#include "s376/airport.hpp"
#include "s376/faa.hpp"
#include "s376/lights.hpp"
#include "s376/traffic.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

    auto one_end = [&](double lat, double lon, double hdg, const std::string& ident,
                       double disp_ft) {
        const double disp_m = disp_ft * geo::kFtToM;
        if (disp_m > 8.0) {
            // Yellow chevrons in the displaced area (official THR, not old paint).
            for (double d = 12.0; d < disp_m - 8.0; d += 24.0) {
                mark_rect(mesh, lat, lon, hdg, elev, d, d + 10.0, 4.0, 14.0, 0.92f, 0.78f, 0.12f);
                mark_rect(mesh, lat, lon, hdg, elev, d, d + 10.0, -14.0, -4.0, 0.92f, 0.78f, 0.12f);
            }
            offset_m(lat, lon, hdg, disp_m, lat, lon);
        }
        // Threshold bars at the landing THR: 8 (150 ft) or 12 (200 ft).
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
    one_end(rw.le_lat, rw.le_lon, rw.heading_deg, rw.ident_le, rw.le_displaced_ft);
    one_end(rw.he_lat, rw.he_lon, wrap360(rw.heading_deg + 180.0), rw.ident_he, rw.he_displaced_ft);
    // Rubber deposits in the first 900 m each end (darker asphalt, not a marking).
    strip_quad(mesh, rw.le_lat, rw.le_lon, rw.he_lat, rw.he_lon, rw.heading_deg, half * 0.35,
               rw.le_elev_m + 0.42, rw.he_elev_m + 0.42, 0.10f, 0.09f, 0.09f, 0.f);
}

void push_light(Airport& a, double lat, double lon, double alt_m, float r, float g, float b,
                int sw, LightKind kind, int runway_k = 0, int seq = -1) {
    LightPoint p;
    p.lat = lat;
    p.lon = lon;
    p.alt_m = alt_m + 0.6;
    p.r = r;
    p.g = g;
    p.b = b;
    p.switch_num = sw;
    p.kind = kind;
    p.runway_k = runway_k;
    p.seq = seq;
    a.lights.push_back(p);
}

void bar(Airport& a, double lat, double lon, double hdg, double alt_m, double half_span, int n,
         float r, float g, float b, int sw, LightKind kind, int runway_k = 0) {
    if (n < 1)
        return;
    for (int i = 0; i < n; ++i) {
        const double t = n == 1 ? 0.0 : (i / static_cast<double>(n - 1) - 0.5) * 2.0;
        double plat, plon;
        offset_m(lat, lon, hdg + 90.0, t * half_span, plat, plon);
        push_light(a, plat, plon, alt_m, r, g, b, sw, kind, runway_k);
    }
}

void lights_for_runway(Airport& a, const Runway& rw, int k) {
    const int sw_app_a = lights::runway_switch(k, lights::kOffApproachA);
    const int sw_strobe_a = lights::runway_switch(k, lights::kOffStrobeA);
    const int sw_edge = lights::runway_switch(k, lights::kOffEdge);
    const int sw_cl = lights::runway_switch(k, lights::kOffCenterline);
    const int sw_vasi = lights::runway_switch(k, lights::kOffVasi);
    const int sw_tdz = lights::runway_switch(k, lights::kOffTdz);
    const int sw_app_b = lights::runway_switch(k, lights::kOffApproachB);
    const int sw_strobe_b = lights::runway_switch(k, lights::kOffStrobeB);
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
        push_light(a, llat, llon, elev, er, eg, eb, sw_edge, LightKind::Edge, k);
        push_light(a, rlat, rlon, elev, er, eg, eb, sw_edge, LightKind::Edge, k);
    }

    const int n_cl = std::max(2, static_cast<int>(std::floor(rw.length_m / step_cl)));
    for (int i = 0; i <= n_cl; ++i) {
        const double d = rw.length_m * i / n_cl;
        double clat, clon;
        offset_m(rw.le_lat, rw.le_lon, rw.heading_deg, d, clat, clon);
        const double elev = rw.le_elev_m + (rw.he_elev_m - rw.le_elev_m) * (d / rw.length_m);
        const bool red = d > rw.length_m - 300.0;
        push_light(a, clat, clon, elev, 1.f, red ? 0.2f : 1.f, red ? 0.15f : 0.95f, sw_cl,
                   LightKind::Centerline, k);
    }

    auto landing = [&](bool le, double& lat, double& lon, double& hdg, double& elev, double& disp_m) {
        lat = le ? rw.le_lat : rw.he_lat;
        lon = le ? rw.le_lon : rw.he_lon;
        hdg = le ? rw.heading_deg : rw.heading_he_deg;
        elev = le ? rw.le_elev_m : rw.he_elev_m;
        disp_m = (le ? rw.le_displaced_ft : rw.he_displaced_ft) * geo::kFtToM;
        if (disp_m > 0.5)
            offset_m(lat, lon, hdg, disp_m, lat, lon);
    };

    auto end_bar = [&](bool le, int sw) {
        double lat, lon, hdg, elev, disp_m;
        landing(le, lat, lon, hdg, elev, disp_m);
        bar(a, lat, lon, hdg, elev, rw.width_m * 0.5, 8, 0.15f, 1.f, 0.25f, sw, LightKind::End, k);
        // Red end lights facing the opposite direction (bidirectional pair).
        bar(a, le ? rw.le_lat : rw.he_lat, le ? rw.le_lon : rw.he_lon, hdg, elev, rw.width_m * 0.5, 6,
            1.f, 0.12f, 0.1f, sw, LightKind::End, k);
    };
    end_bar(true, sw_tdz);
    end_bar(false, sw_tdz);

    auto tdz_bars = [&](bool le, bool want) {
        if (!want)
            return;
        double lat, lon, hdg, elev, disp_m;
        landing(le, lat, lon, hdg, elev, disp_m);
        const double reach = 900.0; // 3000 ft
        for (int i = 1; i <= 30; ++i) {
            const double d = reach * i / 30.0;
            double clat, clon;
            offset_m(lat, lon, hdg, d, clat, clon);
            bar(a, clat, clon, hdg, elev, 9.0, 3, 1.f, 1.f, 0.85f, sw_tdz, LightKind::Threshold, k);
        }
    };
    tdz_bars(true, rw.tdz_le);
    tdz_bars(false, rw.tdz_he);

    auto papi = [&](bool le) {
        const std::string side = le ? rw.papi_le : rw.papi_he;
        if (side.empty() || side == "NONE")
            return;
        const int nsys = le ? rw.papi_count_le : rw.papi_count_he;
        double lat, lon, hdg, elev, disp_m;
        landing(le, lat, lon, hdg, elev, disp_m);
        const bool right = side.find("P4R") != std::string::npos && side.find("P4L") == std::string::npos;
        const double perp = (right ? 1.0 : -1.0) * (rw.width_m * 0.5 + 15.0);
        for (int s = 0; s < std::max(1, nsys); ++s) {
            double plat, plon;
            offset_m(lat, lon, hdg, 107.0 + s * 30.0, plat, plon); // ~350 ft past threshold
            offset_m(plat, plon, hdg + 90.0, perp + s * 8.0, plat, plon);
            for (int i = 0; i < 4; ++i) {
                double qlat, qlon;
                offset_m(plat, plon, hdg, i * 9.0, qlat, qlon);
                const float g = i < 2 ? 1.f : 0.15f;
                const float r = i < 2 ? 0.15f : 1.f;
                push_light(a, qlat, qlon, elev, r, g, 0.15f, sw_vasi, LightKind::Vasi, k);
            }
        }
    };
    papi(true);
    papi(false);

    auto reil = [&](bool le, int sw) {
        if (!(le ? rw.reil_le : rw.reil_he))
            return;
        double lat, lon, hdg, elev, disp_m;
        landing(le, lat, lon, hdg, elev, disp_m);
        double llat, llon, rlat, rlon;
        offset_m(lat, lon, hdg + 90.0, rw.width_m * 0.5 + 8.0, rlat, rlon);
        offset_m(lat, lon, hdg - 90.0, rw.width_m * 0.5 + 8.0, llat, llon);
        push_light(a, llat, llon, elev, 1.f, 1.f, 1.f, sw, LightKind::Reil, k);
        push_light(a, rlat, rlon, elev, 1.f, 1.f, 1.f, sw, LightKind::Reil, k);
    };
    reil(true, sw_strobe_a);
    reil(false, sw_strobe_b);

    auto als = [&](bool le, int sw) {
        const std::string sys = le ? rw.approach_le : rw.approach_he;
        if (sys.empty() || sys == "NONE")
            return;
        double lat, lon, hdg, elev, disp_m;
        landing(le, lat, lon, hdg, elev, disp_m);
        const double back = wrap360(hdg + 180.0);
        auto bar5 = [&](double dist_ft, LightKind kind, int seq = -1) {
            double plat, plon;
            offset_m(lat, lon, back, dist_ft * geo::kFtToM, plat, plon);
            bar(a, plat, plon, hdg, elev, 6.0, 5, 1.f, 1.f, 0.92f, sw, kind, k);
            if (seq >= 0) {
                push_light(a, plat, plon, elev + 0.3, 1.f, 1.f, 1.f, sw, LightKind::Flasher, k, seq);
            }
        };
        if (sys == "ALSF-2") {
            // FAA ALSF-2: 5-light bars every 100 ft to 2400 ft, 1000-ft
            // crossbar, side rows, sequenced flashers.
            for (int i = 1; i <= 24; ++i)
                bar5(100.0 * i, LightKind::Approach, (i >= 10) ? (24 - i) : -1);
            double xlat, xlon;
            offset_m(lat, lon, back, 1000.0 * geo::kFtToM, xlat, xlon);
            bar(a, xlat, xlon, hdg, elev, 22.0, 15, 1.f, 1.f, 0.92f, sw, LightKind::Approach, k);
            for (int i = 1; i <= 10; ++i) {
                double slat, slon;
                offset_m(lat, lon, back, 100.0 * i * geo::kFtToM, slat, slon);
                double llat, llon, rlat, rlon;
                offset_m(slat, slon, hdg - 90.0, 18.0, llat, llon);
                offset_m(slat, slon, hdg + 90.0, 18.0, rlat, rlon);
                push_light(a, llat, llon, elev, 1.f, 0.15f, 0.1f, sw, LightKind::Approach, k);
                push_light(a, rlat, rlon, elev, 1.f, 0.15f, 0.1f, sw, LightKind::Approach, k);
            }
        } else if (sys == "ALSF-1") {
            for (int i = 1; i <= 24; ++i)
                bar5(100.0 * i, LightKind::Approach, (i >= 10) ? (24 - i) : -1);
            double xlat, xlon;
            offset_m(lat, lon, back, 1000.0 * geo::kFtToM, xlat, xlon);
            bar(a, xlat, xlon, hdg, elev, 22.0, 15, 1.f, 1.f, 0.92f, sw, LightKind::Approach, k);
        } else if (sys == "MALSR") {
            for (int i = 1; i <= 7; ++i)
                bar5(200.0 * i, LightKind::Approach);
            for (int i = 0; i < 5; ++i)
                bar5(1600.0 + 200.0 * i, LightKind::Flasher, i);
        } else if (sys == "MALSF") {
            for (int i = 1; i <= 7; ++i)
                bar5(200.0 * i, LightKind::Approach);
            for (int i = 0; i < 5; ++i)
                bar5(1400.0 + 200.0 * i, LightKind::Flasher, i);
        } else if (sys == "MALS") {
            for (int i = 1; i <= 7; ++i)
                bar5(200.0 * i, LightKind::Approach);
        } else if (sys == "SALS") {
            for (int i = 1; i <= 7; ++i)
                bar5(200.0 * i, LightKind::Approach);
        } else if (sys == "CAT-I") {
            for (int i = 1; i <= 10; ++i)
                bar5(300.0 * i, LightKind::Approach);
        } else if (sys == "CAT-II") {
            for (int i = 1; i <= 15; ++i)
                bar5(200.0 * i, LightKind::Approach, (i >= 8) ? (15 - i) : -1);
            double xlat, xlon;
            offset_m(lat, lon, back, 1000.0 * geo::kFtToM, xlat, xlon);
            bar(a, xlat, xlon, hdg, elev, 18.0, 11, 1.f, 1.f, 0.92f, sw, LightKind::Approach, k);
        } else if (sys == "RLLS") {
            for (int i = 1; i <= 12; ++i) {
                bar5(500.0 * i, LightKind::Approach, i - 1);
            }
        } else {
            bar5(1400.0, LightKind::Approach);
        }
    };
    als(true, sw_app_a);
    als(false, sw_app_b);
}

void lights_for_taxi(Airport& a) {
    for (const auto& tw : a.taxiways) {
        if (tw.path.size() < 2)
            continue;
        for (std::size_t s = 1; s < tw.path.size(); ++s) {
            const double a0 = tw.path[s - 1].first, o0 = tw.path[s - 1].second;
            const double a1 = tw.path[s].first, o1 = tw.path[s].second;
            const double dlat = (a1 - a0) * geo::kDeg * geo::kWgsA;
            const double dlon = (o1 - o0) * geo::kDeg * geo::kWgsA * std::cos(geo::deg2rad(a0));
            const double len = std::sqrt(dlat * dlat + dlon * dlon);
            const double hdg = geo::rad2deg(std::atan2(dlon, dlat));
            const int n = std::max(1, static_cast<int>(std::floor(len / 30.0)));
            for (int i = 0; i <= n; ++i) {
                const double t = i / static_cast<double>(n);
                const double lat = a0 + (a1 - a0) * t;
                const double lon = o0 + (o1 - o0) * t;
                double elat, elon, clat = lat, clon = lon;
                offset_m(lat, lon, hdg + 90.0, tw.width_m * 0.5, elat, elon);
                push_light(a, elat, elon, a.elev_m, 0.15f, 0.85f, 0.25f, a.sw_taxi_edge,
                           LightKind::TaxiEdge);
                offset_m(lat, lon, hdg - 90.0, tw.width_m * 0.5, elat, elon);
                push_light(a, elat, elon, a.elev_m, 0.15f, 0.85f, 0.25f, a.sw_taxi_edge,
                           LightKind::TaxiEdge);
                if (i % 2 == 0)
                    push_light(a, clat, clon, a.elev_m, 1.f, 0.85f, 0.15f, a.sw_taxi_centerline,
                               LightKind::TaxiCenterline);
            }
            if (s == 1) {
                bar(a, a0, o0, hdg, a.elev_m, tw.width_m * 0.45, 5, 1.f, 0.12f, 0.1f, a.sw_stop_bar,
                    LightKind::StopBar);
            }
        }
    }
}

Runway runway_from_faa_pair(const FaaEnd& le, const FaaEnd& he) {
    Runway r;
    r.ident_le = le.ident;
    r.ident_he = he.ident;
    r.le_lat = le.lat;
    r.le_lon = le.lon;
    r.le_elev_m = le.elev_ft * geo::kFtToM;
    r.he_lat = he.lat;
    r.he_lon = he.lon;
    r.he_elev_m = he.elev_ft * geo::kFtToM;
    r.length_m = geo::inverse_wgs84(le.lat, le.lon, he.lat, he.lon, &r.heading_deg);
    geo::inverse_wgs84(he.lat, he.lon, le.lat, le.lon, &r.heading_he_deg);
    r.width_m = le.width_ft * geo::kFtToM;
    r.approach_le = le.als;
    r.approach_he = he.als;
    r.le_displaced_ft = le.displaced_ft;
    r.he_displaced_ft = he.displaced_ft;
    r.papi_le = le.papi;
    r.papi_he = he.papi;
    r.papi_deg_le = le.papi_deg;
    r.papi_deg_he = he.papi_deg;
    r.papi_count_le = (le.papi.find('+') != std::string::npos) ? 2 : 1;
    r.papi_count_he = (he.papi.find('+') != std::string::npos) ? 2 : 1;
    r.reil_le = le.reil;
    r.reil_he = he.reil;
    r.tdz_le = le.tdz;
    r.tdz_he = he.tdz;
    r.emas_le = le.emas;
    r.emas_he = he.emas;
    r.emas_le_len_ft = le.emas_len_ft;
    r.emas_he_len_ft = he.emas_len_ft;
    r.emas_wid_ft = std::max(le.emas_wid_ft, he.emas_wid_ft);
    r.source.name = le.source;
    r.source.effective = le.effective;
    r.source.retrieved = le.retrieved;
    return r;
}

Airport airport_from_faa_ref(const FaaAirportRef& faa) {
    Airport a;
    a.icao = faa.icao;
    a.name = faa.name;
    a.attribution = faa.source + " effective " + faa.effective + " retrieved " + faa.retrieved;
    a.arp_lat = faa.arp_lat;
    a.arp_lon = faa.arp_lon;
    a.elev_m = faa.elev_ft * geo::kFtToM;
    a.source.name = faa.source;
    a.source.effective = faa.effective;
    a.source.retrieved = faa.retrieved;
    for (std::size_t i = 0; i + 1 < faa.ends.size(); i += 2)
        a.runways.push_back(runway_from_faa_pair(faa.ends[i], faa.ends[i + 1]));
    return a;
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
    case LightKind::Strobe:
        return "strobe";
    case LightKind::Vasi:
        return "vasi";
    case LightKind::TaxiEdge:
        return "taxi_edge";
    case LightKind::StopBar:
        return "stop_bar";
    case LightKind::TaxiCenterline:
        return "taxi_centerline";
    case LightKind::Flasher:
        return "flasher";
    case LightKind::Reil:
        return "reil";
    case LightKind::Rwsl:
        return "rwsl";
    case LightKind::End:
        return "end";
    case LightKind::LeadOn:
        return "lead_on";
    case LightKind::SignGlow:
        return "sign_glow";
    case LightKind::Beacon:
        return "beacon";
    case LightKind::Night:
        return "night";
    }
    return "?";
}

void decorate_simple(Airport& a, const char* taxi0, double perp0, const char* taxi1, double perp1,
                     const char* apron, double alat, double alon, double hdg, double alen,
                     double awid, const char* bldg, float bh) {
    if (!a.runways.empty()) {
        a.taxiways.push_back(parallel_taxi(taxi0, a.runways[0], perp0));
        if (taxi1 && a.runways.size() > 1)
            a.taxiways.push_back(parallel_taxi(taxi1, a.runways[1], perp1));
    }
    a.aprons.push_back(box_apron(apron, alat, alon, hdg, alen, awid, a.elev_m));
    a.buildings.push_back(box_building(bldg, alat, alon, hdg, alen * 0.4, 80, bh));
}

Airport ksea_public() {
    Airport a = airport_from_faa_ref(faa_ksea());
    a.taxiways.push_back(parallel_taxi("T", a.runways[0], 90.0));
    a.taxiways.push_back(parallel_taxi("B", a.runways[1], 80.0));
    a.aprons.push_back(box_apron("main", a.arp_lat, a.arp_lon + 0.004, 90.0, 800, 400, a.elev_m));
    a.buildings.push_back(box_building("main-term", a.arp_lat, a.arp_lon + 0.004, 90.0, 420, 90, 22.f));
    generate_lights(a);
    return a;
}

Airport ksfo_public() {
    Airport a = airport_from_faa_ref(faa_ksfo());
    a.taxiways.push_back(parallel_taxi("A", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("B", a.runways[1], -80.0));
    a.taxiways.push_back(parallel_taxi("C", a.runways[2], 70.0));
    a.aprons.push_back(box_apron("terminal", 37.6165, -122.3860, 30.0, 900, 500, a.elev_m));
    a.buildings.push_back(box_building("ITB", 37.6165, -122.3860, 30.0, 380, 80, 24.f));
    generate_lights(a);
    return a;
}

Airport klas_public() {
    Airport a = airport_from_faa_ref(faa_klas());
    a.taxiways.push_back(parallel_taxi("A", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("B", a.runways[1], -80.0));
    a.taxiways.push_back(parallel_taxi("S", a.runways[2], 70.0));
    a.aprons.push_back(box_apron("D-gates", 36.0845, -115.1520, 90.0, 1100, 450, a.elev_m));
    a.buildings.push_back(box_building("D", 36.0845, -115.1520, 90.0, 500, 80, 20.f));
    generate_lights(a);
    return a;
}

Airport klax_public() {
    Airport a = airport_from_faa_ref(faa_klax());
    a.taxiways.push_back(parallel_taxi("B", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("C", a.runways[1], -80.0));
    a.taxiways.push_back(parallel_taxi("S", a.runways[2], 70.0));
    a.aprons.push_back(box_apron("TBIT", 33.9435, -118.4085, 69.0, 1200, 500, a.elev_m));
    a.buildings.push_back(box_building("TBIT", 33.9435, -118.4085, 69.0, 520, 90, 28.f));
    generate_lights(a);
    return a;
}

Airport kjfk_public() {
    Airport a = airport_from_faa_ref(faa_kjfk());
    a.taxiways.push_back(parallel_taxi("A", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("B", a.runways[1], -80.0));
    a.taxiways.push_back(parallel_taxi("Z", a.runways[3], 80.0));
    a.aprons.push_back(box_apron("T4", 40.6445, -73.7780, 40.0, 1000, 450, a.elev_m));
    a.buildings.push_back(box_building("T4", 40.6445, -73.7780, 40.0, 380, 80, 22.f));
    a.buildings.push_back(box_building("T5", 40.6410, -73.7870, 121.0, 280, 70, 18.f));
    a.buildings.push_back(box_building("T8", 40.6468, -73.7895, 121.0, 260, 70, 20.f));
    a.buildings.push_back(box_building("T1", 40.6438, -73.7908, 121.0, 240, 70, 18.f));
    a.buildings.push_back(box_building("T7", 40.6488, -73.7832, 40.0, 220, 65, 18.f));
    a.buildings.push_back(box_building("hangar", 40.6515, -73.7940, 121.0, 180, 70, 16.f));
    a.buildings.push_back(box_building("cargo", 40.6550, -73.7720, 40.0, 200, 80, 14.f));
    add_kjfk_public_scenery(a);
    generate_lights(a);
    return a;
}

Airport kewr_public() {
    Airport a = airport_from_faa_ref(faa_kewr());
    a.taxiways.push_back(parallel_taxi("A", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("B", a.runways[1], -80.0));
    a.taxiways.push_back(parallel_taxi("S", a.runways[2], 70.0));
    a.aprons.push_back(box_apron("A-gates", 40.6895, -74.1740, 26.0, 900, 420, a.elev_m));
    a.buildings.push_back(box_building("A", 40.6895, -74.1740, 26.0, 360, 80, 22.f));
    a.buildings.push_back(box_building("B", 40.6920, -74.1775, 26.0, 280, 70, 20.f));
    a.buildings.push_back(box_building("C", 40.6955, -74.1680, 95.0, 300, 75, 20.f));
    a.buildings.push_back(box_building("tower", 40.6928, -74.1688, 26.0, 40, 40, 55.f));
    a.tower_lat = 40.6928;
    a.tower_lon = -74.1688;
    a.tower_h_m = 55.f;
    a.has_beacon = true;
    add_kewr_public_scenery(a);
    generate_lights(a);
    return a;
}

Airport airport_from_faa(const std::string& icao) {
    const FaaAirportRef* ref = faa_airport(icao);
    if (!ref)
        throw std::runtime_error("no official FAA/AIP both-end layout for " + icao);
    Airport a = airport_from_faa_ref(*ref);
    generate_lights(a);
    return a;
}

Airport kbos_public() {
    Airport a = airport_from_faa_ref(faa_kbos());
    a.taxiways.push_back(parallel_taxi("N", a.runways[0], 80.0));
    a.taxiways.push_back(parallel_taxi("E", a.runways[1], -80.0));
    a.aprons.push_back(box_apron("A-gates", 42.3668, -71.0205, 50.0, 900, 420, a.elev_m));
    a.buildings.push_back(box_building("A", 42.3668, -71.0205, 50.0, 360, 80, 22.f));
    a.buildings.push_back(box_building("B", 42.3642, -71.0178, 50.0, 240, 70, 18.f));
    a.buildings.push_back(box_building("E", 42.3695, -71.0228, 150.0, 280, 70, 20.f));
    generate_lights(a);
    return a;
}

Airport kiad_public() {
    Airport a = airport_from_faa_ref(faa_kiad());
    decorate_simple(a, "A", 80.0, "B", -80.0, "midfield", a.arp_lat, a.arp_lon, 1.0, 900, 420,
                    "main", 22.f);
    generate_lights(a);
    return a;
}

Airport kmco_public() {
    Airport a = airport_from_faa_ref(faa_kmco());
    decorate_simple(a, "A", 80.0, "B", -80.0, "airside", a.arp_lat, a.arp_lon, 180.0, 1000, 450,
                    "main", 22.f);
    generate_lights(a);
    return a;
}

Airport kmia_public() {
    Airport a = airport_from_faa_ref(faa_kmia());
    decorate_simple(a, "M", 80.0, "N", -80.0, "terminal", a.arp_lat, a.arp_lon, 87.0, 1100, 450,
                    "main", 22.f);
    generate_lights(a);
    return a;
}

Airport krno_public() {
    Airport a = airport_from_faa_ref(faa_krno());
    decorate_simple(a, "A", 80.0, "B", -80.0, "terminal", a.arp_lat, a.arp_lon, 180.0, 700, 350,
                    "main", 18.f);
    generate_lights(a);
    return a;
}

Airport eddf_public() {
    Airport a = airport_from_faa_ref(faa_eddf());
    decorate_simple(a, "N", 80.0, "S", -80.0, "terminal", a.arp_lat, a.arp_lon, 70.0, 1200, 500,
                    "T1", 24.f);
    generate_lights(a);
    return a;
}

Airport egll_public() {
    Airport a = airport_from_faa_ref(faa_egll());
    decorate_simple(a, "A", 80.0, "S", -80.0, "central", a.arp_lat, a.arp_lon, 90.0, 1100, 450,
                    "T5", 24.f);
    generate_lights(a);
    return a;
}

Airport lebl_public() {
    Airport a = airport_from_faa_ref(faa_lebl());
    decorate_simple(a, "T", 80.0, "S", -80.0, "T1", a.arp_lat, a.arp_lon, 66.0, 900, 400, "T1",
                    20.f);
    generate_lights(a);
    return a;
}

Airport seqm_public() {
    Airport a = airport_from_faa_ref(faa_seqm());
    decorate_simple(a, "A", 80.0, nullptr, 0, "terminal", a.arp_lat, a.arp_lon, 180.0, 800, 380,
                    "main", 18.f);
    generate_lights(a);
    return a;
}

Airport skbo_public() {
    Airport a = airport_from_faa_ref(faa_skbo());
    decorate_simple(a, "A", 80.0, "B", -80.0, "terminal", a.arp_lat, a.arp_lon, 127.0, 1000, 420,
                    "T1", 20.f);
    generate_lights(a);
    return a;
}

Airport skrg_public() {
    Airport a = airport_from_faa_ref(faa_skrg());
    decorate_simple(a, "A", 80.0, nullptr, 0, "terminal", a.arp_lat, a.arp_lon, 360.0, 700, 350,
                    "main", 16.f);
    generate_lights(a);
    return a;
}

Airport vnkt_public() {
    Airport a = airport_from_faa_ref(faa_vnkt());
    decorate_simple(a, "A", 70.0, nullptr, 0, "terminal", a.arp_lat, a.arp_lon, 22.0, 600, 300,
                    "main", 16.f);
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
    if (id == "KEWR")
        return kewr_public();
    if (id == "KBOS")
        return kbos_public();
    if (id == "KIAD")
        return kiad_public();
    if (id == "KMCO")
        return kmco_public();
    if (id == "KMIA")
        return kmia_public();
    if (id == "KRNO")
        return krno_public();
    if (id == "EDDF")
        return eddf_public();
    if (id == "EGLL")
        return egll_public();
    if (id == "LEBL")
        return lebl_public();
    if (id == "SEQM")
        return seqm_public();
    if (id == "SKBO")
        return skbo_public();
    if (id == "SKRG")
        return skrg_public();
    if (id == "VNKT")
        return vnkt_public();
    throw std::runtime_error("no built-in layout for " + icao);
}

std::vector<Airport> all_public_airports() {
    return {ksea_public(), ksfo_public(), klas_public(),  klax_public(), kjfk_public(),
            kewr_public(), kbos_public(), kiad_public(),  kmco_public(), kmia_public(),
            krno_public(), eddf_public(), egll_public(),  lebl_public(), seqm_public(),
            skbo_public(), skrg_public(), vnkt_public()};
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
        double lat0 = le ? rw.le_lat : rw.he_lat;
        double lon0 = le ? rw.le_lon : rw.he_lon;
        const double hdg = le ? rw.heading_deg
                              : (rw.heading_he_deg > 0.0 ? rw.heading_he_deg
                                                         : wrap360(rw.heading_deg + 180.0));
        const double disp_m = (le ? rw.le_displaced_ft : rw.he_displaced_ft) * geo::kFtToM;
        if (disp_m > 0.5)
            offset_m(lat0, lon0, hdg, disp_m, lat0, lon0);
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
            // Cockpit 12k photos: ocean, then Rockaway, then JFK, lots of sky.
            // ~11 nm stand-off, nearly level (horizon ~mid-low in a 32.5° VFOV).
            const double back_nm = std::min(11.0, agl / 1100.0);
            offset_m(a.arp_lat, a.arp_lon, wrap360(p.hdg_true_deg + 180.0), back_nm * 1852.0, p.lat,
                     p.lon);
            if (!o.have_pitch)
                p.pitch_deg = -std::min(2.0, agl / 8000.0);
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
      << "," << east << ");way[\"highway\"=\"motorway\"](" << south << "," << west << "," << north
      << "," << east << ");way[\"highway\"=\"trunk\"](" << south << "," << west << "," << north
      << "," << east << ");way[\"highway\"=\"primary\"](" << south << "," << west << "," << north
      << "," << east << ");way[\"highway\"=\"secondary\"](" << south << "," << west << "," << north
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
        else if (osm_tags_are_road(tags))
            kind = "road";
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
            } else if (kind == "road") {
                Road rd;
                rd.name = "osm";
                rd.cls = parse_osm_highway(tags);
                rd.path = std::move(pts);
                finalize_road(rd);
                a.roads.push_back(std::move(rd));
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
    return merge_osm_file_stream(a, path, 2ull * 1024ull * 1024ull);
}

int merge_osm_file_stream(Airport& a, const std::string& path, std::uint64_t max_bytes) {
    if (path.empty() || !std::filesystem::exists(path) || !std::filesystem::is_regular_file(path))
        return 0;
    const auto sz = std::filesystem::file_size(path);
    if (max_bytes > 0 && sz > max_bytes)
        return 0;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return 0;
    int added = 0;
    std::string carry;
    carry.reserve(256 * 1024);
    char buf[64 * 1024];
    while (in) {
        in.read(buf, sizeof(buf));
        const auto n = static_cast<std::size_t>(in.gcount());
        if (n == 0)
            break;
        carry.append(buf, n);
        std::size_t pos = 0;
        while (true) {
            auto g = carry.find("\"geometry\"", pos);
            if (g == std::string::npos) {
                if (carry.size() > 16384)
                    carry.erase(0, carry.size() - 8192);
                break;
            }
            auto arr = carry.find('[', g);
            if (arr == std::string::npos) {
                carry.erase(0, g);
                break;
            }
            int depth = 0;
            std::size_t end = arr;
            bool done = false;
            for (; end < carry.size(); ++end) {
                if (carry[end] == '[')
                    ++depth;
                else if (carry[end] == ']') {
                    --depth;
                    if (depth == 0) {
                        done = true;
                        break;
                    }
                }
            }
            if (!done) {
                carry.erase(0, g);
                break;
            }
            const auto win = carry.rfind('{', g);
            const std::size_t start = win != std::string::npos && win < g ? win : g;
            const auto chunk = carry.substr(start, end - start + 1);
            const bool is_bldg = chunk.find("\"building\"") != std::string::npos;
            const bool is_apron = chunk.find("apron") != std::string::npos;
            const bool is_road = osm_tags_are_road(chunk);
            // Same caps as write_osm_bin — a 400 MB Overpass dump must not
            // grow unbounded Airport vectors at bake time.
            if (is_bldg && a.buildings.size() >= kMaxOsmBuildings) {
                pos = end + 1;
                continue;
            }
            if (is_road && !is_bldg && a.roads.size() >= kMaxOsmRoads) {
                pos = end + 1;
                continue;
            }
            if (is_apron && !is_bldg && !is_road && a.aprons.size() >= kMaxOsmAprons) {
                pos = end + 1;
                continue;
            }
            if (!is_bldg && !is_apron && !is_road && a.taxiways.size() >= kMaxOsmTaxiways) {
                pos = end + 1;
                continue;
            }
            if (a.taxiways.size() >= kMaxOsmTaxiways && a.aprons.size() >= kMaxOsmAprons &&
                a.buildings.size() >= kMaxOsmBuildings && a.roads.size() >= kMaxOsmRoads)
                return added;
            added += merge_osm_json(a, chunk);
            pos = end + 1;
            if (pos > 128 * 1024) {
                carry.erase(0, pos);
                pos = 0;
            }
        }
    }
    return added;
}

namespace {

void write_u32(std::ostream& o, std::uint32_t v) {
    o.write(reinterpret_cast<const char*>(&v), 4);
}
void write_f32(std::ostream& o, float v) {
    o.write(reinterpret_cast<const char*>(&v), 4);
}
void write_f64(std::ostream& o, double v) {
    o.write(reinterpret_cast<const char*>(&v), 8);
}
bool read_u32(std::istream& i, std::uint32_t& v) {
    i.read(reinterpret_cast<char*>(&v), 4);
    return static_cast<bool>(i);
}
bool read_f32(std::istream& i, float& v) {
    i.read(reinterpret_cast<char*>(&v), 4);
    return static_cast<bool>(i);
}
bool read_f64(std::istream& i, double& v) {
    i.read(reinterpret_cast<char*>(&v), 8);
    return static_cast<bool>(i);
}

void write_poly(std::ostream& o, const std::string& name, float extra,
                const std::vector<std::pair<double, double>>& pts) {
    const auto nlen = static_cast<std::uint32_t>(std::min<std::size_t>(name.size(), 255));
    write_u32(o, nlen);
    o.write(name.data(), static_cast<std::streamsize>(nlen));
    write_f32(o, extra);
    write_u32(o, static_cast<std::uint32_t>(pts.size()));
    for (const auto& p : pts) {
        write_f64(o, p.first);
        write_f64(o, p.second);
    }
}

bool read_poly(std::istream& i, std::string& name, float& extra,
               std::vector<std::pair<double, double>>& pts) {
    std::uint32_t nlen = 0, npts = 0;
    if (!read_u32(i, nlen) || nlen > 1024)
        return false;
    name.assign(nlen, '\0');
    i.read(name.data(), static_cast<std::streamsize>(nlen));
    if (!read_f32(i, extra) || !read_u32(i, npts) || npts > 20000)
        return false;
    pts.resize(npts);
    for (auto& p : pts) {
        if (!read_f64(i, p.first) || !read_f64(i, p.second))
            return false;
    }
    return static_cast<bool>(i);
}

} // namespace

bool write_osm_bin(const Airport& a, const std::string& path) {
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);
    std::ofstream o(path, std::ios::binary);
    if (!o)
        return false;
    o.write("S376OSM1", 8);
    write_u32(o, 2);
    const auto nt = std::min<std::size_t>(a.taxiways.size(), kMaxOsmTaxiways);
    const auto na = std::min<std::size_t>(a.aprons.size(), kMaxOsmAprons);
    const auto nb = std::min<std::size_t>(a.buildings.size(), kMaxOsmBuildings);
    const auto nr = std::min<std::size_t>(a.roads.size(), kMaxOsmRoads);
    write_u32(o, static_cast<std::uint32_t>(nt));
    write_u32(o, static_cast<std::uint32_t>(na));
    write_u32(o, static_cast<std::uint32_t>(nb));
    for (std::size_t i = 0; i < nt; ++i)
        write_poly(o, a.taxiways[i].name, static_cast<float>(a.taxiways[i].width_m), a.taxiways[i].path);
    for (std::size_t i = 0; i < na; ++i)
        write_poly(o, a.aprons[i].name, 0.f, a.aprons[i].ring);
    for (std::size_t i = 0; i < nb; ++i)
        write_poly(o, a.buildings[i].name, a.buildings[i].height_m, a.buildings[i].ring);
    write_u32(o, static_cast<std::uint32_t>(nr));
    for (std::size_t i = 0; i < nr; ++i) {
        float cls = 3.f;
        if (a.roads[i].cls == RoadClass::Motorway)
            cls = 0.f;
        else if (a.roads[i].cls == RoadClass::Trunk)
            cls = 1.f;
        else if (a.roads[i].cls == RoadClass::Primary)
            cls = 2.f;
        write_poly(o, a.roads[i].name, cls, a.roads[i].path);
    }
    return static_cast<bool>(o);
}

int read_osm_bin(Airport& a, const std::string& path) {
    std::ifstream i(path, std::ios::binary);
    if (!i)
        return 0;
    char mag[8];
    i.read(mag, 8);
    if (!i || std::memcmp(mag, "S376OSM1", 8) != 0)
        return 0;
    std::uint32_t ver = 0, nt = 0, na = 0, nb = 0;
    if (!read_u32(i, ver) || (ver != 1 && ver != 2) || !read_u32(i, nt) || !read_u32(i, na) ||
        !read_u32(i, nb))
        return 0;
    int added = 0;
    for (std::uint32_t n = 0; n < nt; ++n) {
        Taxiway t;
        float w = 23.f;
        if (!read_poly(i, t.name, w, t.path))
            break;
        t.width_m = w;
        a.taxiways.push_back(std::move(t));
        ++added;
    }
    for (std::uint32_t n = 0; n < na; ++n) {
        Apron ap;
        float z = 0;
        if (!read_poly(i, ap.name, z, ap.ring))
            break;
        a.aprons.push_back(std::move(ap));
        ++added;
    }
    for (std::uint32_t n = 0; n < nb; ++n) {
        Building b;
        if (!read_poly(i, b.name, b.height_m, b.ring))
            break;
        a.buildings.push_back(std::move(b));
        ++added;
    }
    if (ver >= 2) {
        std::uint32_t nr = 0;
        if (read_u32(i, nr) && nr <= static_cast<std::uint32_t>(kMaxOsmRoads)) {
            for (std::uint32_t n = 0; n < nr; ++n) {
                Road r;
                float cls = 3.f;
                if (!read_poly(i, r.name, cls, r.path))
                    break;
                if (cls < 0.5f)
                    r.cls = RoadClass::Motorway;
                else if (cls < 1.5f)
                    r.cls = RoadClass::Trunk;
                else if (cls < 2.5f)
                    r.cls = RoadClass::Primary;
                else
                    r.cls = RoadClass::Secondary;
                finalize_road(r);
                a.roads.push_back(std::move(r));
                ++added;
            }
        }
    }
    return added;
}

int bake_osm_bin(const std::string& json_path, const std::string& bin_path) {
    Airport tmp;
    // Bake tools may stream a large Overpass file; no 2 MiB cap.
    const int n = merge_osm_file_stream(tmp, json_path, 0);
    if (!write_osm_bin(tmp, bin_path))
        return 0;
    return n;
}

int try_load_osm(Airport& a, const std::string& cache_dir) {
    std::string id = a.icao;
    std::string low = a.icao;
    for (char& c : id)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (char& c : low)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto folder_name = [](const std::string& path) {
        auto name = std::filesystem::path(path).parent_path().filename().string();
        for (char& c : name)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return name;
    };
    auto icao_like = [](const std::string& name) {
        if (name.size() != 4)
            return false;
        for (char c : name) {
            if (!std::isalpha(static_cast<unsigned char>(c)))
                return false;
        }
        return true;
    };
    std::vector<std::string> paths;
    if (!cache_dir.empty()) {
        const auto p = cache_dir + "/osm.bin";
        const auto folder = folder_name(p);
        // cache/KJFK/osm.bin is KJFK only — never merge it into KSEA/KSFO/…
        if (!icao_like(folder) || folder == id)
            paths.push_back(p);
    } else {
        paths.push_back("cache/" + id + "/osm.bin");
        paths.push_back("cache/" + low + "/osm.bin");
    }
    for (const auto& p : paths) {
        if (!std::filesystem::exists(p))
            continue;
        const int n = read_osm_bin(a, p);
        if (n > 0)
            return n;
    }
    return 0;
}

void generate_lights(Airport& a) {
    a.lights.clear();
    for (std::size_t i = 0; i < a.runways.size(); ++i)
        lights_for_runway(a, a.runways[i], static_cast<int>(i) + 1);
    lights_for_taxi(a);
    add_scenery_lights(a);
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
        auto emas = [&](bool le) {
            const bool on = le ? rw.emas_le : rw.emas_he;
            const double len_ft = le ? rw.emas_le_len_ft : rw.emas_he_len_ft;
            if (!on)
                return;
            const double use_len_ft = len_ft >= 1.0 ? len_ft : 200.0;
            const double lat = le ? rw.le_lat : rw.he_lat;
            const double lon = le ? rw.le_lon : rw.he_lon;
            const double elev = le ? rw.le_elev_m : rw.he_elev_m;
            const double out = le ? wrap360(rw.heading_deg + 180.0) : rw.heading_he_deg;
            const double len_m = use_len_ft * geo::kFtToM;
            const double wid_m =
                (rw.emas_wid_ft > 1.0 ? rw.emas_wid_ft : rw.width_m * geo::kMToFt) * geo::kFtToM;
            double elat, elon;
            offset_m(lat, lon, out, len_m, elat, elon);
            strip_quad(mesh, lat, lon, elat, elon, out, wid_m * 0.5, elev + 0.38, elev + 0.38, 0.42f,
                       0.40f, 0.36f, 2.f);
        };
        emas(true);
        emas(false);
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
    std::vector<std::size_t> order;
    order.reserve(a.buildings.size());
    for (std::size_t i = 0; i < a.buildings.size(); ++i)
        order.push_back(i);
    std::sort(order.begin(), order.end(), [&](std::size_t i, std::size_t j) {
        auto dist2 = [&](std::size_t k) {
            if (a.buildings[k].ring.empty())
                return 1e18;
            double clat = 0, clon = 0;
            for (const auto& p : a.buildings[k].ring) {
                clat += p.first;
                clon += p.second;
            }
            clat /= static_cast<double>(a.buildings[k].ring.size());
            clon /= static_cast<double>(a.buildings[k].ring.size());
            const double dlat = clat - a.arp_lat;
            const double dlon = clon - a.arp_lon;
            return dlat * dlat + dlon * dlon;
        };
        return dist2(i) < dist2(j);
    });
    int nb = 0;
    for (std::size_t idx : order) {
        if (!buildings || nb >= kMeshBuildingsFar)
            break;
        const auto& b = a.buildings[idx];
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
        const bool far = nb >= kMeshBuildingsNear;
        const std::size_t step = far ? std::max<std::size_t>(1, b.ring.size() / 4) : 1;
        for (std::size_t i = 0; i < b.ring.size(); i += step) {
            const auto& p0 = b.ring[i];
            const auto& p1 = b.ring[(i + step) % b.ring.size()];
            const geo::Ecef A = geo::lla_to_ecef(p0.first, p0.second, z0);
            const geo::Ecef B = geo::lla_to_ecef(p1.first, p1.second, z0);
            const geo::Ecef C = geo::lla_to_ecef(p1.first, p1.second, z1);
            const geo::Ecef D = geo::lla_to_ecef(p0.first, p0.second, z1);
            geo::Ecef wn{B.x - A.x, B.y - A.y, B.z - A.z};
            geo::Ecef nn{wn.y * u.z - wn.z * u.y, wn.z * u.x - wn.x * u.z, wn.x * u.y - wn.y * u.x};
            add_tri(mesh, A, B, C, nn, 0.62f, 0.58f, 0.52f, 3.f);
            add_tri(mesh, A, C, D, nn, 0.62f, 0.58f, 0.52f, 3.f);
            add_tri(mesh, roof_c, D, C, u, 0.28f, 0.30f, 0.32f, 4.f);
        }
        ++nb;
    }
    for (const auto& t : a.trees) {
        strip_quad(mesh, t.lat, t.lon, t.lat + 0.00004, t.lon, 0.0, 1.6, a.elev_m, a.elev_m + t.height_m,
                   0.18f, 0.32f, 0.14f, 4.f);
    }
    for (const auto& p : a.parked) {
        double nlat, nlon;
        offset_m(p.lat, p.lon, p.hdg_deg, 18.0, nlat, nlon);
        strip_quad(mesh, p.lat, p.lon, nlat, nlon, p.hdg_deg, 7.0, a.elev_m + 2.5, a.elev_m + 2.5,
                   0.72f, 0.73f, 0.76f, 3.f);
    }
    for (const auto& j : a.jetbridges) {
        double nlat, nlon;
        offset_m(j.lat, j.lon, j.hdg_deg, j.length_m, nlat, nlon);
        strip_quad(mesh, j.lat, j.lon, nlat, nlon, j.hdg_deg, 2.2, a.elev_m + 5.0, a.elev_m + 5.0,
                   0.55f, 0.56f, 0.58f, 3.f);
    }
    return mesh;
}

} // namespace s376
