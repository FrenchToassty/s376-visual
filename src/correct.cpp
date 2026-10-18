#include "s376/correct.hpp"

#include "s376/geo.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace s376 {
namespace {

void add_item(CorrectionReport& r, const std::string& icao, const char* feature, const char* img,
              const char* official, const char* action) {
    ImageryCorrection c;
    c.icao = icao;
    c.feature = feature;
    c.imagery = img;
    c.official = official;
    c.action = action;
    r.items.push_back(std::move(c));
}

void paint_runway_pixels(RgbImage& im, const GeoBox& box, double lat0, double lon0, double lat1,
                         double lon1, double half_w_m, std::uint8_t r, std::uint8_t g,
                         std::uint8_t b) {
    if (im.w < 2 || im.h < 2 || box.east <= box.west || box.north <= box.south)
        return;
    const double mid_lat = 0.5 * (lat0 + lat1);
    const double m_per_deg_lat = geo::kDeg * geo::kWgsA;
    const double m_per_deg_lon = geo::kDeg * geo::kWgsA * std::cos(geo::deg2rad(mid_lat));
    auto to_px = [&](double lat, double lon, int& x, int& y) {
        x = static_cast<int>(std::lround((lon - box.west) / (box.east - box.west) * (im.w - 1)));
        y = static_cast<int>(std::lround((box.north - lat) / (box.north - box.south) * (im.h - 1)));
    };
    int x0, y0, x1, y1;
    to_px(lat0, lon0, x0, y0);
    to_px(lat1, lon1, x1, y1);
    const double dx = static_cast<double>(x1 - x0);
    const double dy = static_cast<double>(y1 - y0);
    const int steps = std::max(2, static_cast<int>(std::hypot(dx, dy)));
    const double deg_half =
        half_w_m / std::max(1e-6, std::hypot(m_per_deg_lat * ((lat1 - lat0) / std::max(1e-9, std::hypot(lat1 - lat0, lon1 - lon0))),
                                              1.0));
    (void)deg_half;
    const double px_per_m_x = (im.w - 1) / ((box.east - box.west) * m_per_deg_lon);
    const double px_per_m_y = (im.h - 1) / ((box.north - box.south) * m_per_deg_lat);
    const int half_px = std::max(1, static_cast<int>(std::lround(half_w_m * 0.5 * (px_per_m_x + px_per_m_y))));
    for (int i = 0; i <= steps; ++i) {
        const double t = i / static_cast<double>(steps);
        const int cx = static_cast<int>(std::lround(x0 + dx * t));
        const int cy = static_cast<int>(std::lround(y0 + dy * t));
        for (int oy = -half_px; oy <= half_px; ++oy) {
            for (int ox = -half_px; ox <= half_px; ++ox) {
                const int x = cx + ox;
                const int y = cy + oy;
                if (x < 0 || y < 0 || x >= im.w || y >= im.h)
                    continue;
                const int di = (y * im.w + x) * 3;
                im.rgb[static_cast<std::size_t>(di)] = r;
                im.rgb[static_cast<std::size_t>(di + 1)] = g;
                im.rgb[static_cast<std::size_t>(di + 2)] = b;
            }
        }
    }
}

} // namespace

CorrectionReport plan_corrections(const Airport& a, const FaaAirportRef& faa) {
    CorrectionReport r;
    r.icao = a.icao;
    r.imagery = check_airport_vs_faa(a, faa);
    r.imagery.icao = a.icao;
    add_item(r, a.icao, "runway-surface",
             "photo / Raster pad may show old paint, an extension, or missing pavement",
             (faa.source + " effective " + faa.effective).c_str(),
             "repaint official runway surface from both surveyed ends");
    for (const auto& e : faa.ends) {
        std::ostringstream off;
        off << e.ident << " physical end " << e.lat << "," << e.lon << "  ALS " << e.als << "  PAPI "
            << e.papi << " " << e.papi_deg << "°";
        add_item(r, a.icao, ("RWY " + e.ident).c_str(),
                 "imagery is texture only; stale markings / old threshold possible",
                 off.str().c_str(),
                 "repaint markings at official landing THR; official ALS/PAPI/REIL/TDZ win");
        if (e.displaced_ft > 0.5) {
            std::ostringstream d;
            d << e.ident << " displaced " << e.displaced_ft << " ft (" << e.source << ")";
            add_item(r, a.icao, ("RWY " + e.ident + " displaced-threshold").c_str(),
                     "photo may show an older painted threshold", d.str().c_str(),
                     "repaint arrows/bars at official THR; blend stale paint in the displaced area");
        }
        if (e.emas) {
            std::ostringstream em;
            em << e.ident << " EMAS " << e.emas_len_ft << "x" << e.emas_wid_ft << " ft";
            add_item(r, a.icao, ("RWY " + e.ident + " EMAS").c_str(),
                     "photo may omit the bed or show a previous length", em.str().c_str(),
                     "paint official EMAS bed beyond the DER");
        }
    }
    add_item(r, a.icao, "stale-pavement",
             "construction, demolished pavement, renamed/removed taxiways in the photo",
             "official runway / taxi geometry only",
             "blend out paint and pavement the official data says is not there");
    return r;
}

std::string format_correction_report(const CorrectionReport& r) {
    std::ostringstream o;
    o << r.icao << " imagery corrections  " << r.items.size() << " planned  painted=" << r.painted
      << "\n";
    o << format_acc_report(r.imagery);
    for (const auto& c : r.items) {
        o << "  CORRECT  " << c.feature << "\n";
        o << "    imagery   " << c.imagery << "\n";
        o << "    official  " << c.official << "\n";
        o << "    action    " << c.action << "\n";
    }
    return o.str();
}

int apply_correction_overlay(RgbImage& im, const GeoBox& box, const Airport& a) {
    if (im.w < 2 || im.h < 2 || im.rgb.size() < static_cast<std::size_t>(im.w * im.h * 3))
        return 0;
    int n = 0;
    for (const auto& rw : a.runways) {
        paint_runway_pixels(im, box, rw.le_lat, rw.le_lon, rw.he_lat, rw.he_lon, rw.width_m * 0.5,
                            42, 42, 44);
        ++n;
        auto emas = [&](bool le) {
            const bool on = le ? rw.emas_le : rw.emas_he;
            const double len_ft = le ? rw.emas_le_len_ft : rw.emas_he_len_ft;
            if (!on)
                return;
            const double lat = le ? rw.le_lat : rw.he_lat;
            const double lon = le ? rw.le_lon : rw.he_lon;
            const double out = le ? geo::wrap360(rw.heading_deg + 180.0) : rw.heading_he_deg;
            const double len_m = (len_ft >= 1.0 ? len_ft : 200.0) * geo::kFtToM;
            double elat, elon;
            offset_m(lat, lon, out, len_m, elat, elon);
            const double wid =
                (rw.emas_wid_ft > 1.0 ? rw.emas_wid_ft * geo::kFtToM : rw.width_m) * 0.5;
            paint_runway_pixels(im, box, lat, lon, elat, elon, wid, 110, 104, 92);
            ++n;
        };
        emas(true);
        emas(false);
    }
    return n;
}

AccReport check_imagery_vs_official(const Airport& a, const FaaAirportRef& faa, const GeoBox* box,
                                    int w, int h) {
    AccReport r = check_airport_vs_faa(a, faa);
    r.icao = a.icao;
    if (box && w > 1 && h > 1)
        imagery_aligned_to_runways(*box, w, h, a, kAccImageryM, &r);
    return r;
}

} // namespace s376
