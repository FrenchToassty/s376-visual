#include "s376/airport.hpp"
#include "s376/config.hpp"
#include "s376/diagnostics.hpp"
#include "s376/host.hpp"
#include "s376/log.hpp"
#include "s376/render.hpp"
#include "s376/safety.hpp"
#include "s376/synth.hpp"
#include "s376/terrain.hpp"
#include "s376/version.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>

namespace {

std::atomic<bool> g_stop{false};

void on_sig(int) {
    g_stop = true;
}

void usage() {
    std::cerr
        << "s376-visual " << s376::version()
        << " — S376 image generator\n"
           "usage: s376-visual [options]\n"
           "  -c, --config PATH     TOML config (default config/s376-visual.toml if present)\n"
           "  --bind HOST:PORT      override host.bind (default 0.0.0.0:2300)\n"
           "  --channel NAME        far_left|mid_left|mid_right|far_right|all\n"
           "                        aliases: left|front|right|front_fo\n"
           "                        (default: all enabled, or $S376_CHANNEL)\n"
           "  --cache-dir PATH      packed DEM/imagery from s376-cache\n"
           "  --headless            run the host receiver with no GL window\n"
           "  --demo                in-process synth to 127.0.0.1 (no VAX needed)\n"
           "  --demo-weather        demo plus 124/125/126/127/128 weather and a storm cell\n"
           "  --duration SEC        exit after SEC seconds (tests)\n"
           "  -v, --verbose         console DEBUG\n"
           "  --debug               packet dumps + DEBUG\n"
           "  --gl-debug            OSG per-attribute GL error checks (off in Release)\n"
           "  --quality NAME        gtx980ti | rtx | custom\n"
           "  --reply-master NAME   only this --channel sends the type-200 HAT reply\n"
           "  --no-hat              never send HAT (overrides reply_master)\n"
           "  --screenshot PATH     write the first channel after a few frames and exit\n"
           "  --at ICAO[:RWY[:nm]]  pose the eye (KJFK:04R:0 lined-up, KJFK:04R:8, or KJFK)\n"
           "  --alt FT              override shot altitude (MSL feet)\n"
           "  --bank DEG            override shot roll (right-wing-down)\n"
           "  --hdg DEG             override shot true heading\n"
           "  --pitch DEG           override shot pitch\n"
           "  --diagnostics         print a dump and exit\n"
           "  --doctor              environment checks and exit\n"
           "  --version             print version\n"
           "  -h, --help\n";
}

s376::HostState host_from_shot(const s376::ShotPose& p) {
    s376::HostState h;
    h.have_eyepoint = true;
    h.eye.csn = s376::xt4::kEyepointCsn;
    h.eye.minor_csn = -1;
    h.eye.lat_deg = p.lat;
    h.eye.lon_deg = p.lon;
    h.eye.alt_ft = p.alt_ft;
    h.eye.heading_deg = static_cast<float>(-p.hdg_true_deg);
    h.eye.pitch_deg = static_cast<float>(p.pitch_deg);
    h.eye.roll_deg = static_cast<float>(p.roll_deg);
    h.have_env = true;
    h.env.tod_select = -1;
    h.env.tod_hours = 14.5f;
    h.env.visible_range_ft = 528000.f;
    h.env.month = 6;
    h.env.day = 21;
    h.env.year = 2024;
    return h;
}

} // namespace

