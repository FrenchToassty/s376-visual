#include "s376/config.hpp"
#include "s376/diagnostics.hpp"
#include "s376/image.hpp"
#include "s376/state.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

extern int g_fails;
extern int g_passes;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (cond) {                                                                                \
            ++g_passes;                                                                            \
        } else {                                                                                   \
            ++g_fails;                                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n";          \
        }                                                                                          \
    } while (0)

void test_image() {
    const auto solid = s376::make_solid_rgb(8, 6, 40, 80, 160);
    CHECK(solid.w == 8 && solid.h == 6);
    CHECK(solid.rgb.size() == 8u * 6u * 3u);
    CHECK(s376::pixels_near_solid(solid, 40, 80, 160, 0));
    CHECK(!s376::pixels_near_solid(solid, 255, 0, 0, 2));

    const auto grad = s376::make_gradient_rgb(16, 12);
    CHECK(grad.w == 16 && grad.h == 12);
    CHECK(s376::pixels_near_gradient(grad, 0));
    CHECK(grad.rgb[0] == 0 && grad.rgb[1] == 0 && grad.rgb[2] == 128);
    CHECK(grad.rgb[static_cast<std::size_t>((16 - 1) * 3)] == 255);

    std::vector<std::uint8_t> rgba = {10, 20, 30, 255, 40, 50, 60, 128};
    std::vector<std::uint8_t> rgb(6, 0);
    s376::rgba_to_rgb(rgba.data(), 2, rgb.data());
    CHECK(rgb[0] == 10 && rgb[1] == 20 && rgb[2] == 30);
    CHECK(rgb[3] == 40 && rgb[4] == 50 && rgb[5] == 60);

    std::uint8_t flip[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    s376::flip_vert(flip, 2, 2, 3);
    CHECK(flip[0] == 7 && flip[5] == 12 && flip[6] == 1 && flip[11] == 6);

    const auto tmp = (std::filesystem::temp_directory_path() / "s376-test-image").string();
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    std::string err;
    CHECK(s376::self_test_shot_files(tmp, err));
    CHECK(err.empty());
    CHECK(std::filesystem::exists(tmp + "/s376-shot-self.ppm"));
    CHECK(std::filesystem::exists(tmp + "/s376-shot-self.png"));
    CHECK(std::filesystem::file_size(tmp + "/s376-shot-self.png") > 32);

    s376::RgbImage back;
    CHECK(s376::read_ppm(tmp + "/s376-shot-self.ppm", back));
    CHECK(back.w == 64 && back.h == 48);
    CHECK(s376::pixels_near_gradient(back, 0));

    std::string ppm, png;
    CHECK(s376::write_shot_images(tmp + "/pair.png", solid, &ppm, &png));
    CHECK(ppm.find(".ppm") != std::string::npos);
    CHECK(png.find(".png") != std::string::npos);
    CHECK(std::filesystem::exists(ppm) && std::filesystem::exists(png));

    CHECK(s376::looks_like_video_path("out.mp4"));
    CHECK(s376::looks_like_video_path("/tmp/jfk.MOV"));
    CHECK(!s376::looks_like_video_path("/tmp/frames"));
    CHECK(s376::record_frames_dir("out.mp4") == "out-frames");
    CHECK(s376::record_frames_dir("/tmp/jfk-frames") == "/tmp/jfk-frames");
    CHECK(s376::frame_png_path("/tmp/f", 1).find("frame-000001.png") != std::string::npos);
    const auto raw = s376::ffmpeg_rawvideo_cmd(1920, 1080, 30, "out.mp4");
    CHECK(raw.find("ffmpeg") != std::string::npos);
    CHECK(raw.find("1920x1080") != std::string::npos);
    CHECK(raw.find("rgb24") != std::string::npos);
    CHECK(raw.find("'out.mp4'") != std::string::npos);
    const auto mux = s376::ffmpeg_png_seq_cmd("out-frames", 30, "out.mp4");
    CHECK(mux.find("frame-%06d.png") != std::string::npos);
    CHECK(mux.find("'out.mp4'") != std::string::npos);
    CHECK(s376::shell_single_quote("a'b").find("'\\''") != std::string::npos);

    s376::RecordWriter rec;
    CHECK(rec.open(tmp + "/clip.mp4", 8, 6, 30, true));
    CHECK(!rec.used_ffmpeg());
    CHECK(rec.write(solid));
    CHECK(rec.write(solid));
    CHECK(rec.close());
    CHECK(rec.frames() == 2);
    CHECK(std::filesystem::exists(s376::frame_png_path(rec.frames_dir(), 1)));
    CHECK(std::filesystem::exists(s376::frame_png_path(rec.frames_dir(), 2)));
    CHECK(rec.mux_cmd().find("ffmpeg") != std::string::npos);

    if (s376::ffmpeg_on_path()) {
        const auto mp4 = tmp + "/tiny.mp4";
        const auto tiny = s376::make_solid_rgb(16, 16, 40, 80, 160);
        s376::RecordWriter pipe;
        CHECK(pipe.open(mp4, 16, 16, 30, false));
        CHECK(pipe.used_ffmpeg());
        CHECK(pipe.write(tiny));
        CHECK(pipe.write(tiny));
        CHECK(pipe.close());
        CHECK(pipe.frames() == 2);
        CHECK(std::filesystem::exists(mp4));
        CHECK(std::filesystem::file_size(mp4) > 32);
    }
    std::filesystem::remove_all(tmp);

    auto cfg = s376::default_config();
    const std::string all = s376::dump_config(cfg);
    CHECK(all.find("channel far_left") != std::string::npos);
    CHECK(all.find("channel far_right") != std::string::npos);
    CHECK(all.find("selected=") == std::string::npos);

    const std::string mid = s376::dump_config(cfg, {"mid_left"});
    CHECK(mid.find("selected=mid_left") != std::string::npos);
    CHECK(mid.find("channel mid_left") != std::string::npos);
    CHECK(mid.find("hdg=0") != std::string::npos);
    CHECK(mid.find("channel far_right") == std::string::npos);
    CHECK(mid.find("channel far_left") == std::string::npos);
    CHECK(mid.find("1920x1080") != std::string::npos);

    const std::string two = s376::dump_config(cfg, {"far_left", "mid_left"});
    CHECK(two.find("selected=far_left,mid_left") != std::string::npos);
    CHECK(two.find("channel far_left") != std::string::npos);
    CHECK(two.find("channel mid_left") != std::string::npos);
    CHECK(two.find("channel far_right") == std::string::npos);

    s376::HostState shot;
    shot.have_eyepoint = true;
    shot.eyepoints = 1;
    shot.datagrams = 0;
    shot.eye.lat_deg = 40.4932;
    shot.eye.lon_deg = -73.9191;
    shot.eye.alt_ft = 12000;
    shot.eye.heading_deg = -36.f;
    const auto dump = s376::diagnostics_dump(cfg, &shot, {"mid_left"});
    CHECK(dump.find("selected=mid_left") != std::string::npos);
    CHECK(dump.find("channel mid_left") != std::string::npos);
    CHECK(dump.find("channel far_right") == std::string::npos);
    CHECK(dump.find("datagrams=0") != std::string::npos);
    CHECK(dump.find("eyepoints=1") != std::string::npos);
    CHECK(dump.find("alt_ft=12000") != std::string::npos);
}
