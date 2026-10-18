#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace s376::dxt {

// BC1 / DXT1: 8 bytes per 4×4 block, 4:1 vs 24-bit RGB (plus a 128-byte DDS header).
// Good enough for draped satellite/hillshade. Not a video encoder.

struct RgbImage {
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> rgb;
};

// Encode tightly packed RGB8 (row-major, top-down) to a DDS BC1 file.
void write_dds_bc1(const std::string& path, const std::uint8_t* rgb, int w, int h);

// Encode and also return the raw BC1 blocks (no header) for tests.
std::vector<std::uint8_t> encode_bc1(const std::uint8_t* rgb, int w, int h);

// Decode BC1 blocks back to RGB8 (for tests / fallback if OSG has no DDS plugin).
std::vector<std::uint8_t> decode_bc1(const std::uint8_t* blocks, int w, int h);

std::size_t bc1_size(int w, int h);

} // namespace s376::dxt
