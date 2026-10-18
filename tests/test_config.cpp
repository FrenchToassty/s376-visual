#include "s376/config.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

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
#define CHECK_NEAR(a, b, eps)                                                                      \
    do {                                                                                           \
        const double _a = static_cast<double>(a);                                                  \
        const double _b = static_cast<double>(b);                                                  \
        if (std::fabs(_a - _b) <= (eps)) {                                                         \
            ++g_passes;                                                                            \
        } else {                                                                                   \
            ++g_fails;                                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #a << "=" << _a << " "  \
                      << #b << "=" << _b << "\n";                                                  \
        }                                                                                          \
    } while (0)

#include <cmath>

void test_config() {
    auto d = s376::default_config();
    CHECK(d.channels.size() == 3);
    CHECK(d.channels[0].name == "front");
    CHECK_NEAR(d.channels[1].heading_offset_deg, 35.515, 1e-6);
    CHECK_NEAR(d.channels[2].heading_offset_deg, -35.25, 1e-6);
    CHECK_NEAR(d.channels[0].hfov_deg, 50.0, 1e-9);
    CHECK_NEAR(d.channels[0].vfov_deg, 32.5, 1e-9);
    CHECK(d.host.bind == "0.0.0.0:2300");
    CHECK(d.host.reply_port == 2301);

    const char* path = "s376-test-config.toml";
    {
        std::ofstream o(path);
        o << "[host]\nbind = \"127.0.0.1:2300\"\nword_order = \"be\"\nfloat_format = \"ieee\"\n"
             "hat_responder = false\n"
             "[render]\nengine = \"osg\"\nframe_rate = 30\n"
             "[[channels]]\nname = \"front\"\nheading_offset_deg = 0\nhfov_deg = 50\n"
             "vfov_deg = 32.5\nwidth = 800\nheight = 600\n"
             "[[channels]]\nname = \"left\"\nheading_offset_deg = 35.515\nenabled = false\n";
    }
    auto c = s376::load_config(path);
    CHECK(c.host.bind == "127.0.0.1:2300");
    CHECK(c.host.word_order == s376::xt4::WordOrder::Be);
    CHECK(c.host.float_fmt == s376::xt4::FloatFmt::Ieee);
    CHECK(!c.host.hat_responder);
    CHECK(c.render.engine == "osg");
    CHECK_NEAR(c.render.frame_rate, 30.0, 1e-9);
    CHECK(d.render.msaa == 4);
    CHECK(d.render.aniso == 16);
    CHECK(c.channels.size() == 2);
    CHECK(c.channels[0].width == 800);
    CHECK(!c.channels[1].enabled);
    std::remove(path);

    std::ifstream ksea_earth("data/earth/ksea.earth");
    CHECK(static_cast<bool>(ksea_earth));
    std::string earth((std::istreambuf_iterator<char>(ksea_earth)), std::istreambuf_iterator<char>());
    CHECK(earth.find("cache/ksea/elev.tif") != std::string::npos);
    CHECK(earth.find("cache/ksea/imagery.tif") != std::string::npos);
    CHECK(earth.find("cache/ksea/osm.geojson") != std::string::npos);
    CHECK(earth.find("feature_geom") != std::string::npos);
    std::ifstream globe("data/earth/s376.earth");
    CHECK(static_cast<bool>(globe));

    std::string desk = "config/desk-4monitor.toml";
    {
        std::ifstream probe(desk);
        if (!probe)
            desk = "../config/desk-4monitor.toml";
    }
    auto desk_cfg = s376::load_config(desk);
    CHECK(desk_cfg.channels.size() == 4);
    CHECK(desk_cfg.render.msaa == 4);
    CHECK(desk_cfg.render.aniso == 16);
    CHECK(desk_cfg.render.earth_file.find("ksea.earth") != std::string::npos);
    bool ios = false, left = false, front = false, right = false;
    for (const auto& ch : desk_cfg.channels) {
        if (ch.name == "ios") {
            ios = ch.window_x == 0 && ch.pitch_offset_deg < 0;
        }
        if (ch.name == "left") {
            left = ch.window_x == 1920;
            CHECK_NEAR(ch.heading_offset_deg, 35.515, 1e-6);
        }
        if (ch.name == "front")
            front = ch.window_x == 3840;
        if (ch.name == "right")
            right = ch.window_x == 5760 && ch.heading_offset_deg < 0;
    }
    CHECK(ios && left && front && right);
    auto placed = s376::layout_channels(s376::select_channels(desk_cfg, "all"), 1, 7680, 1080);
    CHECK(placed.size() == 4);
    for (const auto& p : placed) {
        if (p.name == "ios")
            CHECK(p.x == 0);
        if (p.name == "front")
            CHECK(p.x == 3840);
    }
}