int main(int argc, char** argv) {
    std::string config_path;
    std::string bind_override;
    std::string channel_name;
    std::string cache_override;
    bool headless = false;
    bool demo = false;
    bool demo_weather = false;
    bool verbose = false;
    bool debug = false;
    bool gl_debug = false;
    bool do_diag = false;
    bool do_doctor = false;
    double duration = -1;
    std::string quality_override;
    std::string screenshot_path;
    std::string at_spec;
    s376::ShotOverrides shot_over;
    std::string reply_master_override;
    bool no_hat = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << flag << " needs an argument\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") {
            usage();
            return 0;
        }
        if (a == "--version") {
            std::cout << s376::version() << "\n";
            return 0;
        }
        if (a == "-c" || a == "--config")
            config_path = need(a.c_str());
        else if (a == "--bind")
            bind_override = need(a.c_str());
        else if (a == "--channel")
            channel_name = need(a.c_str());
        else if (a == "--cache-dir")
            cache_override = need(a.c_str());
        else if (a == "--headless")
            headless = true;
        else if (a == "--demo")
            demo = true;
        else if (a == "--demo-weather") {
            demo = true;
            demo_weather = true;
        }
        else if (a == "--duration")
            duration = std::stod(need(a.c_str()));
        else if (a == "-v" || a == "--verbose")
            verbose = true;
        else if (a == "--debug")
            debug = true;
        else if (a == "--gl-debug")
            gl_debug = true;
        else if (a == "--quality")
            quality_override = need(a.c_str());
        else if (a == "--reply-master")
            reply_master_override = need(a.c_str());
        else if (a == "--no-hat")
            no_hat = true;
        else if (a == "--screenshot")
            screenshot_path = need(a.c_str());
        else if (a == "--at")
            at_spec = need(a.c_str());
        else if (a == "--alt") {
            shot_over.have_alt = true;
            shot_over.alt_ft = std::stod(need(a.c_str()));
        } else if (a == "--bank") {
            shot_over.have_bank = true;
            shot_over.bank_deg = std::stod(need(a.c_str()));
        } else if (a == "--hdg") {
            shot_over.have_hdg = true;
            shot_over.hdg_deg = std::stod(need(a.c_str()));
        } else if (a == "--pitch") {
            shot_over.have_pitch = true;
            shot_over.pitch_deg = std::stod(need(a.c_str()));
        } else if (a == "--diagnostics")
            do_diag = true;
        else if (a == "--doctor")
            do_doctor = true;
        else {
            std::cerr << "unknown option " << a << "\n";
            usage();
            return 2;
        }
    }

    s376::AppConfig cfg = s376::default_config();
    if (config_path.empty()) {
        std::ifstream probe("config/s376-visual.toml");
        if (probe)
            config_path = "config/s376-visual.toml";
    }
    if (!config_path.empty()) {
        try {
            cfg = s376::load_config(config_path);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            return 2;
        }
    }
    if (!bind_override.empty())
        cfg.host.bind = bind_override;
    if (!cache_override.empty())
        cfg.render.cache_dir = cache_override;
    if (verbose)
        cfg.log.verbose = true;
    if (debug)
        cfg.log.debug = true;
    if (gl_debug)
        cfg.render.gl_debug = true;
    if (!quality_override.empty()) {
        if (quality_override != "gtx980ti" && quality_override != "rtx" &&
            quality_override != "custom") {
            std::cerr << "--quality must be gtx980ti|rtx|custom\n";
            return 2;
        }
        cfg.render.quality = quality_override;
        if (quality_override != "custom")
            s376::apply_quality_preset(cfg.render, quality_override);
    }
    if (!reply_master_override.empty())
        cfg.host.reply_master = reply_master_override;
    if (no_hat)
        cfg.host.hat_responder = false;
    if (channel_name.empty()) {
        if (const char* env_ch = std::getenv("S376_CHANNEL"))
            channel_name = env_ch;
    }
    if (demo) {
        cfg.host.bind = "127.0.0.1:2300";
        cfg.host.allow_sim_lan = false;
        cfg.host.allow_lan = false;
    }

    s376::log::Options lo;
    lo.verbose = cfg.log.verbose;
    lo.debug = cfg.log.debug;
    lo.log_dir = cfg.log.dir;
    s376::log::setup(lo, "visual");

    if (do_doctor) {
        auto rep = s376::run_doctor(cfg, config_path, true);
        std::cout << s376::format_report(rep);
        s376::log::shutdown();
        return rep.fails ? 1 : 0;
    }
    if (do_diag) {
        std::cout << s376::diagnostics_dump(cfg, nullptr);
        s376::log::shutdown();
        return 0;
    }

    s376::ShotPose shot;
    s376::HostState shot_host;
    const bool have_shot = !at_spec.empty();
    if (have_shot) {
        try {
            shot = s376::parse_shot_at(at_spec);
            s376::apply_shot_overrides(shot, shot_over);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            s376::log::shutdown();
            return 2;
        }
        shot_host = host_from_shot(shot);
        S376_LOG_INFO("ig",
                      "shot %s:%s at %.4f,%.4f  %.0fft  hdg %.1f  pitch %.1f  bank %.1f  %.2fnm",
                      shot.icao.c_str(), shot.rwy.empty() ? "-" : shot.rwy.c_str(), shot.lat,
                      shot.lon, shot.alt_ft, shot.hdg_true_deg, shot.pitch_deg, shot.roll_deg,
                      shot.dist_nm);
    }
    if (!screenshot_path.empty() && !have_shot) {
        std::cerr << "--screenshot needs --at ICAO[:RWY[:dist_nm]]\n";
        s376::log::shutdown();
        return 2;
    }
    if (!screenshot_path.empty() && channel_name.empty())
        channel_name = s376::default_shot_channel(cfg);
    if (!screenshot_path.empty()) {
        const auto parent = std::filesystem::path(screenshot_path).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);
    }

    const auto channels = s376::select_channels(cfg, channel_name);
    if (channels.empty()) {
        S376_LOG_ERROR("ig", "no channel matched '%s'",
                       channel_name.empty() ? "all" : channel_name.c_str());
        return 2;
    }
    const bool hat_on = s376::hat_reply_enabled(cfg.host, channels);
    cfg.host.hat_responder = hat_on;
    if (channels.size() > 1) {
        S376_LOG_INFO("ig",
                      "opening %zu channels on one process (laptop/desk). "
                      "Production is one projector PC: --channel far_left|mid_left|mid_right|far_right",
                      channels.size());
    }
    S376_LOG_INFO("ig", "HAT reply %s  master='%s'  this=%s", hat_on ? "on" : "off",
                  cfg.host.reply_master.c_str(),
                  channels.size() == 1 ? channels.front().name.c_str() : "all");

    std::signal(SIGINT, on_sig);
    std::signal(SIGTERM, on_sig);

    s376::SharedState state;
    s376::HostReceiver rx(state, cfg.host);
    auto dem = s376::load_or_synth_dem(cfg.render.cache_dir);
    rx.set_heights(std::make_shared<s376::DemHeightSource>(dem));
    S376_LOG_INFO("ig", "HAT DEM %dx%d  %.3f,%.3f .. %.3f,%.3f", dem->w, dem->h, dem->west,
                  dem->south, dem->east, dem->north);
    std::thread synth_th;
    const bool live_host = screenshot_path.empty();
    if (live_host) {
        try {
            rx.start();
        } catch (const std::exception& ex) {
            S376_LOG_ERROR("ig", "host socket: %s", ex.what());
            s376::log::shutdown();
            return 1;
        }
    }

    s376::SynthConfig demo_cfg;
    demo_cfg.weather = demo_weather;
    const double demo_duration = duration < 0 ? 3600.0 : duration;
    if (demo && live_host) {
        S376_LOG_INFO("ig", "demo: in-process synth -> 127.0.0.1:2300 for %.1fs", demo_duration);
        synth_th = std::thread([demo_cfg, demo_duration] {
            try {
                s376::run_synth_sender("127.0.0.1:2300", demo_cfg, demo_duration, false, false,
                                       &g_stop);
            } catch (const std::exception& ex) {
                S376_LOG_ERROR("synth", "%s", ex.what());
            }
        });
    }

    std::unique_ptr<s376::Renderer> rend;
    int rc = 0;
    if (headless)
        rend = std::make_unique<s376::NullRenderer>();
    else
        rend = s376::make_renderer(cfg.render.engine);
    if (!rend->begin(cfg, channels)) {
        S376_LOG_ERROR("ig", "renderer %s failed to start", rend->name().c_str());
        g_stop = true;
        rc = 1;
    } else {
        S376_LOG_INFO("ig", "renderer=%s views=%zu quality=%s", rend->name().c_str(),
                      rend->view_count(), cfg.render.quality.c_str());
    }

    const auto t0 = std::chrono::steady_clock::now();
    const double period = 1.0 / std::max(cfg.render.frame_rate, 1.0);
    std::uint64_t frames = 0;
    std::uint64_t last_eyepoints = 0;
    while (!g_stop && !rend->done()) {
        if (g_stop) {
            rend->request_stop();
            break;
        }
        if (duration >= 0) {
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (elapsed >= duration) {
                rend->request_stop();
                break;
            }
        }
        s376::HostState host = state.load();
        if (have_shot)
            host = shot_host;
        else if (cfg.render.sync_to_host && host.have_eyepoint &&
                 host.eyepoints == last_eyepoints) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        last_eyepoints = host.eyepoints;
        rend->frame(host);
        ++frames;
        if (headless)
            std::this_thread::sleep_for(std::chrono::duration<double>(period));
        if (!screenshot_path.empty() && frames >= 6) {
            if (!rend->capture(screenshot_path)) {
                S376_LOG_ERROR("ig", "screenshot failed: %s", screenshot_path.c_str());
                rc = 1;
                g_stop = true;
                rend->request_stop();
                break;
            }
            S376_LOG_INFO("ig", "wrote %s", screenshot_path.c_str());
            rend->request_stop();
            break;
        }
        if (frames % 300 == 0 && host.have_eyepoint) {
            S376_LOG_INFO("ig", "%s", s376::diagnostics_dump(cfg, &host).c_str());
        }
    }

    if (g_stop)
        rend->request_stop();
    rend->end();
    g_stop = true;
    rx.stop();
    if (synth_th.joinable())
        synth_th.join();
    {
        const auto host = state.load();
        S376_LOG_INFO("ig", "stopped after %llu frames\n%s",
                      static_cast<unsigned long long>(frames),
                      s376::diagnostics_dump(cfg, &host).c_str());
    }
    s376::log::shutdown();
    return rc;
}
