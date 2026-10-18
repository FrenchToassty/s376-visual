#include "s376/airport.hpp"
#include "s376/config.hpp"
#include "s376/control.hpp"
#include "s376/diagnostics.hpp"
#include "s376/host.hpp"
#include "s376/landmarks.hpp"
#include "s376/missiles.hpp"
#include "s376/image.hpp"
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
#include <string>
#include <thread>
#include <vector>

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
           "  --screenshot PATH     write the first channel (PPM+PNG) after warmup and exit\n"
           "  --screenshot-wait SEC settle time before capture (default 3; pager idle or this)\n"
           "  --self-test-shot DIR  render a known solid/gradient, check pixels, write files\n"
           "  --approach ICAO:RWY   scripted straight glidepath to touchdown (KJFK:04R)\n"
           "  --start-agl FT        approach start AGL (default 2000)\n"
           "  --gs DEG              approach glidepath degrees (default 3.0)\n"
           "  --kts KT              approach groundspeed (default 140)\n"
           "  --fps N               record frame rate (default 30)\n"
           "  --record PATH         offscreen FBO flight to PATH (.mp4 via ffmpeg, or PNG dir)\n"
           "  --at ICAO[:RWY[:nm]]  pose the eye (KJFK:04R:0 lined-up, KJFK:04R:8, or KJFK)\n"
           "                        does not wait for host packets\n"
           "  --alt FT              override shot altitude (MSL feet)\n"
           "  --bank DEG            override shot roll (right-wing-down)\n"
           "  --hdg DEG             override shot true heading\n"
           "  --pitch DEG           override shot pitch\n"
           "  --era current|classic session-only NYC skyline easter egg (not saved)\n"
           "  --control HOST:PORT   local operator control (default 127.0.0.1:2376; not xT4)\n"
           "  --no-control          do not listen for s376-wizard\n"
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
    h.eyepoints = 1;
    h.datagrams = 0;
    h.last_peer = "shot";
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
    std::string self_test_shot;
    double screenshot_wait = 3.0;
    std::string at_spec;
    std::string approach_spec;
    std::string record_path;
    double start_agl_ft = 2000.0;
    double gs_deg = 3.0;
    double kts = 140.0;
    double record_fps = 30.0;
    s376::ShotOverrides shot_over;
    std::string reply_master_override;
    bool no_hat = false;
    std::string era_override;
    std::string control_override;
    bool no_control = false;

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
        else if (a == "--era")
            era_override = need(a.c_str());
        else if (a == "--control")
            control_override = need(a.c_str());
        else if (a == "--no-control")
            no_control = true;
        else if (a == "--screenshot")
            screenshot_path = need(a.c_str());
        else if (a == "--screenshot-wait")
            screenshot_wait = std::stod(need(a.c_str()));
        else if (a == "--self-test-shot")
            self_test_shot = need(a.c_str());
        else if (a == "--at")
            at_spec = need(a.c_str());
        else if (a == "--approach")
            approach_spec = need(a.c_str());
        else if (a == "--start-agl")
            start_agl_ft = std::stod(need(a.c_str()));
        else if (a == "--gs")
            gs_deg = std::stod(need(a.c_str()));
        else if (a == "--kts")
            kts = std::stod(need(a.c_str()));
        else if (a == "--fps")
            record_fps = std::stod(need(a.c_str()));
        else if (a == "--record")
            record_path = need(a.c_str());
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
    if (!era_override.empty()) {
        try {
            const auto e = s376::parse_skyline_era(era_override);
            s376::set_session_era(e, e == s376::SkylineEra::Classic);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            return 2;
        }
    }
    if (channel_name.empty()) {
        if (const char* env_ch = std::getenv("S376_CHANNEL"))
            channel_name = env_ch;
    }
    if (!control_override.empty())
        cfg.control.bind = control_override;
    if (no_control)
        cfg.control.enabled = false;
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
        const auto chans = s376::select_channels(cfg, channel_name);
        std::vector<std::string> names;
        for (const auto& ch : chans)
            names.push_back(ch.name);
        std::cout << s376::diagnostics_dump(cfg, nullptr, names);
        s376::log::shutdown();
        return 0;
    }

    if (!approach_spec.empty() && !at_spec.empty()) {
        std::cerr << "use --approach or --at, not both\n";
        s376::log::shutdown();
        return 2;
    }
    if (!record_path.empty() || !screenshot_path.empty())
        cfg.render.msaa = 0;
    if (!record_path.empty() && approach_spec.empty()) {
        std::cerr << "--record needs --approach ICAO:RWY\n";
        s376::log::shutdown();
        return 2;
    }
    if (kts < 30.0 || kts > 400.0) {
        std::cerr << "--kts must be 30..400\n";
        s376::log::shutdown();
        return 2;
    }
    if (record_fps < 1.0 || record_fps > 120.0) {
        std::cerr << "--fps must be 1..120\n";
        s376::log::shutdown();
        return 2;
    }

    s376::ShotPose shot;
    s376::HostState shot_host;
    s376::ApproachPlan approach;
    bool have_shot = !at_spec.empty();
    if (!approach_spec.empty()) {
        try {
            if (!s376::plan_approach(approach_spec, gs_deg, start_agl_ft, approach)) {
                std::cerr << "bad --approach " << approach_spec << " (need ICAO:RWY, --gs 0.25..10, "
                             "--start-agl 20..20000)\n";
                s376::log::shutdown();
                return 2;
            }
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            s376::log::shutdown();
            return 2;
        }
        approach.kts = kts;
        approach.fps = record_fps;
        shot = s376::approach_sample(approach, 0);
        s376::apply_shot_overrides(shot, shot_over);
        have_shot = true;
    } else if (have_shot) {
        try {
            shot = s376::parse_shot_at(at_spec);
            s376::apply_shot_overrides(shot, shot_over);
        } catch (const std::exception& ex) {
            std::cerr << ex.what() << "\n";
            s376::log::shutdown();
            return 2;
        }
    }
    if (have_shot) {
        shot_host = host_from_shot(shot);
        if (!screenshot_path.empty())
            shot_host.script_dt = 1.f / 60.f;
        S376_LOG_INFO("ig",
                      "shot %s:%s at %.4f,%.4f  %.0fft  hdg %.1f  pitch %.1f  bank %.1f  %.2fnm",
                      shot.icao.c_str(), shot.rwy.empty() ? "-" : shot.rwy.c_str(), shot.lat,
                      shot.lon, shot.alt_ft, shot.hdg_true_deg, shot.pitch_deg, shot.roll_deg,
                      shot.dist_nm);
        if (!approach_spec.empty()) {
            S376_LOG_INFO("ig",
                          "approach %s:%s  gs %.2f  start-agl %.0f  %.0f kts  %.0f fps  "
                          "%.2f nm  %.1fs  %d frames",
                          approach.icao.c_str(), approach.rwy.c_str(), approach.gs_deg,
                          approach.start_agl_ft, approach.kts, approach.fps,
                          approach.start_dist_m / 1852.0, s376::approach_time_s(approach),
                          s376::approach_frame_count(approach));
        }
        if (cfg.render.cache_dir.empty()) {
            const auto found = s376::resolve_cache_dir({}, shot.icao);
            if (!found.empty()) {
                cfg.render.cache_dir = found;
                S376_LOG_INFO("ig", "auto cache_dir %s (from --at %s)", found.c_str(),
                              shot.icao.c_str());
            } else {
                S376_LOG_WARN("ig",
                              "no cache/%s — renderer will use a synth KSEA DEM. "
                              "Run `s376-cache fetch %s` or pass --cache-dir",
                              shot.icao.c_str(), shot.icao.c_str());
            }
        }
    }
    if (!screenshot_path.empty() && !have_shot && self_test_shot.empty()) {
        std::cerr << "--screenshot needs --at ICAO[:RWY[:dist_nm]] or --approach ICAO:RWY\n";
        s376::log::shutdown();
        return 2;
    }
    if ((!screenshot_path.empty() || !record_path.empty()) && channel_name.empty())
        channel_name = s376::default_shot_channel(cfg);
    if (!screenshot_path.empty()) {
        const auto parent = std::filesystem::path(screenshot_path).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);
    }
    if (!self_test_shot.empty())
        std::filesystem::create_directories(self_test_shot);

    const auto channels = s376::select_channels(cfg, channel_name);
    if (channels.empty()) {
        S376_LOG_ERROR("ig", "no channel matched '%s'",
                       channel_name.empty() ? "all" : channel_name.c_str());
        return 2;
    }
    const bool hat_on = s376::hat_reply_enabled(cfg.host, channels);
    cfg.host.hat_responder = hat_on;
    std::vector<std::string> selected_names;
    for (const auto& ch : channels) {
        selected_names.push_back(ch.name);
        S376_LOG_INFO("ig", "selected %s  hdg=%.3f  %dx%d  fov=%.1fx%.1f", ch.name.c_str(),
                      ch.heading_offset_deg, ch.width, ch.height, ch.hfov_deg, ch.vfov_deg);
    }
    if (channels.size() > 1) {
        S376_LOG_INFO("ig",
                      "opening %zu channels on one process (laptop/desk). "
                      "Production is one projector PC: --channel far_left|mid_left|mid_right|far_right",
                      channels.size());
    }
    S376_LOG_INFO("ig", "HAT reply %s  master='%s'  this=%s", hat_on ? "on" : "off",
                  cfg.host.reply_master.c_str(),
                  channels.size() == 1 ? channels.front().name.c_str() : "all");

    s376::control_hub().attach(cfg, channels);
    s376::ControlServer control;
    if (cfg.control.enabled && !cfg.control.bind.empty()) {
        try {
            control.start(cfg.control.bind, cfg.host.allow_sim_lan, cfg.host.allow_lan);
        } catch (const std::exception& ex) {
            S376_LOG_WARN("wizard", "control not listening: %s", ex.what());
        }
    }

    if (!self_test_shot.empty()) {
        std::string err;
        if (!s376::self_test_shot_files(self_test_shot, err)) {
            S376_LOG_ERROR("ig", "self-test-shot failed: %s", err.c_str());
            s376::log::shutdown();
            return 1;
        }
        S376_LOG_INFO("ig", "self-test-shot CPU ok  %s", self_test_shot.c_str());
        if (headless) {
            const s376::HostState* hp = have_shot ? &shot_host : nullptr;
            S376_LOG_INFO("ig", "%s", s376::diagnostics_dump(cfg, hp, selected_names).c_str());
            s376::log::shutdown();
            return 0;
        }
    }

    std::signal(SIGINT, on_sig);
    std::signal(SIGTERM, on_sig);

    s376::SharedState state;
    s376::HostReceiver rx(state, cfg.host);
    auto dem = s376::load_or_synth_dem(cfg.render.cache_dir);
    auto heights = std::make_shared<s376::DemHeightSource>(dem);
    rx.set_heights(heights);
    s376::PttWatch ptt;
    S376_LOG_INFO("ig", "HAT DEM %dx%d  %.3f,%.3f .. %.3f,%.3f", dem->w, dem->h, dem->west,
                  dem->south, dem->east, dem->north);
    std::thread synth_th;
    // --at / --screenshot / --self-test-shot pose the eye themselves.
    // Only --demo still opens a loopback socket (synth).
    const bool live_host =
        demo || (!have_shot && screenshot_path.empty() && self_test_shot.empty() &&
                 record_path.empty());
    if (have_shot && !demo)
        S376_LOG_INFO("ig", "--at: no host socket (datagrams not required)");
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
    if (headless && record_path.empty() && screenshot_path.empty())
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
    auto t_fx = t0;
    const double period = 1.0 / std::max(cfg.render.frame_rate, 1.0);
    std::uint64_t frames = 0;
    std::uint64_t last_eyepoints = 0;
    if (rc == 0 && !record_path.empty()) {
        const int n = s376::approach_frame_count(approach);
        const float dt = static_cast<float>(1.0 / std::max(1.0, approach.fps));
        const int w = std::max(8, channels.front().width);
        const int h = std::max(8, channels.front().height);
        s376::RecordWriter writer;
        if (!writer.open(record_path, w, h, approach.fps)) {
            S376_LOG_ERROR("ig", "record open failed: %s", record_path.c_str());
            rc = 1;
        } else {
            S376_LOG_INFO("ig", "record %s  %dx%d  %d frames  sink=%s  rss=%.1f MiB",
                          record_path.c_str(), w, h, n,
                          writer.used_ffmpeg() ? "ffmpeg" : "png",
                          static_cast<double>(s376::current_rss_bytes()) / (1024.0 * 1024.0));
            s376::RgbImage im;
            shot_host = host_from_shot(s376::approach_sample(approach, 0));
            shot_host.script_dt = dt;
            const auto t_warm = std::chrono::steady_clock::now();
            for (int warm = 0; !g_stop && !rend->done(); ++warm) {
                rend->frame(shot_host);
                ++frames;
                const double waited =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t_warm)
                        .count();
                if (warm >= 7 && (rend->pager_idle() || waited >= screenshot_wait))
                    break;
            }
            for (int i = 0; i < n && !g_stop && !rend->done(); ++i) {
                const double t = static_cast<double>(i) / std::max(1.0, approach.fps);
                const auto pose = s376::approach_sample(approach, t);
                shot_host = host_from_shot(pose);
                shot_host.script_dt = dt;
                rend->frame(shot_host);
                ++frames;
                if (!rend->capture_rgb(im) || !writer.write(im)) {
                    S376_LOG_ERROR("ig", "record frame %d failed", i);
                    rc = 1;
                    break;
                }
                if (i == 0 || i + 1 == n || i % 60 == 0) {
                    S376_LOG_INFO("ig",
                                  "record %d/%d  %.0fft  %.2fnm  rss=%.1f MiB", i + 1, n,
                                  pose.alt_ft, pose.dist_nm,
                                  static_cast<double>(s376::current_rss_bytes()) /
                                      (1024.0 * 1024.0));
                }
            }
            if (!writer.close() && writer.used_ffmpeg()) {
                S376_LOG_ERROR("ig", "ffmpeg mux failed for %s", record_path.c_str());
                rc = 1;
            }
            if (!writer.mux_cmd().empty()) {
                S376_LOG_INFO("ig", "wrote %d PNG frames in %s", writer.frames(),
                              writer.frames_dir().c_str());
                S376_LOG_INFO("ig", "mux: %s", writer.mux_cmd().c_str());
                std::cout << writer.mux_cmd() << "\n";
            } else if (writer.used_ffmpeg()) {
                S376_LOG_INFO("ig", "wrote %s  %d frames", record_path.c_str(), writer.frames());
            }
        }
        rend->request_stop();
    }
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
        if (host.have_eyepoint && ptt.edge(host, cfg.egg, rend->ptt_key_down())) {
            (void)s376::try_fire_missile(host.eye.lat_deg, host.eye.lon_deg, host.eye.alt_ft,
                                         host.eye.true_heading_deg(), host.eye.pitch_deg);
        }
        {
            const auto now = std::chrono::steady_clock::now();
            const double dt = std::chrono::duration<double>(now - t_fx).count();
            t_fx = now;
            s376::step_missiles(heights.get(), dt);
        }
        if (!have_shot && cfg.render.sync_to_host && host.have_eyepoint &&
            host.eyepoints == last_eyepoints) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        last_eyepoints = host.eyepoints;
        {
            const double t = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now().time_since_epoch())
                                 .count();
            s376::control_hub().note_host(host, t);
            s376::control_hub().note_frame(t);
        }
        if (s376::control_hub().restart_requested()) {
            S376_LOG_INFO("wizard", "refresh channels — exiting so the operator can reopen");
            rend->request_stop();
            g_stop = true;
            break;
        }
        rend->frame(host);
        ++frames;
        if (headless)
            std::this_thread::sleep_for(std::chrono::duration<double>(period));
        if (!self_test_shot.empty() && frames >= 2) {
            std::string err;
            if (!rend->capture_self_test(self_test_shot, err)) {
                S376_LOG_ERROR("ig", "self-test-shot failed: %s", err.c_str());
                rc = 1;
            } else {
                S376_LOG_INFO("ig", "self-test-shot ok  %s", self_test_shot.c_str());
            }
            rend->request_stop();
            break;
        }
        if (!screenshot_path.empty() && frames >= 8) {
            const double waited =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (!rend->pager_idle() && waited < screenshot_wait)
                continue;
            if (!rend->capture(screenshot_path)) {
                S376_LOG_ERROR("ig", "screenshot failed: %s", screenshot_path.c_str());
                rc = 1;
                g_stop = true;
                rend->request_stop();
                break;
            }
            S376_LOG_INFO("ig", "wrote %s (FBO %dx%d, waited %.2fs pager=%s)",
                          screenshot_path.c_str(), channels.front().width, channels.front().height,
                          waited, rend->pager_idle() ? "idle" : "timeout");
            rend->request_stop();
            break;
        }
        if (frames % 300 == 0 && host.have_eyepoint) {
            S376_LOG_INFO("ig", "%s", s376::diagnostics_dump(cfg, &host, selected_names).c_str());
        }
    }

    if (!self_test_shot.empty() && frames == 0)
        S376_LOG_WARN("ig", "self-test-shot: no GL frames (CPU already passed; hangar GPU for FBO)");
    if (g_stop)
        rend->request_stop();
    rend->end();
    g_stop = true;
    rx.stop();
    if (synth_th.joinable())
        synth_th.join();
    {
        const s376::HostState host = have_shot ? shot_host : state.load();
        S376_LOG_INFO("ig", "stopped after %llu frames\n%s",
                      static_cast<unsigned long long>(frames),
                      s376::diagnostics_dump(cfg, &host, selected_names).c_str());
    }
    s376::log::shutdown();
    return rc;
}
