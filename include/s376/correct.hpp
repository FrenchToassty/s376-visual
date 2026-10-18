#pragma once

#include "s376/accuracy.hpp"
#include "s376/airport.hpp"
#include "s376/faa.hpp"
#include "s376/image.hpp"
#include "s376/terrain.hpp"

#include <string>
#include <vector>

namespace s376 {

// Deterministic correction layer. Imagery (public or Raster-local) is
// only texture. Official FAA NASR / AIP geometry always wins: we
// repaint runway surface, markings, displaced-threshold arrows/bars,
// and EMAS from the official ends, and log every action.

struct ImageryCorrection {
    std::string icao;
    std::string feature;
    std::string imagery;  // what the photo / Raster pad may show
    std::string official; // FAA / AIP value
    std::string action;
};

struct CorrectionReport {
    std::string icao;
    std::vector<ImageryCorrection> items;
    AccReport imagery;
    int painted = 0;
    bool ok() const { return imagery.fails.empty(); }
};

CorrectionReport plan_corrections(const Airport& a, const FaaAirportRef& faa);
std::string format_correction_report(const CorrectionReport& r);

// Draw official surface / markings / EMAS over a north-up RGB box.
// Used when imagery conflicts with FAA/AIP (old paint, extension, missing
// pavement). Returns pixels written.
int apply_correction_overlay(RgbImage& im, const GeoBox& box, const Airport& a);

// Doctor / CI: airport vs official, plus imagery geotransform if a box
// is supplied. Flags mismatches above kAccImageryM / kAccEndM.
AccReport check_imagery_vs_official(const Airport& a, const FaaAirportRef& faa,
                                    const GeoBox* box = nullptr, int w = 0, int h = 0);

} // namespace s376
