#include "s376/terrain.hpp"
#include "s376/dxt.hpp"
#include "s376/geo.hpp"
#include "s376/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef S376_HAS_GDAL
#include <gdal_priv.h>
#include <cpl_conv.h>
#endif

namespace s376 {
namespace {

constexpr char kMagic[] = "S376DEM1";

void ensure_parent(const std::string& path) {
    const auto p = std::filesystem::path(path).parent_path();
    if (!p.empty())
        std::filesystem::create_directories(p);
}

} // namespace

bool DemGrid::in_bounds(double lat, double lon) const {
    return !empty() && lat >= south && lat <= north && lon >= west && lon <= east;
}

bool DemGrid::sample_m(double lat, double lon, double& z_m_out) const {
    if (!in_bounds(lat, lon))
        return false;
    const double u = (lon - west) / (east - west) * (w - 1);
    const double v = (north - lat) / (north - south) * (h - 1);
    const int x0 = std::max(0, std::min(w - 2, static_cast<int>(std::floor(u))));
    const int y0 = std::max(0, std::min(h - 2, static_cast<int>(std::floor(v))));
    const double tx = u - x0;
    const double ty = v - y0;
    const float a = z_m[static_cast<std::size_t>(y0 * w + x0)];
    const float b = z_m[static_cast<std::size_t>(y0 * w + x0 + 1)];
    const float c = z_m[static_cast<std::size_t>((y0 + 1) * w + x0)];
    const float d = z_m[static_cast<std::size_t>((y0 + 1) * w + x0 + 1)];
    z_m_out = a * (1 - tx) * (1 - ty) + b * tx * (1 - ty) + c * (1 - tx) * ty + d * tx * ty;
    return std::isfinite(z_m_out);
}

bool DemGrid::sample_ft(double lat, double lon, double& z_ft_out) const {
    double m = 0;
    if (!sample_m(lat, lon, m))
        return false;
    z_ft_out = m * geo::kMToFt;
    return true;
}

DemGrid DemGrid::synth_ksea(int w, int h) {
    DemGrid d;
    d.w = std::max(8, w);
    d.h = std::max(8, h);
    // ~25 km box around Sea-Tac. Public ARP 47.4479, -122.3103, 433 ft.
    d.west = -122.48;
    d.east = -122.14;
    d.south = 47.32;
    d.north = 47.58;
    d.z_m.assign(static_cast<std::size_t>(d.w * d.h), 132.f);
    const double field_lat = 47.4479;
    const double field_lon = -122.3103;
    for (int y = 0; y < d.h; ++y) {
        const double lat = d.north - (d.north - d.south) * y / (d.h - 1);
        for (int x = 0; x < d.w; ++x) {
            const double lon = d.west + (d.east - d.west) * x / (d.w - 1);
            const double dlat = (lat - field_lat) * 111320.0;
            const double dlon = (lon - field_lon) * 111320.0 * std::cos(geo::deg2rad(field_lat));
            const double r = std::sqrt(dlat * dlat + dlon * dlon);
            float z = 132.f;
            // Puget Sound to the west a bit lower; Cascades foothills to the east.
            z += static_cast<float>(std::max(0.0, (lon + 122.31) * 400.0));
            z += static_cast<float>(20.0 * std::sin(dlat * 0.001) * std::cos(dlon * 0.001));
            if (r < 2500.0)
                z = 132.f; // flatten the airport
            else if (r < 4000.0)
                z = 132.f + (z - 132.f) * static_cast<float>((r - 2500.0) / 1500.0);
            d.z_m[static_cast<std::size_t>(y * d.w + x)] = z;
        }
    }
    return d;
}

void DemGrid::save(const std::string& path) const {
    ensure_parent(path);
    std::ofstream o(path, std::ios::binary);
    if (!o)
        throw std::runtime_error("cannot write " + path);
    o.write(kMagic, 8);
    o.write(reinterpret_cast<const char*>(&w), 4);
    o.write(reinterpret_cast<const char*>(&h), 4);
    o.write(reinterpret_cast<const char*>(&west), 8);
    o.write(reinterpret_cast<const char*>(&south), 8);
    o.write(reinterpret_cast<const char*>(&east), 8);
    o.write(reinterpret_cast<const char*>(&north), 8);
    o.write(reinterpret_cast<const char*>(z_m.data()), static_cast<std::streamsize>(z_m.size() * 4));
}

DemGrid DemGrid::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot read " + path);
    char mag[8];
    in.read(mag, 8);
    if (std::memcmp(mag, kMagic, 8) != 0)
        throw std::runtime_error(path + " is not an s376 DEM");
    DemGrid d;
    in.read(reinterpret_cast<char*>(&d.w), 4);
    in.read(reinterpret_cast<char*>(&d.h), 4);
    in.read(reinterpret_cast<char*>(&d.west), 8);
    in.read(reinterpret_cast<char*>(&d.south), 8);
    in.read(reinterpret_cast<char*>(&d.east), 8);
    in.read(reinterpret_cast<char*>(&d.north), 8);
    if (d.w <= 0 || d.h <= 0 || d.w > 16384 || d.h > 16384)
        throw std::runtime_error(path + " has implausible DEM size");
    d.z_m.resize(static_cast<std::size_t>(d.w * d.h));
    in.read(reinterpret_cast<char*>(d.z_m.data()), static_cast<std::streamsize>(d.z_m.size() * 4));
    if (!in)
        throw std::runtime_error(path + " truncated");
    return d;
}

