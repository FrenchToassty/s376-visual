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
    // Optional seat offset in aircraft body (m). 0 = host eyepoint (true
    // duplicate front). FO mid_right/far_right may set eye_right_m > 0.
    double eye_fwd_m = 0;
    double eye_right_m = 0;
    double eye_up_m = 0;
    int monitor = 0;
    int width = 1920;
    int height = 1080;
    bool fullscreen = false;
    bool enabled = true;
    int window_x = -1; // -1 = auto (monitor or tiled)
    int window_y = -1;
    // Original Raster: Front=0, Left=1, Right=2. −1 = not a channel PC.
    int raster_ch = -1;
    bool flip_vertical = false; // Left channel was flipped
    // "channel" draws a collimated view. "host_io" is operator + host I/O.
    std::string role = "channel";
    std::string correction_file = "data/display/correction.toml";
    std::string pattern; // none | grid5 | white70 | black
};

struct WindowPlacement {
    std::string name;
    int screen = 0;
    int x = 80;
    int y = 80;
    int w = 1920;
    int h = 1080;
    bool fullscreen = false;
};

struct HostConfig {
    std::string bind = "0.0.0.0:2300";
    std::uint16_t reply_port = 2301;
    xt4::WordOrder word_order = xt4::WordOrder::Auto;
    xt4::FloatFmt float_fmt = xt4::FloatFmt::Auto;
    bool hat_responder = true;
    // Empty = this process replies (single-box / laptop). Otherwise only the
    // process whose selected channel list includes this name sends type-200.
    // Four projector PCs share one TOML; set reply_master = "mid_left".
    std::string reply_master = "mid_left";
    bool allow_sim_lan = false;
    bool allow_lan = false;
    // Empty = reply to the sender. Host:port (or host, using reply_port).
    // Never default a simulator-LAN address.
    std::string reply_to;
};

struct RenderConfig {
    std::string engine = "auto"; // auto | osgearth | osg | null
    std::string earth_file = "data/earth/s376.earth";
    std::string cache_dir;
    double frame_rate = 60.0;
    std::string window_title = "s376-visual";
    bool vsync = true;
    int msaa = 4;  // 0 off; 4 is GL 3.3 / Maxwell-safe
    int aniso = 16; // 1 off; 16 is typical
    bool gl_debug = false; // OSG per-attribute GL error checks + notify
    std::string quality = "gtx980ti"; // gtx980ti | rtx | custom
    bool hdr = true;              // ACES tonemap
    bool atmosphere = true;       // analytic sky + aerial perspective
    bool water = true;            // Fresnel water + soft coast mask
    bool markings = true;         // FAA AC 150/5340-1 painted markings
    bool detail_textures = true;  // procedural / CC0 asphalt+grass
    bool buildings = true;        // OSM extruded footprints
    bool ssao = true;             // cheap baked AO; one 980 Ti channel can afford it
    bool shadows = false;         // CSM toggle; still not drawn
    float exposure = 1.0f;
    bool sync_to_host = true;     // draw only on a new type-100 eyepoint
    int ram_mb = 2048;            // CPU resident budget (gtx980ti: 2 GB)
    int vram_mb = 5120;           // GPU texture budget (6 GB Maxwell − headroom)
    int mesh_n = 96;              // DEM mesh samples per tile side
};

void apply_quality_preset(RenderConfig& r, const std::string& name);

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
// When `selected` is non-empty, only those channels are printed and a
// `selected=name` line is added (so --channel mid_left does not look like far_right).
std::string dump_config(const AppConfig& cfg, const std::vector<std::string>& selected);

// name empty or "all" → every enabled channel; otherwise the named one.
std::vector<ChannelConfig> select_channels(const AppConfig& cfg, const std::string& name);

// Type-200 HAT: hat_responder must be on, and reply_master empty or selected.
bool hat_reply_enabled(const HostConfig& h, const std::vector<ChannelConfig>& selected);

// First configured name in `prefer`, else the first enabled channel.
std::string default_shot_channel(const AppConfig& cfg);

// Place windows: explicit window_x/y win; else one channel per monitor when
// num_screens > 1; else tile by heading (far_left … far_right) on one display.
std::vector<WindowPlacement> layout_channels(const std::vector<ChannelConfig>& channels,
                                             int num_screens, int screen_w, int screen_h);

xt4::WordOrder parse_word_order(const std::string& s);
xt4::FloatFmt parse_float_fmt(const std::string& s);

} // namespace s376
