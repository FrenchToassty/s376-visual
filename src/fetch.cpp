#include "s376/fetch.hpp"
#include "s376/dxt.hpp"
#include "s376/geo.hpp"
#include "s376/log.hpp"
#include "s376/safety.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace s376 {
namespace {

void ensure_dir(const std::string& dir) {
    std::filesystem::create_directories(dir);
}

std::string icao_upper(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

int shell_quote_ok(const std::string& s) {
    return s.find('\'') == std::string::npos && s.find('\n') == std::string::npos;
}

} // namespace

GeoBox box_from_radius(double lat, double lon, double radius_km) {
    const double r_m = radius_km * 1000.0;
    const double dlat = geo::rad2deg(r_m / geo::kWgsA);
    const double cl = std::max(std::cos(geo::deg2rad(lat)), 1e-6);
    const double dlon = geo::rad2deg(r_m / (geo::kWgsA * cl));
    GeoBox b;
    b.south = lat - dlat;
    b.north = lat + dlat;
    b.west = lon - dlon;
    b.east = lon + dlon;
    return b;
}

std::vector<LodSpec> default_lods(double radius_km) {
    std::vector<LodSpec> out;
    LodSpec near;
    near.level = 0;
    near.radius_km = std::min(8.0, radius_km);
    near.dem_n = 384;
    near.img_n = 768;
    near.bc7 = true;
    out.push_back(near);
    if (radius_km > 8.5) {
        LodSpec mid;
        mid.level = 1;
        mid.radius_km = std::min(25.0, radius_km);
        mid.dem_n = 256;
        mid.img_n = 512;
        out.push_back(mid);
    }
    if (radius_km > 26.0) {
        LodSpec far;
        far.level = 2;
        far.radius_km = radius_km;
        far.dem_n = 128;
        far.img_n = 256;
        out.push_back(far);
    }
    return out;
}

AirportRef lookup_airport_ref(const std::string& icao) {
    const std::string id = icao_upper(icao);
    static const AirportRef kAll[] = {
        {"KSEA", "Seattle-Tacoma International", 47.4502, -122.3088, 433, true},
        {"KSFO", "San Francisco International", 37.6188, -122.3754, 13, true},
        {"KLAS", "Harry Reid / McCarran", 36.0801, -115.1523, 2181, true},
        {"KLAX", "Los Angeles International", 33.9425, -118.4081, 128, true},
        {"KJFK", "John F Kennedy International", 40.6399, -73.7787, 13, true},
        {"EGLL", "London Heathrow", 51.4700, -0.4543, 83, false},
        {"EDDF", "Frankfurt Main", 50.0379, 8.5622, 364, false},
    };
    for (const auto& a : kAll) {
        if (a.icao == id)
            return a;
    }
    throw std::runtime_error("unknown ICAO '" + icao + "' (add it to lookup_airport_ref)");
}

std::string glo30_tile_url(int lat_deg, int lon_deg) {
    // 1° COG on the public AWS Open Data bucket (no credentials).
    char ns[16], ew[16];
    const int alat = std::min(90, std::abs(lat_deg));
    const int alon = std::min(180, std::abs(lon_deg));
    std::snprintf(ns, sizeof(ns), "%c%02d", lat_deg >= 0 ? 'N' : 'S', alat);
    std::snprintf(ew, sizeof(ew), "%c%03d", lon_deg >= 0 ? 'E' : 'W', alon);
    std::ostringstream o;
    o << "https://copernicus-dem-30m.s3.eu-central-1.amazonaws.com/"
      << "Copernicus_DSM_COG_10_" << ns << "_00_" << ew << "_00_DEM/"
      << "Copernicus_DSM_COG_10_" << ns << "_00_" << ew << "_00_DEM.tif";
    return o.str();
}

std::vector<std::string> glo30_urls_for_box(const GeoBox& b) {
    std::vector<std::string> u;
    const int lat0 = static_cast<int>(std::floor(b.south));
    const int lat1 = static_cast<int>(std::floor(b.north - 1e-9));
    const int lon0 = static_cast<int>(std::floor(b.west));
    const int lon1 = static_cast<int>(std::floor(b.east - 1e-9));
    for (int la = lat0; la <= lat1; ++la) {
        for (int lo = lon0; lo <= lon1; ++lo)
            u.push_back(glo30_tile_url(la, lo));
    }
    return u;
}

std::string usgs_export_url(const char* service, const GeoBox& b, int w, int h, const char* format,
                            const char* pixel) {
    std::ostringstream o;
    o << "https://elevation.nationalmap.gov/arcgis/rest/services/" << service
      << "/ImageServer/exportImage?bbox=" << b.west << "," << b.south << "," << b.east << ","
      << b.north << "&bboxSR=4326&imageSR=4326&size=" << w << "," << h << "&format=" << format
      << "&f=image";
    if (pixel)
        o << "&pixelType=" << pixel << "&interpolation=RSP_BilinearInterpolation";
    return o.str();
}

std::string s2_tile_url(int z, int x, int y) {
    std::ostringstream o;
    o << "https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless-2023_3857/default/g/" << z << "/" << y
      << "/" << x << ".jpg";
    return o.str();
}

bool http_get_file(const std::string& url, const std::string& dest) {
    safety::check_data_url(url);
    if (!shell_quote_ok(url) || !shell_quote_ok(dest))
        throw std::runtime_error("refusing curl path with a quote");
    ensure_dir(std::filesystem::path(dest).parent_path().string());
    const char* curl = "curl";
#ifdef _WIN32
    curl = "curl.exe";
#endif
    std::ostringstream cmd;
    cmd << curl << " -fsSL --max-time 180 --retry 2 -A "
        << "'s376-visual/0.1 (+https://github.com/joeylawell/s376-visual)' -o '" << dest << "' '"
        << url << "'";
    S376_LOG_INFO("cache", "GET %s", url.c_str());
    const int rc = std::system(cmd.str().c_str());
    if (rc != 0)
        return false;
    return std::filesystem::exists(dest) && std::filesystem::file_size(dest) > 32;
}

FetchPlan plan_fetch(const FetchRequest& req) {
    const AirportRef apt = lookup_airport_ref(req.icao);
    FetchPlan p;
    p.icao = apt.icao;
    p.lods = default_lods(req.radius_km);
    std::string dem = req.dem;
    std::string img = req.imagery;
    if (dem == "auto")
        dem = apt.conus ? "3dep" : "glo30";
    if (img == "auto")
        img = apt.conus ? "naip" : "s2";
    p.dem_kind = dem;
    p.img_kind = img;

    std::ostringstream attr;
    attr << "Packed by s376-cache for " << apt.icao << " (" << apt.name << ").\n";
    if (dem == "glo30") {
        attr << "Elevation: Copernicus GLO-30 (© European Union, Copernicus Programme; "
                "licence https://spacedata.copernicus.eu).\n";
        p.urls = glo30_urls_for_box(box_from_radius(apt.lat, apt.lon, req.radius_km));
    } else if (dem == "3dep") {
        attr << "Elevation: USGS 3DEP (public domain, US government work).\n";
        for (const auto& lod : p.lods) {
            const GeoBox b = box_from_radius(apt.lat, apt.lon, lod.radius_km);
            p.urls.push_back(usgs_export_url("3DEPElevation", b, lod.dem_n, lod.dem_n, "tiff", "F32"));
        }
        // GLO-30 fallback tiles
        auto g = glo30_urls_for_box(box_from_radius(apt.lat, apt.lon, req.radius_km));
        p.urls.insert(p.urls.end(), g.begin(), g.end());
    } else {
        throw std::runtime_error("dem must be auto|glo30|3dep");
    }
    if (img == "naip") {
        attr << "Imagery: USDA NAIP via USGS National Map ImageServer (US public domain).\n";
        for (const auto& lod : p.lods) {
            const GeoBox b = box_from_radius(apt.lat, apt.lon, lod.radius_km);
            p.urls.push_back("https://imagery.nationalmap.gov/arcgis/rest/services/USGSNAIPImagery/"
                             "ImageServer/exportImage?bbox=" +
                             std::to_string(b.west) + "," + std::to_string(b.south) + "," +
                             std::to_string(b.east) + "," + std::to_string(b.north) +
                             "&bboxSR=4326&imageSR=4326&size=" + std::to_string(lod.img_n) + "," +
                             std::to_string(lod.img_n) + "&format=jpg&f=image");
        }
    } else if (img == "s2") {
        attr << "Imagery: EOX Sentinel-2 cloudless 2023 (CC BY 4.0, Copernicus Sentinel data).\n";
        // A few z=12 tiles covering the ARP — exact mosaic is built at fetch time.
        p.urls.push_back(s2_tile_url(12, 0, 0));
    } else if (img == "hillshade") {
        attr << "Imagery: local hillshade (no download).\n";
    } else {
        throw std::runtime_error("imagery must be auto|naip|s2|hillshade");
    }
    attr << "Do not commit the downloaded rasters. See docs/SOURCES.md.\n";
    p.attribution = attr.str();
    return p;
}

DemGrid resample_dem(const DemGrid& src, const GeoBox& box, int w, int h) {
    DemGrid d;
    d.w = std::max(8, w);
    d.h = std::max(8, h);
    d.west = box.west;
    d.south = box.south;
    d.east = box.east;
    d.north = box.north;
    d.z_m.assign(static_cast<std::size_t>(d.w * d.h), 0.f);
    for (int y = 0; y < d.h; ++y) {
        const double lat = d.north - (d.north - d.south) * y / (d.h - 1);
        for (int x = 0; x < d.w; ++x) {
            const double lon = d.west + (d.east - d.west) * x / (d.w - 1);
            double z = 0;
            if (!src.sample_m(lat, lon, z))
                z = 0;
            d.z_m[static_cast<std::size_t>(y * d.w + x)] = static_cast<float>(z);
        }
    }
    return d;
}

ImageRgb resample_rgb(const ImageRgb& src, int w, int h) {
    ImageRgb im;
    im.w = std::max(4, w);
    im.h = std::max(4, h);
    im.rgb.assign(static_cast<std::size_t>(im.w * im.h * 3), 0);
    if (src.rgb.empty() || src.w < 1 || src.h < 1)
        return im;
    for (int y = 0; y < im.h; ++y) {
        const double v = (y + 0.5) * src.h / im.h;
        const int y0 = std::min(src.h - 1, std::max(0, static_cast<int>(v)));
        for (int x = 0; x < im.w; ++x) {
            const double u = (x + 0.5) * src.w / im.w;
            const int x0 = std::min(src.w - 1, std::max(0, static_cast<int>(u)));
            const int si = (y0 * src.w + x0) * 3;
            const int di = (y * im.w + x) * 3;
            im.rgb[di] = src.rgb[si];
            im.rgb[di + 1] = src.rgb[si + 1];
            im.rgb[di + 2] = src.rgb[si + 2];
        }
    }
    return im;
}

std::uint64_t directory_bytes(const std::string& dir) {
    if (!std::filesystem::exists(dir))
        return 0;
    std::uint64_t n = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
        if (e.is_regular_file())
            n += e.file_size();
    }
    return n;
}