#ifdef S376_HAS_GDAL
bool gdal_available() {
    return true;
}

DemGrid DemGrid::load_geotiff(const std::string& path) {
    GDALAllRegister();
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
    if (!ds)
        throw std::runtime_error("GDAL could not open " + path);
    DemGrid d;
    d.w = ds->GetRasterXSize();
    d.h = ds->GetRasterYSize();
    double gt[6] = {};
    if (ds->GetGeoTransform(gt) != CE_None) {
        GDALClose(ds);
        throw std::runtime_error(path + " has no geotransform");
    }
    d.west = gt[0];
    d.north = gt[3];
    d.east = gt[0] + gt[1] * d.w;
    d.south = gt[3] + gt[5] * d.h;
    if (d.south > d.north)
        std::swap(d.south, d.north);
    d.z_m.resize(static_cast<std::size_t>(d.w * d.h));
    CPLErr e = ds->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, d.w, d.h, d.z_m.data(), d.w, d.h,
                                              GDT_Float32, 0, 0);
    GDALClose(ds);
    if (e != CE_None)
        throw std::runtime_error("GDAL read failed for " + path);
    // If the file looks like feet (typical US NED), leave it — Copernicus is metres.
    return d;
}

void DemGrid::save_geotiff(const std::string& path) const {
    GDALAllRegister();
    GDALDriver* drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!drv)
        throw std::runtime_error("GDAL has no GTiff driver");
    ensure_parent(path);
    GDALDataset* ds = drv->Create(path.c_str(), w, h, 1, GDT_Float32, nullptr);
    if (!ds)
        throw std::runtime_error("GDAL could not create " + path);
    double gt[6] = {west, (east - west) / w, 0, north, 0, -(north - south) / h};
    ds->SetGeoTransform(gt);
    ds->SetProjection("GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
                      "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]");
    CPLErr we = ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, w, h, const_cast<float*>(z_m.data()),
                                               w, h, GDT_Float32, 0, 0);
    GDALClose(ds);
    if (we != CE_None)
        throw std::runtime_error("GDAL write failed for " + path);
}
#else
bool gdal_available() {
    return false;
}

DemGrid DemGrid::load_geotiff(const std::string& path) {
    throw std::runtime_error("GDAL was not built in; cannot read " + path +
                             " (install libgdal-dev and re-cmake)");
}

void DemGrid::save_geotiff(const std::string&) const {
    throw std::runtime_error("GDAL was not built in");
}
#endif

bool DemHeightSource::sample_ft(double lat, double lon, double& elev_msl_ft) const {
    if (!dem_)
        return false;
    return dem_->sample_ft(lat, lon, elev_msl_ft);
}

ImageRgb ImageRgb::hillshade(const DemGrid& dem) {
    ImageRgb im;
    im.w = dem.w;
    im.h = dem.h;
    im.rgb.resize(static_cast<std::size_t>(im.w * im.h * 3));
    const double dx = (dem.east - dem.west) / std::max(1, dem.w - 1) * 111320.0 *
                      std::cos(geo::deg2rad((dem.north + dem.south) * 0.5));
    const double dy = (dem.north - dem.south) / std::max(1, dem.h - 1) * 111320.0;
    for (int y = 0; y < im.h; ++y) {
        for (int x = 0; x < im.w; ++x) {
            const int x0 = std::max(0, x - 1), x1 = std::min(im.w - 1, x + 1);
            const int y0 = std::max(0, y - 1), y1 = std::min(im.h - 1, y + 1);
            const float zx0 = dem.z_m[static_cast<std::size_t>(y * dem.w + x0)];
            const float zx1 = dem.z_m[static_cast<std::size_t>(y * dem.w + x1)];
            const float zy0 = dem.z_m[static_cast<std::size_t>(y0 * dem.w + x)];
            const float zy1 = dem.z_m[static_cast<std::size_t>(y1 * dem.w + x)];
            const double dzx = (zx1 - zx0) / std::max(1e-3, dx * (x1 - x0));
            const double dzy = (zy0 - zy1) / std::max(1e-3, dy * (y1 - y0));
            double nx = -dzx, ny = -dzy, nz = 1.0;
            const double mag = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= mag;
            ny /= mag;
            nz /= mag;
            const double shade = std::max(0.0, nx * 0.4 + ny * 0.3 + nz * 0.85);
            const float z = dem.z_m[static_cast<std::size_t>(y * dem.w + x)];
            const bool water = z < 20.f;
            unsigned char r, g, b;
            if (water) {
                r = 30;
                g = 60;
                b = 110;
            } else {
                r = static_cast<unsigned char>(40 + 80 * shade);
                g = static_cast<unsigned char>(60 + 110 * shade);
                b = static_cast<unsigned char>(40 + 50 * shade);
            }
            const int o = (y * im.w + x) * 3;
            im.rgb[o] = r;
            im.rgb[o + 1] = g;
            im.rgb[o + 2] = b;
        }
    }
    return im;
}

