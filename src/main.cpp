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
#include <csignal>
#include <cstring>
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
           "  --channel NAME        front|left|right|ios|all  (default: all enabled)\n"
           "  --cache-dir PATH      packed DEM/imagery from s376-cache\n"
           "  --headless            run the host receiver with no GL window\n"
           "  --demo                in-process synth to 127.0.0.1 (no VAX needed)\n"
           "  --demo-weather        demo plus 124/125/126/127/128 weather and a storm cell\n"
           "  --duration SEC        exit after SEC seconds (tests)\n"
           "  -v, --verbose         console DEBUG\n"
           "  --debug               packet dumps + DEBUG\n"
           "  --diagnostics         print a dump and exit\n"
           "  --doctor              environment checks and exit\n"
           "  --version             print version\n"
           "  -h, --help\n";
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
    bool do_diag = false;
    bool do_doctor = false;
    double duration = -1;

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
        else if (a == "--diagnostics")
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

    const auto channels = s376::select_channels(cfg, channel_name);
    if (channels.empty()) {
        S376_LOG_ERROR("ig", "no channel matched '%s'",
                       channel_name.empty() ? "all" : channel_name.c_str());
        return 2;
    }

    std::signal(SIGINT, on_sig);
    std::signal(SIGTERM, on_sig);

    s376::SharedState state;
    s376::HostReceiver rx(state, cfg.host);
    auto dem = s376::load_or_synth_dem(cfg.render.cache_dir);
    rx.set_heights(std::make_shared<s376::DemHeightSource>(dem));
    S376_LOG_INFO("ig", "HAT DEM %dx%d  %.3f,%.3f .. %.3f,%.3f", dem->w, dem->h, dem->west,
                  dem->south, dem->east, dem->north);
    std::thread synth_th;
    try {
        rx.start();
    } catch (const std::exception& ex) {
        S376_LOG_ERROR("ig", "host socket: %s", ex.what());
        s376::log::shutdown();
        return 1;
    }

    s376::SynthConfig demo_cfg;
    demo_cfg.weather = demo_weather;
    const double demo_duration = duration < 0 ? 3600.0 : duration;
    if (demo) {
        S376_LOG_INFO("ig", "demo: in-process synth -> 127.0.0.1:2300 for %.1fs", demo_duration);
        synth_th = std::thread([demo_cfg, demo_duration] {
            try {
                s376::run_synth_sender("127.0.0.1:2300", demo_cfg, demo_duration, false, false);
            } catch (const std::exception& ex) {
                S376_LOG_ERROR("synth", "%s", ex.what());
            }
        });
    }

    std::unique_ptr<s376::Renderer> rend;
    if (headless)
        rend = std::make_unique<s376::NullRenderer>();
    else
        rend = s376::make_renderer(cfg.render.engine);
    if (!rend->begin(cfg, channels)) {
        S376_LOG_ERROR("ig", "renderer %s failed to start", rend->name().c_str());
        g_stop = true;
    } else {
        S376_LOG_INFO("ig", "renderer=%s views=%zu", rend->name().c_str(), rend->view_count());
    }

    const auto t0 = std::chrono::steady_clock::now();
    const double period = 1.0 / std::max(cfg.render.frame_rate, 1.0);
    std::uint64_t frames = 0;
    while (!g_stop && !rend->done()) {
        if (duration >= 0) {
            const double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (elapsed >= duration)
                break;
        }
        const auto host = state.load();
        rend->frame(host);
        ++frames;
        if (headless)
            std::this_thread::sleep_for(std::chrono::duration<double>(period));
        if (frames % 300 == 0 && host.have_eyepoint) {
            S376_LOG_INFO("ig", "%s", s376::diagnostics_dump(cfg, &host).c_str());
        }
    }

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
    return 0;
}