namespace {

void write_lod_dir(const std::string& dir, const DemGrid& dem, const ImageRgb& img, bool bc7,
                   bool mips) {
    ensure_dir(dir);
    dem.save(dir + "/elev.s376dem");
    if (img.rgb.empty())
        return;
    if (bc7)
        dxt::write_dds_bc7(dir + "/imagery.dds", img.rgb.data(), img.w, img.h, mips);
    else
        dxt::write_dds_bc1(dir + "/imagery.dds", img.rgb.data(), img.w, img.h, mips);
}

} // namespace

std::uint64_t pack_lods(const CacheRegion& src, const std::string& dir, const std::vector<LodSpec>& lods,
                        bool mips) {
    ensure_dir(dir);
    const double clat = (src.dem.north + src.dem.south) * 0.5;
    const double clon = (src.dem.east + src.dem.west) * 0.5;
    ImageRgb base = src.imagery.rgb.empty() ? ImageRgb::hillshade(src.dem) : src.imagery;
    for (const auto& lod : lods) {
        const GeoBox box = box_from_radius(clat, clon, lod.radius_km);
        DemGrid dem = resample_dem(src.dem, box, lod.dem_n, lod.dem_n);
        ImageRgb img = resample_rgb(base, lod.img_n, lod.img_n);
        if (img.rgb.empty())
            img = ImageRgb::hillshade(dem);
        write_lod_dir(dir + "/lod" + std::to_string(lod.level), dem, img, lod.bc7, mips);
    }
    if (!lods.empty()) {
        // Finest LOD also at the root so HAT / older loaders keep working.
        const auto& lod0 = lods.front();
        const GeoBox box = box_from_radius(clat, clon, lod0.radius_km);
        CacheRegion root;
        root.name = src.name;
        root.dem = resample_dem(src.dem, box, lod0.dem_n, lod0.dem_n);
        root.imagery = resample_rgb(base, lod0.img_n, lod0.img_n);
        if (root.imagery.rgb.empty())
            root.imagery = ImageRgb::hillshade(root.dem);
        write_cache(root, dir);
        if (lod0.bc7)
            dxt::write_dds_bc7(dir + "/imagery.dds", root.imagery.rgb.data(), root.imagery.w,
                               root.imagery.h, mips);
        else
            dxt::write_dds_bc1(dir + "/imagery.dds", root.imagery.rgb.data(), root.imagery.w,
                               root.imagery.h, mips);
    }
    return directory_bytes(dir);
}

