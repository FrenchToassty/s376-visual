#include "s376/dxt.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace s376::dxt {
namespace {

std::uint16_t pack565(int r, int g, int b) {
    return static_cast<std::uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void unpack565(std::uint16_t c, int& r, int& g, int& b) {
    r = ((c >> 11) & 31) * 255 / 31;
    g = ((c >> 5) & 63) * 255 / 63;
    b = (c & 31) * 255 / 31;
}

void encode_block(const std::uint8_t px[16 * 3], std::uint8_t out[8]) {
    int min_l = 1e9, max_l = -1;
    int min_i = 0, max_i = 0;
    for (int i = 0; i < 16; ++i) {
        const int l = px[i * 3] + 2 * px[i * 3 + 1] + px[i * 3 + 2];
        if (l < min_l) {
            min_l = l;
            min_i = i;
        }
        if (l > max_l) {
            max_l = l;
            max_i = i;
        }
    }
    std::uint16_t c0 = pack565(px[max_i * 3], px[max_i * 3 + 1], px[max_i * 3 + 2]);
    std::uint16_t c1 = pack565(px[min_i * 3], px[min_i * 3 + 1], px[min_i * 3 + 2]);
    if (c0 == c1)
        c0 = static_cast<std::uint16_t>(c0 ^ 0x0020); // force 4-color mode
    if (c0 < c1)
        std::swap(c0, c1);
    int r[4], g[4], b[4];
    unpack565(c0, r[0], g[0], b[0]);
    unpack565(c1, r[1], g[1], b[1]);
    r[2] = (2 * r[0] + r[1]) / 3;
    g[2] = (2 * g[0] + g[1]) / 3;
    b[2] = (2 * b[0] + b[1]) / 3;
    r[3] = (r[0] + 2 * r[1]) / 3;
    g[3] = (g[0] + 2 * g[1]) / 3;
    b[3] = (b[0] + 2 * b[1]) / 3;

    std::uint32_t idx = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0, best_d = 1e9;
        for (int k = 0; k < 4; ++k) {
            const int dr = px[i * 3] - r[k];
            const int dg = px[i * 3 + 1] - g[k];
            const int db = px[i * 3 + 2] - b[k];
            const int d = dr * dr + dg * dg + db * db;
            if (d < best_d) {
                best_d = d;
                best = k;
            }
        }
        idx |= static_cast<std::uint32_t>(best) << (2 * i);
    }
    out[0] = static_cast<std::uint8_t>(c0 & 0xFF);
    out[1] = static_cast<std::uint8_t>(c0 >> 8);
    out[2] = static_cast<std::uint8_t>(c1 & 0xFF);
    out[3] = static_cast<std::uint8_t>(c1 >> 8);
    out[4] = static_cast<std::uint8_t>(idx & 0xFF);
    out[5] = static_cast<std::uint8_t>((idx >> 8) & 0xFF);
    out[6] = static_cast<std::uint8_t>((idx >> 16) & 0xFF);
    out[7] = static_cast<std::uint8_t>((idx >> 24) & 0xFF);
}

} // namespace

std::size_t bc1_size(int w, int h) {
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    return static_cast<std::size_t>(bw * bh * 8);
}

std::vector<std::uint8_t> encode_bc1(const std::uint8_t* rgb, int w, int h) {
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(bw * bh * 8));
    std::uint8_t block[16 * 3];
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            for (int y = 0; y < 4; ++y) {
                const int iy = std::min(h - 1, by * 4 + y);
                for (int x = 0; x < 4; ++x) {
                    const int ix = std::min(w - 1, bx * 4 + x);
                    const int si = (iy * w + ix) * 3;
                    const int di = (y * 4 + x) * 3;
                    block[di] = rgb[si];
                    block[di + 1] = rgb[si + 1];
                    block[di + 2] = rgb[si + 2];
                }
            }
            encode_block(block, out.data() + (by * bw + bx) * 8);
        }
    }
    return out;
}

