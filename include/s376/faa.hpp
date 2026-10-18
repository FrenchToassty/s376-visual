#pragma once

#include "s376/airport.hpp"

#include <string>
#include <vector>

namespace s376 {

// Authoritative runway-end records transcribed from public FAA AIP / NFDC
// (28-day NASR family) and cross-checked against OurAirports. The accuracy
// suite compares live airport objects to these rows. Effective dates are the
// NASR cycle current on the retrieval day.

struct FaaEnd {
    std::string icao;
    std::string ident;
    double lat = 0;
    double lon = 0;
    double elev_ft = 0;
    double true_brg_deg = 0;
    double length_ft = 0;
    double width_ft = 0;
    double displaced_ft = 0;
    std::string als;  // NONE | ALSF-2 | MALSR | RLLS
    std::string papi; // P4L | P4R | P4L+P4L
    double papi_deg = 3.0;
    bool reil = false;
    bool tdz = false;
    bool emas = false;
    double emas_len_ft = 0;
    double emas_wid_ft = 0;
    std::string source;
    std::string effective; // NASR / AIP cycle
    std::string retrieved;
};

struct FaaAirportRef {
    std::string icao;
    std::string name;
    double arp_lat = 0;
    double arp_lon = 0;
    double elev_ft = 0;
    std::string source;
    std::string effective;
    std::string retrieved;
    std::vector<FaaEnd> ends;
};

const FaaAirportRef& faa_kjfk();
const FaaAirportRef& faa_kewr();
const FaaAirportRef* faa_airport(const std::string& icao);
std::vector<const FaaAirportRef*> faa_all();

const FaaEnd* faa_end(const FaaAirportRef& a, const std::string& ident);

} // namespace s376
