#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "s376/image.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace s376 {
namespace {

std::string stem_of(const std::string& path) {
    auto dot = path.find_last_of('.');
    auto slash = path.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        return path.substr(0, dot);
    return path;
}

std::string ext_of(const std::string& path) {
    auto dot = path.find_last_of('.');
    auto slash = path.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        std::string e = path.substr(dot);
        for (char& c : e)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return e;
    }
    return {};
}

} // namespace

RgbImage make_solid_rgb(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    RgbImage im;
    im.w = std::max(1, w);
    im.h = std::max(1, h);
    im.rgb.assign(static_cast<std::size_t>(im.w * im.h * 3), 0);
    for (int i = 0; i < im.w * im.h; ++i) {
        im.rgb[static_cast<std::size_t>(i * 3 + 0)] = r;
        im.rgb[static_cast<std::size_t>(i * 3 + 1)] = g;
        im.rgb[static_cast<std::size_t>(i * 3 + 2)] = b;
    }
    return im;
}

RgbImage make_gradient_rgb(int w, int h) {
    RgbImage im;
    im.w = std::max(1, w);
    im.h = std::max(1, h);
    im.rgb.resize(static_cast<std::size_t>(im.w * im.h * 3));
    for (int y = 0; y < im.h; ++y) {
        for (int x = 0; x < im.w; ++x) {
            const int o = (y * im.w + x) * 3;
            im.rgb[static_cast<std::size_t>(o + 0)] =
                static_cast<std::uint8_t>(x * 255 / std::max(1, im.w - 1));
            im.rgb[static_cast<std::size_t>(o + 1)] =
                static_cast<std::uint8_t>(y * 255 / std::max(1, im.h - 1));
            im.rgb[static_cast<std::size_t>(o + 2)] = 128;
        }
    }
    return im;
}

void flip_vert(std::uint8_t* p, int w, int h, int comp) {
    if (!p || w < 1 || h < 2 || comp < 1)
        return;
    const int stride = w * comp;
    std::vector<std::uint8_t> row(static_cast<std::size_t>(stride));
    for (int y = 0; y < h / 2; ++y) {
        std::uint8_t* a = p + y * stride;
        std::uint8_t* b = p + (h - 1 - y) * stride;
        std::memcpy(row.data(), a, static_cast<std::size_t>(stride));
        std::memcpy(a, b, static_cast<std::size_t>(stride));
        std::memcpy(b, row.data(), static_cast<std::size_t>(stride));
    }
}

void rgba_to_rgb(const std::uint8_t* rgba, int n, std::uint8_t* rgb) {
    for (int i = 0; i < n; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
}

bool write_ppm(const std::string& path, const RgbImage& im) {
    if (im.w < 1 || im.h < 1 ||
        im.rgb.size() < static_cast<std::size_t>(im.w * im.h * 3))
        return false;
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    out << "P6\n" << im.w << " " << im.h << "\n255\n";
    out.write(reinterpret_cast<const char*>(im.rgb.data()),
              static_cast<std::streamsize>(im.w * im.h * 3));
    return static_cast<bool>(out);
}

bool write_png(const std::string& path, const RgbImage& im) {
    if (im.w < 1 || im.h < 1 ||
        im.rgb.size() < static_cast<std::size_t>(im.w * im.h * 3))
        return false;
    return stbi_write_png(path.c_str(), im.w, im.h, 3, im.rgb.data(), im.w * 3) != 0;
}

bool write_shot_images(const std::string& path, const RgbImage& im, std::string* ppm_out,
                       std::string* png_out) {
    const std::string stem = stem_of(path);
    const std::string ext = ext_of(path);
    std::string ppm = stem + ".ppm";
    std::string png = stem + ".png";
    if (ext == ".ppm")
        ppm = path;
    if (ext == ".png")
        png = path;
    const bool ok_ppm = write_ppm(ppm, im);
    const bool ok_png = write_png(png, im);
    if (ppm_out)
        *ppm_out = ppm;
    if (png_out)
        *png_out = png;
    return ok_ppm && ok_png;
}

bool read_ppm(const std::string& path, RgbImage& im) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::string magic;
    in >> magic;
    if (magic != "P6")
        return false;
    int w = 0, h = 0, maxv = 0;
    in >> w >> h >> maxv;
    if (w < 1 || h < 1 || maxv != 255)
        return false;
    in.get(); // single whitespace after header
    im.w = w;
    im.h = h;
    im.rgb.resize(static_cast<std::size_t>(w * h * 3));
    in.read(reinterpret_cast<char*>(im.rgb.data()), static_cast<std::streamsize>(im.rgb.size()));
    return static_cast<bool>(in);
}

bool pixels_near_solid(const RgbImage& im, std::uint8_t r, std::uint8_t g, std::uint8_t b, int tol) {
    if (im.w < 1 || im.h < 1 ||
        im.rgb.size() < static_cast<std::size_t>(im.w * im.h * 3))
        return false;
    for (int i = 0; i < im.w * im.h; ++i) {
        const int o = i * 3;
        if (std::abs(static_cast<int>(im.rgb[static_cast<std::size_t>(o + 0)]) - r) > tol)
            return false;
        if (std::abs(static_cast<int>(im.rgb[static_cast<std::size_t>(o + 1)]) - g) > tol)
            return false;
        if (std::abs(static_cast<int>(im.rgb[static_cast<std::size_t>(o + 2)]) - b) > tol)
            return false;
    }
    return true;
}

bool pixels_near_gradient(const RgbImage& im, int tol) {
    const auto expect = make_gradient_rgb(im.w, im.h);
    if (im.rgb.size() != expect.rgb.size())
        return false;
    for (std::size_t i = 0; i < im.rgb.size(); ++i) {
        if (std::abs(static_cast<int>(im.rgb[i]) - static_cast<int>(expect.rgb[i])) > tol)
            return false;
    }
    return true;
}

bool self_test_shot_files(const std::string& dir, std::string& err) {
    const std::string stem = dir.empty() ? "s376-shot-self" : (dir + "/s376-shot-self");
    const auto grad = make_gradient_rgb(64, 48);
    const auto solid = make_solid_rgb(16, 16, 40, 80, 160);
    std::string ppm, png;
    if (!write_shot_images(stem + ".ppm", grad, &ppm, &png)) {
        err = "write_shot_images failed";
        return false;
    }
    RgbImage back;
    if (!read_ppm(ppm, back) || !pixels_near_gradient(back, 0)) {
        err = "gradient PPM roundtrip failed";
        return false;
    }
    if (!write_ppm(stem + "-solid.ppm", solid) || !read_ppm(stem + "-solid.ppm", back) ||
        !pixels_near_solid(back, 40, 80, 160, 0)) {
        err = "solid PPM roundtrip failed";
        return false;
    }
    if (!write_png(stem + "-solid.png", solid)) {
        err = "write_png failed";
        return false;
    }
    err.clear();
    return true;
}

} // namespace s376
