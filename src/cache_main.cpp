#include "s376/airport.hpp"
#include "s376/diagnostics.hpp"
#include "s376/dxt.hpp"
#include "s376/fetch.hpp"
#include "s376/log.hpp"
#include "s376/safety.hpp"
#include "s376/terrain.hpp"
#include "s376/version.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void usage() {
    std::cerr
        << "s376-cache " << s376::version()
        << " — offline DEM / imagery packer (no RSI tiles)\n"
           "usage:\n"
           "  s376-cache fetch ICAO [--radius-km N] [--out DIR] [--dem auto|glo30|3dep]\n"
           "                       [--imagery auto|naip|s2|hillshade] [--dry-run]\n"
           "                       [--near-m M] [--near-dem-m M] [--mid-m M] [--far-m M]\n"
           "                       [--near-km N] [--approach-nm N] [--max-export N]\n"
           "  s376-cache synth-ksea [--out DIR] [--size N] [--lods]\n"
           "  s376-cache from-geotiff DEM.tif [--imagery FILE] [--out DIR] [--lods]\n"
           "  s376-cache pack-lods --from DIR --out DIR\n"
           "  s376-cache osm ICAO [--out DIR] [--radius-km N] [--dry-run]\n"
           "  s376-cache geojson ICAO [--out DIR]\n"
           "  s376-cache info DIR\n"
           "\n"
           "fetch downloads Copernicus GLO-30 (or USGS 3DEP) plus NAIP / Sentinel-2\n"
           "cloudless into BC1/BC7 tiles with mipmaps. High-res LOD near the field,\n"
           "coarse far out. Downloaded rasters stay under cache/ (gitignored).\n"
           "osm pulls Overpass aeroway=taxiway|apron (ODbL) into DIR/osm.json.\n"
           "geojson writes a FeatureCollection for the osgEarth airport layer.\n"
           "Never points at 10.1.95.0/24 or 192.168.173.0/24.\n";
}

bool looks_like_hostport(const std::string& s) {
    return s.find(':') != std::string::npos && s.find('/') == std::string::npos &&
           s.find('\\') == std::string::npos;
}

void refuse_lan_path(const std::string& label, const std::string& s) {
    if (!looks_like_hostport(s))
        return;
    s376::safety::Endpoint ep;
    std::string err;
    if (!s376::safety::parse_host_port(s, ep, err))
        return;
    (void)s376::safety::check_destination(ep.host, ep.port, label, false, false);
}

} // namespace

