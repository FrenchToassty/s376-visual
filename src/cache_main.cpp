#include "s376/dxt.hpp"
#include "s376/log.hpp"
#include "s376/safety.hpp"
#include "s376/terrain.hpp"
#include "s376/version.hpp"

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
           "  s376-cache synth-ksea [--out DIR] [--size N]\n"
           "  s376-cache from-geotiff DEM.tif [--imagery FILE] [--out DIR]\n"
           "  s376-cache info DIR\n"
           "\n"
           "Writes elev.s376dem + imagery.dds (BC1/DXT1) + region.toml.\n"
           "A 25 km KSEA box is a few hundred KiB; a Copernicus GLO-30 1° clip\n"
           "is tens of MiB — not the 857 GB uncompressed RSI CERTS farm.\n"
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
    int size = 128;
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
        else if (a == "--size")
            size = std::stoi(need("--size"));
        else if (a == "--imagery")
            imagery_path = need("--imagery");
        else if (a == "-h" || a == "--help") {
            usage();
            s376::log::shutdown();
            return 0;
        } else if (dem_path.empty() && a[0] != '-')
            dem_path = a;
        else {
            std::cerr << "unknown option " << a << "\n";
            usage();
            s376::log::shutdown();
            return 2;
        }
    }

    try {
        refuse_lan_path("cache dest", out);
        if (cmd == "synth-ksea") {
            s376::CacheRegion r = s376::generate_ksea_cache();
            if (size != 128)
                r.dem = s376::DemGrid::synth_ksea(size, size);
            r.imagery = s376::ImageRgb::hillshade(r.dem);
            r.name = "ksea";
            s376::write_cache(r, out);
            const auto packed = std::filesystem::file_size(out + "/elev.s376dem") +
                                std::filesystem::file_size(out + "/imagery.dds");
            std::cout << "wrote " << out << "  " << packed << " bytes (BC1 + s376dem)\n"
                      << "RSI CERTS for a comparable region was ~857 GB uncompressed.\n";
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
            if (!imagery_path.empty())
                r.imagery = s376::ImageRgb::load_jpeg_or_png(imagery_path);
            if (r.imagery.rgb.empty())
                r.imagery = s376::ImageRgb::hillshade(r.dem);
            s376::write_cache(r, out);
            std::cout << "wrote " << out << "  " << r.dem.w << "x" << r.dem.h << " from "
                      << dem_path << "\n";
            s376::log::shutdown();
            return 0;
        }
        if (cmd == "info") {
            const std::string dir = dem_path.empty() ? out : dem_path;
            auto r = s376::load_cache(dir);
            std::cout << "name bounds " << r.dem.west << "," << r.dem.south << " .. " << r.dem.east
                      << "," << r.dem.north << "\n"
                      << "grid " << r.dem.w << "x" << r.dem.h << "\n"
                      << "imagery " << r.imagery.w << "x" << r.imagery.h << " (hillshade fallback)\n"
                      << "dds " << r.imagery_dds << "\n"
                      << "gdal " << (s376::gdal_available() ? "yes" : "no") << "\n";
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
