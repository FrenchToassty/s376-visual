#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace s376 {

struct RgbImage {
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> rgb; // top-down, 3 bytes per pixel
};

RgbImage make_solid_rgb(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b);
RgbImage make_gradient_rgb(int w, int h);
void flip_vert(std::uint8_t* p, int w, int h, int comp);
void rgba_to_rgb(const std::uint8_t* rgba, int n, std::uint8_t* rgb);

bool write_ppm(const std::string& path, const RgbImage& im);
bool write_png(const std::string& path, const RgbImage& im);
// Write PPM and PNG. `path` may be .ppm, .png, or a stem.
// Returns the PPM path in `ppm_out` / PNG path in `png_out` when non-null.
bool write_shot_images(const std::string& path, const RgbImage& im, std::string* ppm_out = nullptr,
                       std::string* png_out = nullptr);
bool read_ppm(const std::string& path, RgbImage& im);

// True if every pixel is within `tol` of the expected solid / gradient.
bool pixels_near_solid(const RgbImage& im, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                       int tol = 2);
bool pixels_near_gradient(const RgbImage& im, int tol = 2);

// CPU self-test: write a gradient, read it back, check pixels. No GL.
bool self_test_shot_files(const std::string& dir, std::string& err);

} // namespace s376