std::uint64_t fetch_and_pack(const FetchRequest& req, FetchPlan* plan_out) {
    FetchPlan plan = plan_fetch(req);
    if (plan_out)
        *plan_out = plan;
    const AirportRef apt = lookup_airport_ref(req.icao);
    const std::string out = req.out_dir.empty() ? ("cache/" + apt.icao) : req.out_dir;
    if (req.dry_run)
        return 0;

    ensure_dir(out);
    std::ofstream attr(out + "/ATTRIBUTION.txt");
    attr << plan.attribution;

    const std::string raw = out + "/raw";
    ensure_dir(raw);
    std::vector<DemGrid> dems;
    ImageRgb imagery;

    auto try_get = [&](const std::string& url, const std::string& dest) -> bool {
        try {
            return http_get_file(url, dest);
        } catch (const std::exception& ex) {
            S376_LOG_WARN("cache", "%s", ex.what());
            return false;
        }
    };

    if (plan.dem_kind == "3dep") {
        const GeoBox b = box_from_radius(apt.lat, apt.lon, std::min(8.0, req.radius_km));
        const std::string dest = raw + "/3dep.tif";
        const std::string url = usgs_export_url("3DEPElevation", b, 384, 384, "tiff", "F32");
        if (try_get(url, dest) && gdal_available()) {
            try {
                dems.push_back(DemGrid::load_geotiff(dest));
            } catch (const std::exception& ex) {
                S376_LOG_WARN("cache", "3DEP read: %s", ex.what());
            }
        }
    }
    if (dems.empty() && (plan.dem_kind == "glo30" || plan.dem_kind == "3dep")) {
        for (const auto& url : glo30_urls_for_box(box_from_radius(apt.lat, apt.lon, req.radius_km))) {
            const auto dest = raw + "/glo30-" + std::to_string(dems.size()) + ".tif";
            if (try_get(url, dest) && gdal_available()) {
                try {
                    dems.push_back(DemGrid::load_geotiff(dest));
                } catch (const std::exception& ex) {
                    S376_LOG_WARN("cache", "GLO-30 read: %s", ex.what());
                }
            }
        }
    }

    if (plan.img_kind == "naip") {
        const GeoBox b = box_from_radius(apt.lat, apt.lon, std::min(8.0, req.radius_km));
        const std::string dest = raw + "/naip.jpg";
        const std::string url =
            "https://imagery.nationalmap.gov/arcgis/rest/services/USGSNAIPImagery/"
            "ImageServer/exportImage?bbox=" +
            std::to_string(b.west) + "," + std::to_string(b.south) + "," + std::to_string(b.east) +
            "," + std::to_string(b.north) +
            "&bboxSR=4326&imageSR=4326&size=768,768&format=jpg&f=image";
        if (try_get(url, dest) && gdal_available()) {
            try {
                imagery = ImageRgb::load_jpeg_or_png(dest);
            } catch (const std::exception& ex) {
                S376_LOG_WARN("cache", "NAIP read: %s", ex.what());
            }
        }
    } else if (plan.img_kind == "s2") {
        // One z=11 tile covering the ARP is enough for a first pack; stitch later.
        const double lat = geo::deg2rad(apt.lat);
        const int z = 11;
        const int n = 1 << z;
        const int x = static_cast<int>(std::floor((apt.lon + 180.0) / 360.0 * n));
        const int y = static_cast<int>(std::floor(
            (1.0 - std::log(std::tan(lat) + 1.0 / std::cos(lat)) / geo::kPi) / 2.0 * n));
        const std::string dest = raw + "/s2.jpg";
        if (try_get(s2_tile_url(z, x, y), dest) && gdal_available()) {
            try {
                imagery = ImageRgb::load_jpeg_or_png(dest);
            } catch (const std::exception& ex) {
                S376_LOG_WARN("cache", "S2 read: %s", ex.what());
            }
        }
    }

    CacheRegion region;
    region.name = apt.icao;
    if (!dems.empty()) {
        region.dem = dems.front();
        for (std::size_t i = 1; i < dems.size(); ++i) {
            // Prefer the first (3DEP or first GLO-30). Extra tiles are used if the
            // primary misses a sample when we resample.
            (void)dems[i];
        }
        // If we have several GLO-30 tiles, resample from a merged sampler.
        if (dems.size() > 1) {
            const GeoBox box = box_from_radius(apt.lat, apt.lon, req.radius_km);
            DemGrid merged;
            merged.w = 256;
            merged.h = 256;
            merged.west = box.west;
            merged.south = box.south;
            merged.east = box.east;
            merged.north = box.north;
            merged.z_m.assign(256 * 256, 0.f);
            for (int y = 0; y < 256; ++y) {
                const double la = merged.north - (merged.north - merged.south) * y / 255.0;
                for (int x = 0; x < 256; ++x) {
                    const double lo = merged.west + (merged.east - merged.west) * x / 255.0;
                    double z = 0;
                    bool ok = false;
                    for (const auto& t : dems) {
                        if (t.sample_m(la, lo, z)) {
                            ok = true;
                            break;
                        }
                    }
                    merged.z_m[static_cast<std::size_t>(y * 256 + x)] =
                        ok ? static_cast<float>(z) : 0.f;
                }
            }
            region.dem = std::move(merged);
        }
    } else {
        S376_LOG_WARN("cache", "no DEM downloaded; packing synth pad for %s", apt.icao.c_str());
        region.dem = DemGrid::synth_ksea();
        region.dem.west = box_from_radius(apt.lat, apt.lon, req.radius_km).west;
        region.dem.east = box_from_radius(apt.lat, apt.lon, req.radius_km).east;
        region.dem.south = box_from_radius(apt.lat, apt.lon, req.radius_km).south;
        region.dem.north = box_from_radius(apt.lat, apt.lon, req.radius_km).north;
    }
    region.imagery = imagery.rgb.empty() ? ImageRgb::hillshade(region.dem) : imagery;
    const auto bytes = pack_lods(region, out, plan.lods, req.mips);
    // Drop raw GeoTIFFs after a successful pack — they are gitignored anyway
    // and a 1° GLO-30 COG is tens of MiB.
    std::error_code ec;
    std::filesystem::remove_all(raw, ec);
    const auto packed = directory_bytes(out);
    S376_LOG_INFO("cache", "packed %s  %llu bytes  dem=%s img=%s", apt.icao.c_str(),
                  static_cast<unsigned long long>(packed), plan.dem_kind.c_str(),
                  plan.img_kind.c_str());
    (void)bytes;
    return packed;
}

} // namespace s376
