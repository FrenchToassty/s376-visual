#include "s376/faa.hpp"

#include "s376/geo.hpp"

#include <cctype>

namespace s376 {
namespace {

std::string up(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

FaaEnd end(const char* icao, const char* ident, double lat, double lon, double elev_ft,
           double brg, double len_ft, double wid_ft, double disp_ft, const char* als,
           const char* papi, double papi_deg, bool reil, bool tdz, bool emas, double emas_l,
           double emas_w) {
    FaaEnd e;
    e.icao = icao;
    e.ident = ident;
    e.lat = lat;
    e.lon = lon;
    e.elev_ft = elev_ft;
    e.true_brg_deg = brg;
    e.length_ft = len_ft;
    e.width_ft = wid_ft;
    e.displaced_ft = disp_ft;
    e.als = als;
    e.papi = papi;
    e.papi_deg = papi_deg;
    e.reil = reil;
    e.tdz = tdz;
    e.emas = emas;
    e.emas_len_ft = emas_l;
    e.emas_wid_ft = emas_w;
    e.source = "FAA AIP AD 2 / NFDC airportDisplay, cross-checked OurAirports";
    e.effective = "2026-10-02";
    e.retrieved = "2026-10-18";
    return e;
}

} // namespace

const FaaAirportRef& faa_kjfk() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once = true;
    a.icao = "KJFK";
    a.name = "John F Kennedy International";
    a.arp_lat = geo::dms_to_deg(40, 38, 23.741);
    a.arp_lon = geo::dms_to_deg(73, 46, 43.292, true);
    a.elev_ft = 13.0;
    a.source = "FAA AIP AD 2 KJFK (faa.gov) + NFDC, cross-checked OurAirports";
    a.effective = "2026-10-02";
    a.retrieved = "2026-10-18";
    // Physical ends from FAA AIP AD 2.12. Displaced / ALS / PAPI from
    // AD 2.14 + chart supplement. EMAS from AIP general remarks (DER 4R / 22L).
    a.ends.push_back(end("KJFK", "04L", geo::dms_to_deg(40, 37, 19.2754),
                         geo::dms_to_deg(73, 47, 8.1029, true), 11.9, 31, 12079, 200, 460, "NONE",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KJFK", "22R", geo::dms_to_deg(40, 39, 1.8338),
                         geo::dms_to_deg(73, 45, 47.9596, true), 12.5, 211, 12079, 200, 3424, "NONE",
                         "P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KJFK", "04R", geo::dms_to_deg(40, 37, 31.5418),
                         geo::dms_to_deg(73, 46, 13.2441, true), 11.9, 31, 8400, 200, 0, "ALSF-2",
                         "P4L", 3.00, false, true, true, 393, 226));
    a.ends.push_back(end("KJFK", "22L", geo::dms_to_deg(40, 38, 42.8531),
                         geo::dms_to_deg(73, 45, 17.5027, true), 11.9, 211, 8400, 200, 0, "ALSF-2",
                         "P4L", 3.00, false, true, true, 405, 226));
    a.ends.push_back(end("KJFK", "13L", geo::dms_to_deg(40, 39, 27.952),
                         geo::dms_to_deg(73, 47, 24.8606, true), 12.9, 121, 10000, 200, 907, "ALSF-2",
                         "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KJFK", "31R", geo::dms_to_deg(40, 38, 37.4085),
                         geo::dms_to_deg(73, 45, 33.3818, true), 12.6, 301, 10000, 200, 1027,
                         "MALSR", "P4R", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KJFK", "13R", geo::dms_to_deg(40, 38, 54.1008),
                         geo::dms_to_deg(73, 49, 0.173, true), 12.5, 121, 14511, 200, 2044, "RLLS",
                         "P4L+P4L", 3.00, false, false, false, 0, 0));
    a.ends.push_back(end("KJFK", "31L", geo::dms_to_deg(40, 37, 40.7799),
                         geo::dms_to_deg(73, 46, 18.4107, true), 12.6, 301, 14511, 200, 3264, "NONE",
                         "P4L", 3.00, false, false, false, 0, 0));
    return a;
}

const FaaAirportRef& faa_kewr() {
    static FaaAirportRef a;
    static bool once = false;
    if (once)
        return a;
    once = true;
    a.icao = "KEWR";
    a.name = "Newark Liberty International";
    a.arp_lat = geo::dms_to_deg(40, 41, 33.0); // N40°41.55′
    a.arp_lon = geo::dms_to_deg(74, 10, 7.2, true);
    a.elev_ft = 18.0;
    a.source = "FAA NFDC airportDisplay KEWR + AIP AD 2 + chart supplement, "
               "cross-checked AirNav/OurAirports";
    a.effective = "2026-10-02";
    a.retrieved = "2026-10-18";
    a.ends.push_back(end("KEWR", "04L", geo::dms_to_deg(40, 40, 31.3731),
                         geo::dms_to_deg(74, 10, 46.0156, true), 9.8, 26, 11000, 150, 2540.6,
                         "MALSR", "P4L", 3.10, false, true, false, 0, 0));
    a.ends.push_back(end("KEWR", "22R", geo::dms_to_deg(40, 42, 9.2158),
                         geo::dms_to_deg(74, 9, 43.8183, true), 8.5, 206, 11000, 150, 1440.9,
                         "MALSR", "P4L", 3.10, true, true, false, 0, 0));
    a.ends.push_back(end("KEWR", "04R", geo::dms_to_deg(40, 40, 39.3027),
                         geo::dms_to_deg(74, 10, 27.2825, true), 11.0, 26, 9999, 150, 1190.2,
                         "ALSF-2", "P4L", 2.95, false, true, false, 0, 0));
    a.ends.push_back(end("KEWR", "22L", geo::dms_to_deg(40, 42, 8.2401),
                         geo::dms_to_deg(74, 9, 30.7356, true), 9.0, 206, 9999, 150, 1794.1,
                         "ALSF-2", "P4L", 3.00, false, true, false, 0, 0));
    a.ends.push_back(end("KEWR", "11", geo::dms_to_deg(40, 42, 10.0957),
                         geo::dms_to_deg(74, 10, 50.5467, true), 17.5, 95, 6725, 150, 0, "NONE",
                         "P4R", 3.00, true, true, true, 0, 0));
    a.ends.push_back(end("KEWR", "29", geo::dms_to_deg(40, 42, 4.3179),
                         geo::dms_to_deg(74, 9, 23.5566, true), 9.9, 275, 6725, 150, 224, "NONE",
                         "P4R", 3.00, true, true, true, 0, 0));
    return a;
}

const FaaAirportRef* faa_airport(const std::string& icao) {
    const std::string id = up(icao);
    if (id == "KJFK")
        return &faa_kjfk();
    if (id == "KEWR")
        return &faa_kewr();
    return nullptr;
}

std::vector<const FaaAirportRef*> faa_all() {
    return {&faa_kjfk(), &faa_kewr()};
}

const FaaEnd* faa_end(const FaaAirportRef& a, const std::string& ident) {
    const std::string id = up(ident);
    for (const auto& e : a.ends) {
        if (up(e.ident) == id)
            return &e;
    }
    return nullptr;
}

} // namespace s376
