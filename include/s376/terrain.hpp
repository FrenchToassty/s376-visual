#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace s376 {

// Row-major geodetic height field. Row 0 is north. Units: metres MSL.
struct DemGrid {
    int w = 0;
    int h = 0;
    double west = 0;  // lon
    double south = 0; // lat
    double east = 0;
    double north = 0;
    std::vector<float> z_m;

    bool empty() const { return w <= 1 || h <= 1 || z_m.size() != static_cast<std::size_t>(w * h); }
    bool in_bounds(double lat, double lon) const;
    bool sample_m(double lat, double lon, double& z_m_out) const;
    bool sample_ft(double lat, double lon, double& z_ft_out) const;

    static DemGrid synth_ksea(int w = 128, int h = 128);
    static DemGrid load(const std::string& path); // .s376dem
    void save(const std::string& path) const;

    static DemGrid load_geotiff(const std::string& path); // throws if no GDAL / bad file
    void save_geotiff(const std::string& path) const;
};

struct ImageRgb {
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> rgb; // w*h*3

    static ImageRgb hillshade(const DemGrid& dem);
    static ImageRgb load_jpeg_or_png(const std::string& path); // OSG/GDAL later; unused if empty
};

class HeightSource {
  public:
    virtual ~HeightSource() = default;
    // true if a local DEM covers the point. On miss, elev_msl_ft is unchanged.
    virtual bool sample_ft(double lat, double lon, double& elev_msl_ft) const = 0;
};

class DemHeightSource : public HeightSource {
  public:
    explicit DemHeightSource(std::shared_ptr<DemGrid> dem) : dem_(std::move(dem)) {}
    bool sample_ft(double lat, double lon, double& elev_msl_ft) const override;
    std::shared_ptr<DemGrid> grid() const { return dem_; }

  private:
    std::shared_ptr<DemGrid> dem_;
};

struct CacheRegion {
    std::string dir;
    std::string name = "region";
    DemGrid dem;
    ImageRgb imagery;
    std::string imagery_dds; // path, may be empty
};

CacheRegion generate_ksea_cache();
void write_cache(const CacheRegion& r, const std::string& dir);
CacheRegion load_cache(const std::string& dir);
bool gdal_available();

} // namespace s376
