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
// `mips` writes a full mip chain (box filter) for GPU minification.
void write_dds_bc1(const std::string& path, const std::uint8_t* rgb, int w, int h,
                   bool mips = false);

// BC7 mode-6 (opaque). Larger blocks, better near-field colour. DX10 DDS.
void write_dds_bc7(const std::string& path, const std::uint8_t* rgb, int w, int h,
                   bool mips = false);

std::vector<std::uint8_t> encode_bc1(const std::uint8_t* rgb, int w, int h);
std::vector<std::uint8_t> encode_bc7(const std::uint8_t* rgb, int w, int h);
std::vector<std::uint8_t> decode_bc1(const std::uint8_t* blocks, int w, int h);

std::vector<std::uint8_t> downsample2(const std::uint8_t* rgb, int w, int h, int& ow, int& oh);

std::size_t bc1_size(int w, int h);
std::size_t bc7_size(int w, int h);
int mip_count(int w, int h);

enum class BlockFmt { Bc1, Bc7 };

// Compressed DDS payload (no RGBA decode). Used so the IG never inflates
// a 4000² BC7 tile to 48 MB of RGB on the CPU.
struct CompressedImage {
    int w = 0;
    int h = 0;
    int mips = 1;
    BlockFmt fmt = BlockFmt::Bc1;
    std::vector<std::uint8_t> blocks;
};

bool read_dds(const std::string& path, CompressedImage& out);

} // namespace s376::dxt
