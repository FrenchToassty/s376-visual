#pragma once

#include "s376/geo.hpp"

#include <string>
#include <vector>

namespace s376 {

// Our own packet-112 switch numbers (not RSI's). Documented in
// data/airports/ksea.toml so they can be lined up with the host later.
constexpr int kSwRunwayEdge = 1;
constexpr int kSwCenterline = 2;
constexpr int kSwApproach = 3;
constexpr int kSwThreshold = 4;

enum class LightKind { Edge, Centerline, Approach, Threshold };

struct LightPoint {
    double lat = 0;
    double lon = 0;
    double alt_m = 0;
    float r = 1;
    float g = 1;
    float b = 1;
    int switch_num = kSwRunwayEdge;
    LightKind kind = LightKind::Edge;
};

struct Runway {
    std::string ident_le;
    std::string ident_he;
    double le_lat = 0;
    double le_lon = 0;
    double le_elev_m = 0;
    double he_lat = 0;
    double he_lon = 0;
    double he_elev_m = 0;
    double heading_deg = 0; // LE true heading
    double length_m = 0;
    double width_m = 0;
    std::string approach_le = "MALSR";
    std::string approach_he = "MALSR";
};

struct Taxiway {
    std::string name;
    std::vector<std::pair<double, double>> path; // lat, lon
    double width_m = 23;
};

struct Airport {
    std::string icao;
    std::string name;
    std::string attribution;
    double arp_lat = 0;
    double arp_lon = 0;
    double elev_m = 0;
    int sw_edge = kSwRunwayEdge;
    int sw_centerline = kSwCenterline;
    int sw_approach = kSwApproach;
    int sw_threshold = kSwThreshold;
    std::vector<Runway> runways;
    std::vector<Taxiway> taxiways;
    std::vector<LightPoint> lights;
};

struct MeshVert {
    geo::Ecef p;
    geo::Ecef n;
    float r = 0.18f;
    float g = 0.18f;
    float b = 0.19f;
};

struct AirportMesh {
    std::vector<MeshVert> verts; // triangles, 3 at a time
};

// Public OurAirports runway ends + FAA-chart lighting systems. Taxi
// connectors are schematic offsets from those runways (stand-in for an
// Overpass aeroway=* download). No RSI / CAE geometry.
Airport ksea_public();

void generate_lights(Airport& a);
AirportMesh build_airport_mesh(const Airport& a);

// Offset a geodetic point by metres along a true heading (local WGS84).
void offset_m(double lat, double lon, double heading_deg, double dist_m, double& olat,
              double& olon);

const char* light_kind_name(LightKind k);

} // namespace s376
