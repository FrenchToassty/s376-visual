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

struct Apron {
    std::string name;
    std::vector<std::pair<double, double>> ring; // lat, lon, closed or open
};

struct Building {
    std::string name;
    std::vector<std::pair<double, double>> ring;
    float height_m = 12;
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
    std::vector<Apron> aprons;
    std::vector<Building> buildings;
    std::vector<LightPoint> lights;
};

struct MeshVert {
    geo::Ecef p;
    geo::Ecef n;
    float r = 0.18f;
    float g = 0.18f;
    float b = 0.19f;
    float mat = 0; // 0 asphalt, 1 marking, 2 concrete, 3 wall, 4 roof
};

struct AirportMesh {
    std::vector<MeshVert> verts; // triangles, 3 at a time
};

struct ShotPose {
    std::string icao;
    std::string rwy;
    double dist_nm = 0;
    double lat = 0;
    double lon = 0;
    double alt_ft = 0;
    double hdg_true_deg = 0;
    double pitch_deg = 0;
    double roll_deg = 0;
};

struct ShotOverrides {
    bool have_alt = false;
    double alt_ft = 0;
    bool have_bank = false;
    double bank_deg = 0;
    bool have_hdg = false;
    double hdg_deg = 0;
    bool have_pitch = false;
    double pitch_deg = 0;
};

// Public OurAirports runway ends + FAA-chart lighting systems. Taxi
// connectors are schematic offsets from those runways (stand-in for an
// Overpass aeroway=* download). No RSI / CAE geometry.
Airport ksea_public();
Airport ksfo_public();
Airport klas_public();
Airport klax_public();
Airport kjfk_public();
Airport kbos_public();
Airport airport_by_icao(const std::string& icao);
std::vector<Airport> all_public_airports();

// `KJFK`, `KJFK:04R`, or `KJFK:04R:3` (nm before the threshold on a 3° path).
ShotPose parse_shot_at(const std::string& spec);
void apply_shot_overrides(ShotPose& p, const ShotOverrides& o);
bool runway_shot_pose(const Airport& a, const std::string& ident, double dist_nm, ShotPose& out);

// Merge Overpass `out geom` JSON (aeroway=taxiway|apron). Returns ways added.
int merge_osm_json(Airport& a, const std::string& json);
// Raw Overpass QL (may contain [ ]). Never pass this string to curl unquoted.
std::string overpass_aeroway_query(double south, double west, double north, double east);
// GET URL with [ ] { } percent-encoded so curl globbing cannot expand it.
std::string overpass_aeroway_url(double south, double west, double north, double east);
// Load that JSON from a file (0 if missing). Search cache/<icao>/osm.json etc.
int try_merge_osm_file(Airport& a, const std::string& path);
int try_load_osm(Airport& a, const std::string& cache_dir);

void generate_lights(Airport& a);
AirportMesh build_airport_mesh(const Airport& a, bool buildings = true, bool markings = true);

// GeoJSON FeatureCollection (runways / taxi / aprons) for an osgEarth
// feature_geom layer. Coordinates are lon,lat. Not an RSI extract.
std::string airport_geojson(const Airport& a);
int write_airport_geojson(const Airport& a, const std::string& path);

// Offset a geodetic point by metres along a true heading (local WGS84).
void offset_m(double lat, double lon, double heading_deg, double dist_m, double& olat,
              double& olon);

const char* light_kind_name(LightKind k);

} // namespace s376