ImageRgb ImageRgb::load_jpeg_or_png(const std::string& path) {
#ifdef S376_HAS_GDAL
    GDALAllRegister();
    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpen(path.c_str(), GA_ReadOnly));
    if (!ds)
        throw std::runtime_error("GDAL could not open imagery " + path);
    ImageRgb im;
    im.w = ds->GetRasterXSize();
    im.h = ds->GetRasterYSize();
    if (im.w <= 0 || im.h <= 0 || im.w > 32768 || im.h > 32768) {
        GDALClose(ds);
        throw std::runtime_error(path + " has implausible imagery size");
    }
    im.rgb.assign(static_cast<std::size_t>(im.w * im.h * 3), 0);
    const int nb = ds->GetRasterCount();
    if (nb >= 3) {
        int bands[3] = {1, 2, 3};
        CPLErr e = ds->RasterIO(GF_Read, 0, 0, im.w, im.h, im.rgb.data(), im.w, im.h, GDT_Byte, 3,
                                bands, 3, static_cast<GSpacing>(im.w) * 3, 1);
        GDALClose(ds);
        if (e != CE_None)
            throw std::runtime_error("GDAL imagery read failed for " + path);
        return im;
    }
    std::vector<std::uint8_t> gray(static_cast<std::size_t>(im.w * im.h));
    CPLErr e =
        ds->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, im.w, im.h, gray.data(), im.w, im.h, GDT_Byte,
                                       0, 0);
    GDALClose(ds);
    if (e != CE_None)
        throw std::runtime_error("GDAL imagery read failed for " + path);
    for (int i = 0; i < im.w * im.h; ++i) {
        im.rgb[static_cast<std::size_t>(i) * 3 + 0] = gray[static_cast<std::size_t>(i)];
        im.rgb[static_cast<std::size_t>(i) * 3 + 1] = gray[static_cast<std::size_t>(i)];
        im.rgb[static_cast<std::size_t>(i) * 3 + 2] = gray[static_cast<std::size_t>(i)];
    }
    return im;
#else
    (void)path;
    return {};
#endif
}

CacheRegion generate_ksea_cache() {
    CacheRegion r;
    r.name = "ksea";
    r.dem = DemGrid::synth_ksea(128, 128);
    r.imagery = ImageRgb::hillshade(r.dem);
    return r;
}

void write_cache(const CacheRegion& r, const std::string& dir) {
    ensure_parent(dir + "/elev.s376dem");
    r.dem.save(dir + "/elev.s376dem");
    if (!r.imagery.rgb.empty())
        dxt::write_dds_bc1(dir + "/imagery.dds", r.imagery.rgb.data(), r.imagery.w, r.imagery.h);
    std::ofstream t(dir + "/region.toml");
    t << "name = \"" << r.name << "\"\n";
    t << "west = " << r.dem.west << "\n";
    t << "south = " << r.dem.south << "\n";
    t << "east = " << r.dem.east << "\n";
    t << "north = " << r.dem.north << "\n";
    t << "width = " << r.dem.w << "\n";
    t << "height = " << r.dem.h << "\n";
    t << "elev = \"elev.s376dem\"\n";
    t << "imagery = \"imagery.dds\"\n";
    t << "format = \"bc1\"\n";
    const std::size_t raw = static_cast<std::size_t>(r.dem.w * r.dem.h) * 4 +
                            static_cast<std::size_t>(r.imagery.w * r.imagery.h) * 3;
    const std::size_t packed = static_cast<std::size_t>(r.dem.w * r.dem.h) * 4 + dxt::bc1_size(r.imagery.w, r.imagery.h);
    t << "# packed " << packed << " bytes vs raw " << raw << " (plus RSI CERTS was 857 GB uncompressed)\n";
}

CacheRegion load_cache(const std::string& dir) {
    CacheRegion r;
    r.dir = dir;
    r.dem = DemGrid::load(dir + "/elev.s376dem");
    r.imagery_dds = dir + "/imagery.dds";
    r.imagery = ImageRgb::hillshade(r.dem); // CPU copy for HAT-less drape fallback
    return r;
}

} // namespace s376
