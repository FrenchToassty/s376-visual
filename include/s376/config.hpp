#pragma once

#include "s376/xt4.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace s376 {

struct ChannelConfig {
    std::string name = "front";
    double heading_offset_deg = 0;
    double pitch_offset_deg = 0;
    double roll_offset_deg = 0;
    double hfov_deg = 50.0;
    double vfov_deg = 32.5;
    int monitor = 0;
    int width = 1920;
    int height = 1080;
    bool fullscreen = false;
    bool enabled = true;
};

struct HostConfig {
    std::string bind = "0.0.0.0:2300";
    std::uint16_t reply_port = 2301;
    xt4::WordOrder word_order = xt4::WordOrder::Auto;
    xt4::FloatFmt float_fmt = xt4::FloatFmt::Auto;
    bool hat_responder = true;
    bool allow_sim_lan = false;
    bool allow_lan = false;
};

struct RenderConfig {
    std::string engine = "auto"; // auto | osgearth | osg | null
    std::string earth_file = "data/earth/s376.earth";
    std::string cache_dir;
    double frame_rate = 60.0;
    std::string window_title = "s376-visual";
    bool vsync = true;
};

struct LogConfig {
    bool verbose = false;
    bool debug = false;
    std::string dir;
};

struct AppConfig {
    HostConfig host;
    RenderConfig render;
    LogConfig log;
    std::vector<ChannelConfig> channels;
};

AppConfig default_config();
AppConfig load_config(const std::string& path); // throws std::runtime_error
std::string dump_config(const AppConfig& cfg);

xt4::WordOrder parse_word_order(const std::string& s);
xt4::FloatFmt parse_float_fmt(const std::string& s);

} // namespace s376