std::vector<std::uint8_t> decode_bc1(const std::uint8_t* blocks, int w, int h) {
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(w * h * 3));
    const int bw = (w + 3) / 4;
    const int bh = (h + 3) / 4;
    for (int by = 0; by < bh; ++by) {
        for (int bx = 0; bx < bw; ++bx) {
            const std::uint8_t* b = blocks + (by * bw + bx) * 8;
            const std::uint16_t c0 = static_cast<std::uint16_t>(b[0] | (b[1] << 8));
            const std::uint16_t c1 = static_cast<std::uint16_t>(b[2] | (b[3] << 8));
            int r[4], g[4], bl[4];
            unpack565(c0, r[0], g[0], bl[0]);
            unpack565(c1, r[1], g[1], bl[1]);
            if (c0 > c1) {
                r[2] = (2 * r[0] + r[1]) / 3;
                g[2] = (2 * g[0] + g[1]) / 3;
                bl[2] = (2 * bl[0] + bl[1]) / 3;
                r[3] = (r[0] + 2 * r[1]) / 3;
                g[3] = (g[0] + 2 * g[1]) / 3;
                bl[3] = (bl[0] + 2 * bl[1]) / 3;
            } else {
                r[2] = (r[0] + r[1]) / 2;
                g[2] = (g[0] + g[1]) / 2;
                bl[2] = (bl[0] + bl[1]) / 2;
                r[3] = g[3] = bl[3] = 0;
            }
            std::uint32_t idx = b[4] | (b[5] << 8) | (b[6] << 16) | (static_cast<std::uint32_t>(b[7]) << 24);
            for (int y = 0; y < 4; ++y) {
                const int iy = by * 4 + y;
                if (iy >= h)
                    continue;
                for (int x = 0; x < 4; ++x) {
                    const int ix = bx * 4 + x;
                    if (ix >= w)
                        continue;
                    const int k = static_cast<int>((idx >> (2 * (y * 4 + x))) & 3);
                    const int o = (iy * w + ix) * 3;
                    rgb[o] = static_cast<std::uint8_t>(r[k]);
                    rgb[o + 1] = static_cast<std::uint8_t>(g[k]);
                    rgb[o + 2] = static_cast<std::uint8_t>(bl[k]);
                }
            }
        }
    }
    return rgb;
}

void write_dds_bc1(const std::string& path, const std::uint8_t* rgb, int w, int h) {
    auto blocks = encode_bc1(rgb, w, h);
    std::ofstream o(path, std::ios::binary);
    if (!o)
        throw std::runtime_error("cannot write " + path);
    // DDS header (128 bytes), DXTn
    std::uint8_t hdr[128] = {};
    hdr[0] = 'D';
    hdr[1] = 'D';
    hdr[2] = 'S';
    hdr[3] = ' ';
    auto put32 = [&](int off, std::uint32_t v) {
        hdr[off] = static_cast<std::uint8_t>(v);
        hdr[off + 1] = static_cast<std::uint8_t>(v >> 8);
        hdr[off + 2] = static_cast<std::uint8_t>(v >> 16);
        hdr[off + 3] = static_cast<std::uint8_t>(v >> 24);
    };
    put32(4, 124);                  // dwSize
    put32(8, 0x00001007);           // caps | height | width | pixelformat
    put32(12, static_cast<std::uint32_t>(h));
    put32(16, static_cast<std::uint32_t>(w));
    put32(20, static_cast<std::uint32_t>(std::max(1, (w + 3) / 4) * 8)); // pitch
    put32(76, 32);                  // pf size
    put32(80, 0x4);                 // DDPF_FOURCC
    hdr[84] = 'D';
    hdr[85] = 'X';
    hdr[86] = 'T';
    hdr[87] = '1';
    put32(108, 0x1000); // DDSCAPS_TEXTURE
    o.write(reinterpret_cast<const char*>(hdr), 128);
    o.write(reinterpret_cast<const char*>(blocks.data()), static_cast<std::streamsize>(blocks.size()));
}

} // namespace s376::dxt
