#pragma once

#include "s376/geo.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace s376 {

// Our own packet-112 switch numbers (not a vendor list). Runway 1
// defaults: see data/lights/switches.toml (base 0 + offsets 1..8).
constexpr int kSwApproach = 1;
constexpr int kSwStrobe = 2;
constexpr int kSwRunwayEdge = 3;
constexpr int kSwCenterline = 4;
constexpr int kSwVasi = 5;
constexpr int kSwThreshold = 6; // touchdown zone
constexpr int kSwTaxiEdge = 41;
constexpr int kSwStopBar = 56;
constexpr int kSwTaxiCenterline = 60;

enum class LightKind {
    Edge,
    Centerline,
    Approach,
    Threshold,
    Strobe,
    Vasi,
    TaxiEdge,
    StopBar,
    TaxiCenterline,
    Flasher,
    Reil,
    Rwsl,
    End
};

struct LightPoint {
    double lat = 0;
    double lon = 0;
    double alt_m = 0;
    float r = 1;
    float g = 1;
    float b = 1;
    int switch_num = kSwRunwayEdge;
    LightKind kind = LightKind::Edge;
    int runway_k = 0; // 1..7, 0 = taxi / airport-wide
    int seq = -1;     // sequenced-flasher index, −1 = none
};

struct DataSource {
    std::string name;
    std::string effective; // NASR / AIP 28-day cycle
    std::string retrieved;
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
    double heading_deg = 0;    // LE true heading (from both ends)
    double heading_he_deg = 0; // HE true heading
    double length_m = 0;
    double width_m = 0;
    std::string approach_le = "MALSR";
    std::string approach_he = "MALSR";
    double le_displaced_ft = 0;
    double he_displaced_ft = 0;
    std::string papi_le = "P4L";
    std::string papi_he = "P4L";
    double papi_deg_le = 3.0;
    double papi_deg_he = 3.0;
    int papi_count_le = 1;
    int papi_count_he = 1;
    bool reil_le = false;
    bool reil_he = false;
    bool tdz_le = false;
    bool tdz_he = false;
    bool emas_le = false;
    bool emas_he = false;
    double emas_le_len_ft = 0;
    double emas_he_len_ft = 0;
    double emas_wid_ft = 0;
    DataSource source;
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
    DataSource source;
    int sw_edge = kSwRunwayEdge;
    int sw_centerline = kSwCenterline;
    int sw_approach = kSwApproach;
    int sw_threshold = kSwThreshold;
    int sw_taxi_edge = kSwTaxiEdge;
    int sw_stop_bar = kSwStopBar;
    int sw_taxi_centerline = kSwTaxiCenterline;
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
Airport kewr_public();
Airport kbos_public();
Airport airport_by_icao(const std::string& icao);
Airport airport_from_faa(const std::string& icao); // KJFK / KEWR from FAA both ends
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
// Load that JSON from a file (0 if missing). For bake tools only — the
// renderer must not open a 400 MB Overpass dump.
int try_merge_osm_file(Airport& a, const std::string& path);
// Stream-parse a (possibly huge) Overpass file. Never slurp the whole file.
int merge_osm_file_stream(Airport& a, const std::string& path, std::uint64_t max_bytes = 0);
// Compact runtime sidecar. The IG loads only this, never osm.json.
bool write_osm_bin(const Airport& a, const std::string& path);
int read_osm_bin(Airport& a, const std::string& path);
// Bake osm.json → osm.bin (stream parse). Returns ways written.
int bake_osm_bin(const std::string& json_path, const std::string& bin_path);
// Runtime: osm.bin only. Does not open osm.json.
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