int main(int argc, char** argv) {
    s376::log::Options lo;
    lo.no_file = true;
    s376::log::setup(lo, "cache");

    if (argc < 2) {
        usage();
        s376::log::shutdown();
        return 2;
    }
    const std::string cmd = argv[1];
    if (cmd == "-h" || cmd == "--help") {
        usage();
        s376::log::shutdown();
        return 0;
    }
    if (cmd == "--version") {
        std::cout << s376::version() << "\n";
        s376::log::shutdown();
        return 0;
    }

    std::string out = "cache/ksea";
    std::string dem_path;
    std::string imagery_path;
    std::string from_dir;
    std::string icao;
    std::string dem_kind = "auto";
    std::string img_kind = "auto";
    int size = 128;
    double radius_km = 25;
    double near_m = 1.0;
    double near_dem_m = 2.0;
    double mid_m = 4.0;
    double far_m = 20.0;
    double near_km = 5.0;
    double approach_nm = 10.0;
    int max_export = 4000;
    bool lods = false;
    bool dry = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << flag << " needs an argument\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--out")
            out = need("--out");
        else if (a == "--from")
            from_dir = need("--from");
        else if (a == "--size")
            size = std::stoi(need("--size"));
        else if (a == "--radius-km")
            radius_km = std::stod(need("--radius-km"));
        else if (a == "--near-m")
            near_m = std::stod(need("--near-m"));
        else if (a == "--near-dem-m")
            near_dem_m = std::stod(need("--near-dem-m"));
        else if (a == "--mid-m")
            mid_m = std::stod(need("--mid-m"));
        else if (a == "--far-m")
            far_m = std::stod(need("--far-m"));
        else if (a == "--near-km")
            near_km = std::stod(need("--near-km"));
        else if (a == "--approach-nm")
            approach_nm = std::stod(need("--approach-nm"));
        else if (a == "--max-export")
            max_export = std::stoi(need("--max-export"));
        else if (a == "--dem")
            dem_kind = need("--dem");
        else if (a == "--imagery")
            img_kind = need("--imagery");
        else if (a == "--lods")
            lods = true;
        else if (a == "--dry-run")
            dry = true;
        else if (a == "-h" || a == "--help") {
            usage();
            s376::log::shutdown();
            return 0;
        }         else if (a[0] != '-' && icao.empty() &&
                   (cmd == "fetch" || cmd == "osm" || cmd == "geojson" || cmd == "info" ||
                    cmd == "from-geotiff" || cmd == "pack-lods")) {
            if (cmd == "fetch" || cmd == "osm" || cmd == "geojson")
                icao = a;
            else
                dem_path = a;
        } else {
            std::cerr << "unknown option " << a << "\n";
            usage();
            s376::log::shutdown();
            return 2;
        }
    }

    try {
        refuse_lan_path("cache dest", out);
        if (cmd == "fetch") {
            if (icao.empty()) {
                std::cerr << "fetch needs an ICAO (KSEA, KSFO, KLAS, …)\n";
                s376::log::shutdown();
                return 2;
            }
            s376::FetchRequest req;
            req.icao = icao;
            req.radius_km = radius_km;
            req.dem = dem_kind;
            req.imagery = img_kind;
            req.out_dir = out;
            req.dry_run = dry;
            req.near_m = near_m;
            req.near_dem_m = near_dem_m;
            req.mid_m = mid_m;
            req.far_m = far_m;
            req.near_radius_km = near_km;
            req.approach_nm = approach_nm;
            req.max_export = max_export;
            s376::FetchPlan plan;
            const auto bytes = s376::fetch_and_pack(req, &plan);
            std::cout << plan.icao << " dem=" << plan.dem_kind << " img=" << plan.img_kind
                      << " lods=" << plan.lods.size() << "\n";
            std::cout << "  estimate tiles img=" << plan.estimate.img_tiles
                      << " dem=" << plan.estimate.dem_tiles << " raw~"
                      << s376::format_byte_count(plan.estimate.est_raw_bytes) << " packed~"
                      << s376::format_byte_count(plan.estimate.est_packed_bytes) << "\n";
            for (const auto& lod : plan.lods) {
                std::cout << "  lod" << lod.level << " r=" << lod.radius_km << "km  img "
                          << lod.img_m << "m/px  dem " << lod.dem_m << "m/px"
                          << (lod.bc7 ? "  BC7" : "  BC1") << (lod.tiled ? "  tiled" : "") << "\n";
            }
            for (const auto& u : plan.urls)
                std::cout << "  url " << u << "\n";
            std::cout << plan.attribution;
            if (dry) {
                s376::log::shutdown();
                return 0;
            }
            std::cout << "wrote " << out << "  " << bytes << " bytes  "
                      << s376::count_cache_tiles(out) << " tiles (BC1/BC7 + s376dem + mips)\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "osm") {
            if (icao.empty()) {
                std::cerr << "osm needs an ICAO (KSEA, KSFO, KLAS, …)\n";
                s376::log::shutdown();
                return 2;
            }
            const auto ref = s376::lookup_airport_ref(icao);
            const double rkm = radius_km > 0 ? std::min(radius_km, 12.0) : 6.0;
            const auto box = s376::box_from_radius(ref.lat, ref.lon, rkm);
            const auto url =
                s376::overpass_aeroway_url(box.south, box.west, box.north, box.east);
            std::string dest = out;
            if (dest.empty() || dest == "cache/ksea")
                dest = std::string("cache/") + ref.icao;
            const auto query =
                s376::overpass_aeroway_query(box.south, box.west, box.north, box.east);
            std::cout << ref.icao << " overpass " << url << "\n";
            if (!s376::curl_glob_safe(url)) {
                std::cerr << "overpass URL is not curl-glob-safe (raw [ ]); this is a bug\n";
                s376::log::shutdown();
                return 1;
            }
            if (dry) {
                s376::log::shutdown();
                return 0;
            }
            std::filesystem::create_directories(dest);
            const std::string json_path = dest + "/osm.json";
            // POST the QL so [ ] never sit in the URL; GET with -g is the fallback.
            if (!s376::http_post_file("https://overpass-api.de/api/interpreter", query, json_path) &&
                !s376::http_get_file(url, json_path)) {
                std::cerr << "overpass download failed\n";
                s376::log::shutdown();
                return 1;
            }
            s376::Airport apt = s376::airport_by_icao(ref.icao);
            const int n = s376::try_merge_osm_file(apt, json_path);
            s376::write_airport_geojson(apt, dest + "/osm.geojson");
            std::cout << "wrote " << json_path << "  " << n << " aeroway ways (ODbL — © OSM)\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "geojson") {
            if (icao.empty()) {
                std::cerr << "geojson needs an ICAO (KSEA, KSFO, KLAS, …)\n";
                s376::log::shutdown();
                return 2;
            }
            s376::Airport apt = s376::airport_by_icao(icao);
            std::string dest = out;
            if (dest.empty() || dest == "cache/ksea")
                dest = std::string("cache/") + apt.icao;
            (void)s376::try_load_osm(apt, dest);
            const std::string path = dest + "/osm.geojson";
            if (!s376::write_airport_geojson(apt, path)) {
                std::cerr << "could not write " << path << "\n";
                s376::log::shutdown();
                return 1;
            }
            std::cout << "wrote " << path << "  " << apt.runways.size() << " runways  "
                      << apt.taxiways.size() << " taxi  " << apt.aprons.size() << " aprons\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "synth-ksea") {
            s376::CacheRegion r = s376::generate_ksea_cache();
            if (size != 128)
                r.dem = s376::DemGrid::synth_ksea(size, size);
            r.imagery = s376::ImageRgb::hillshade(r.dem);
            r.name = "ksea";
            if (lods) {
                const auto n = s376::pack_lods(r, out, s376::default_lods(25), true);
                std::cout << "wrote " << out << "  " << n << " bytes (LOD pyramid)\n";
            } else {
                s376::write_cache(r, out);
                const auto packed = std::filesystem::file_size(out + "/elev.s376dem") +
                                    std::filesystem::file_size(out + "/imagery.dds");
                std::cout << "wrote " << out << "  " << packed << " bytes (BC1 + s376dem)\n";
            }
            s376::write_airport_geojson(s376::ksea_public(), out + "/osm.geojson");
            std::cout << "RSI CERTS for a comparable region was ~857 GB uncompressed.\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "from-geotiff") {
            if (dem_path.empty()) {
                std::cerr << "from-geotiff needs a DEM GeoTIFF path\n";
                s376::log::shutdown();
                return 2;
            }
            if (!s376::gdal_available()) {
                std::cerr << "this binary was built without GDAL (install libgdal-dev, re-cmake)\n";
                s376::log::shutdown();
                return 1;
            }
            s376::CacheRegion r;
            r.name = "region";
            r.dem = s376::DemGrid::load_geotiff(dem_path);
            if (!imagery_path.empty() && img_kind != "auto")
                r.imagery = s376::ImageRgb::load_jpeg_or_png(imagery_path);
            else if (!imagery_path.empty())
                r.imagery = s376::ImageRgb::load_jpeg_or_png(imagery_path);
            if (r.imagery.rgb.empty())
                r.imagery = s376::ImageRgb::hillshade(r.dem);
            if (lods)
                s376::pack_lods(r, out, s376::default_lods(25), true);
            else
                s376::write_cache(r, out);
            std::cout << "wrote " << out << "  " << r.dem.w << "x" << r.dem.h << " from "
                      << dem_path << "\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "pack-lods") {
            const std::string src = from_dir.empty() ? dem_path : from_dir;
            if (src.empty()) {
                std::cerr << "pack-lods needs --from DIR\n";
                s376::log::shutdown();
                return 2;
            }
            auto r = s376::load_cache(src);
            const auto n = s376::pack_lods(r, out, s376::default_lods(25), true);
            std::cout << "wrote " << out << "  " << n << " bytes\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "info") {
            const std::string dir = dem_path.empty() ? out : dem_path;
            auto r = s376::load_cache(dir);
            std::cout << "name bounds " << r.dem.west << "," << r.dem.south << " .. " << r.dem.east
                      << "," << r.dem.north << "\n"
                      << "grid " << r.dem.w << "x" << r.dem.h << "\n"
                      << "imagery " << r.imagery.w << "x" << r.imagery.h << "\n"
                      << "dds " << r.imagery_dds << "\n"
                      << "bytes " << s376::directory_bytes(dir) << "\n"
                      << "gdal " << (s376::gdal_available() ? "yes" : "no") << "\n";
            for (int i = 0; i < 4; ++i) {
                const auto lod = dir + "/lod" + std::to_string(i);
                if (std::filesystem::exists(lod + "/elev.s376dem"))
                    std::cout << "lod" << i << " " << s376::directory_bytes(lod) << " bytes\n";
            }
            double z = 0;
            if (r.dem.sample_ft(47.4479, -122.3103, z))
                std::cout << "sample KSEA ARP " << z << " ft MSL\n";
            s376::log::shutdown();
            return 0;
        }
        std::cerr << "unknown command " << cmd << "\n";
        usage();
        s376::log::shutdown();
        return 2;
    } catch (const s376::safety::UnsafeEndpoint& ex) {
        std::cerr << ex.what() << "\n";
        s376::log::shutdown();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        s376::log::shutdown();
        return 1;
    }
}
