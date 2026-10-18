#pragma once

#include <cstdint>
#include <cstdio>
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

bool looks_like_video_path(const std::string& path);
bool ffmpeg_on_path();
std::string shell_single_quote(const std::string& s);
std::string ffmpeg_rawvideo_cmd(int w, int h, double fps, const std::string& out_mp4);
std::string ffmpeg_png_seq_cmd(const std::string& frames_dir, double fps,
                               const std::string& out_mp4);
std::string record_frames_dir(const std::string& record_path);
std::string frame_png_path(const std::string& dir, int i);

// One-frame-at-a-time capture sink. Prefers an ffmpeg rawvideo pipe when
// `path` looks like a video and ffmpeg is on PATH; otherwise numbered PNGs.
// Never keeps more than one RGB frame in memory.
class RecordWriter {
  public:
    RecordWriter() = default;
    RecordWriter(const RecordWriter&) = delete;
    RecordWriter& operator=(const RecordWriter&) = delete;
    ~RecordWriter() { close(); }

    bool open(const std::string& path, int w, int h, double fps, bool force_png = false);
    bool write(const RgbImage& im);
    bool close();

    bool used_ffmpeg() const { return used_ffmpeg_; }
    int frames() const { return frames_; }
    std::string frames_dir() const { return frames_dir_; }
    std::string video_path() const { return video_path_; }
    std::string mux_cmd() const { return mux_cmd_; }

  private:
    std::FILE* pipe_ = nullptr;
    bool used_ffmpeg_ = false;
    int w_ = 0;
    int h_ = 0;
    int frames_ = 0;
    double fps_ = 30;
    std::string frames_dir_;
    std::string video_path_;
    std::string mux_cmd_;
};

} // namespace s376
