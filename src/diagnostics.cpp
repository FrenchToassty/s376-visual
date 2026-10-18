#include "s376/airport.hpp"
#include "s376/diagnostics.hpp"
#include "s376/fetch.hpp"
#include "s376/geo.hpp"
#include "s376/host.hpp"
#include "s376/render.hpp"
#include "s376/safety.hpp"
#include "s376/version.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef S376_HAS_OSG
#include <osg/Version>
#ifndef _WIN32
#include <X11/Xlib.h>
#include <fcntl.h>
#endif
#endif
#ifdef S376_HAS_OSGEARTH
#include <osgEarth/Version>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace s376 {
namespace {

const char* st_name(CheckStatus s) {
    switch (s) {
    case CheckStatus::Pass:
        return "PASS";
    case CheckStatus::Warn:
        return "WARN";
    case CheckStatus::Fail:
        return "FAIL";
    case CheckStatus::Skip:
        return "SKIP";
    }
    return "?";
}

void add(DoctorReport& r, Check c) {
    if (c.status == CheckStatus::Fail)
        ++r.fails;
    if (c.status == CheckStatus::Warn)
        ++r.warns;
    r.checks.push_back(std::move(c));
}

std::string lower_icao(std::string id) {
    for (char& c : id)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return id;
}

bool looks_packed(const std::string& dir) {
    if (std::filesystem::exists(dir + "/elev.s376dem") ||
        std::filesystem::exists(dir + "/lod0/elev.s376dem") ||
        std::filesystem::exists(dir + "/imagery.dds"))
        return true;
    return count_cache_tiles(dir) > 0;
}

} // namespace

std::string format_byte_count(std::uint64_t n) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(1);
    if (n < 1024)
        return std::to_string(n) + " B";
    if (n < 1024ull * 1024)
        o << (n / 1024.0) << " KiB";
    else if (n < 1024ull * 1024 * 1024)
        o << (n / (1024.0 * 1024.0)) << " MiB";
    else
        o << (n / (1024.0 * 1024.0 * 1024.0)) << " GiB";
    return o.str();
}

std::vector<AirportCacheSize> scan_airport_caches(const std::string& root,
                                                  const std::string& extra_dir) {
    std::vector<AirportCacheSize> out;
    auto push_unique = [&](const std::string& icao, const std::string& path) {
        for (const auto& e : out) {
            if (e.path == path || e.icao == icao)
                return;
        }
        AirportCacheSize s;
        s.icao = icao;
        s.path = path;
        s.bytes = directory_bytes(path);
        s.tiles = count_cache_tiles(path);
        out.push_back(std::move(s));
    };

    for (const auto& a : all_public_airports()) {
        const std::string low = lower_icao(a.icao);
        std::string path;
        const std::string candidates[] = {
            root + "/" + a.icao, root + "/" + low, extra_dir,
        };
        for (const auto& c : candidates) {
            if (!c.empty() && std::filesystem::is_directory(c) &&
                (looks_packed(c) || extra_dir != c)) {
                if (c == extra_dir && !looks_packed(c) &&
                    lower_icao(std::filesystem::path(c).filename().string()) != low &&
                    std::filesystem::path(c).filename().string() != a.icao)
                    continue;
                path = c;
                break;
            }
        }
        if (path.empty())
            path = root + "/" + low;
        push_unique(a.icao, path);
    }

    if (std::filesystem::is_directory(root)) {
        for (const auto& e : std::filesystem::directory_iterator(root)) {
            if (!e.is_directory())
                continue;
            const std::string p = e.path().string();
            if (!looks_packed(p))
                continue;
            std::string name = e.path().filename().string();
            for (char& c : name)
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            push_unique(name, p);
        }
    }
    if (!extra_dir.empty() && looks_packed(extra_dir)) {
        std::string name = std::filesystem::path(extra_dir).filename().string();
        for (char& c : name)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (name.empty())
            name = "CACHE";
        push_unique(name, extra_dir);
    }
    return out;
}

std::vector<ChannelFrameTime> bench_channel_times(const AppConfig& cfg, bool try_gpu) {
    auto channels = select_channels(cfg, "all");
    if (channels.empty())
        channels = default_config().channels;

    HostState host;
    host.have_eyepoint = true;
    host.have_env = true;
    host.eye.lat_deg = 47.463684;
    host.eye.lon_deg = -122.307751;
    host.eye.alt_ft = 447;
    host.eye.heading_deg = -180.34f;
    host.eye.pitch_deg = 0;
    host.env.tod_hours = 14.5f;
    host.env.visible_range_ft = 60760.f;
    host.light_steps.fill(5);

    std::vector<ChannelFrameTime> out;
    constexpr int kIters = 8000;
    volatile double sink = 0;
    for (const auto& ch : channels) {
        ChannelFrameTime t;
        t.name = ch.name;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kIters; ++i) {
            auto v = geo::make_channel_view(
                host.eye.lat_deg, host.eye.lon_deg, host.eye.alt_ft * geo::kFtToM,
                host.eye.true_heading_deg(), host.eye.pitch_deg, host.eye.roll_deg,
                ch.heading_offset_deg, ch.pitch_offset_deg, ch.roll_offset_deg, ch.hfov_deg,
                ch.vfov_deg, ch.width, ch.height);
            sink += v.eye.x + v.forward.y + v.up.z;
        }
        t.cpu_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() /
            kIters;
        out.push_back(std::move(t));
    }
    (void)sink;

#ifdef S376_HAS_OSG
    bool have_display = false;
#ifndef _WIN32
    if (const char* dpy = std::getenv("DISPLAY")) {
        if (dpy[0]) {
            // XOpenDisplay prints to stderr when DISPLAY is stale; keep doctor quiet.
            const int saved = dup(STDERR_FILENO);
            const int n = open("/dev/null", O_WRONLY);
            if (n >= 0) {
                dup2(n, STDERR_FILENO);
                close(n);
            }
            if (Display* x = XOpenDisplay(dpy)) {
                XCloseDisplay(x);
                have_display = true;
            }
            if (saved >= 0) {
                dup2(saved, STDERR_FILENO);
                close(saved);
            }
        }
    }
#else
    have_display = try_gpu;
#endif
    if (try_gpu && have_display) {
        try {
            AppConfig c = cfg;
            c.render.engine = "osg";
            c.render.vsync = false;
            auto chans = channels;
            for (auto& ch : chans) {
                ch.width = 160;
                ch.height = 90;
                ch.fullscreen = false;
                ch.window_x = 0;
                ch.window_y = 0;
            }
            auto rend = make_osg_renderer();
            if (rend && rend->begin(c, chans)) {
                rend->frame(host);
                const int kFrames = 8;
                const auto g0 = std::chrono::steady_clock::now();
                for (int i = 0; i < kFrames; ++i)
                    rend->frame(host);
                const double composite_ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g0)
                        .count() /
                    kFrames;
                rend->end();
                const double share = composite_ms / std::max<std::size_t>(1, out.size());
                for (auto& t : out)
                    t.gpu_ms = share;
            }
        } catch (...) {
            // GPU bench is best-effort; CPU numbers still stand.
        }
    }
#else
    (void)try_gpu;
#endif
    return out;
}

DoctorReport run_doctor(const AppConfig& cfg, const std::string& config_path, bool listen,
                        bool bench) {
    DoctorReport r;
    add(r, {"version", CheckStatus::Pass, std::string("s376-visual ") + version(), ""});
#ifdef _WIN32
    add(r, {"platform", CheckStatus::Pass, "Windows", ""});
#else
    add(r, {"platform", CheckStatus::Pass, "Linux", ""});
#endif

#ifdef S376_HAS_OSGEARTH
    add(r, {"osgearth", CheckStatus::Pass, "osgEarth present at build time", ""});
#elif defined(S376_HAS_OSG)
    add(r,
        {"osgearth", CheckStatus::Warn,
         "OSG " + std::string(osgGetVersion()) + " present; osgEarth not found (procedural globe + local DEM)",
         "build osgEarth from source on Mint 22 (see README) and re-cmake"});
#else
    add(r, {"osgearth", CheckStatus::Warn, "no OSG; renderer is null/headless",
            "apt install libopenscenegraph-dev"});
#endif

#ifdef S376_HAS_GDAL
    add(r, {"gdal", CheckStatus::Pass, "GDAL present — GeoTIFF DEM/imagery path enabled", ""});
#else
    add(r, {"gdal", CheckStatus::Warn, "GDAL not built in; portable .s376dem only",
            "apt install libgdal-dev and re-cmake"});
#endif

    if (!config_path.empty()) {
        std::ifstream in(config_path);
        add(r, {"config", in ? CheckStatus::Pass : CheckStatus::Fail, config_path,
                in ? "" : "copy config/s376-visual.toml"});
    } else {
        add(r, {"config", CheckStatus::Pass, "built-in defaults (3 RSI channels)", ""});
    }

    safety::Endpoint ep;
    std::string err;
    if (!safety::parse_host_port(cfg.host.bind, ep, err)) {
        add(r, {"bind-parse", CheckStatus::Fail, err, "host.bind = \"0.0.0.0:2300\""});
    } else {
        try {
            safety::check_bind(ep.host, ep.port, "doctor bind", cfg.host.allow_sim_lan,
                               cfg.host.allow_lan);
            add(r, {"bind-safety", CheckStatus::Pass, ep.host + ":" + std::to_string(ep.port), ""});
        } catch (const safety::UnsafeEndpoint& ex) {
            add(r, {"bind-safety", CheckStatus::Fail, ex.what(),
                    "use 0.0.0.0 or 127.0.0.1; never bind a sim-LAN address in CI"});
        }
    }

#ifndef _WIN32
    ifaddrs* ifa = nullptr;
    bool on_sim = false;
    std::string sim_addr;
    if (getifaddrs(&ifa) == 0) {
        for (ifaddrs* p = ifa; p; p = p->ifa_next) {
            if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET)
                continue;
            char ip[64] = {};
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(p->ifa_addr)->sin_addr, ip,
                      sizeof(ip));
            if (safety::is_forbidden(ip)) {
                on_sim = true;
                sim_addr = ip;
            }
        }
        freeifaddrs(ifa);
    }
    if (on_sim) {
        add(r, {"local-addrs", CheckStatus::Warn,
                "this machine has address " + sim_addr + " on a sim LAN",
                "tests/CI must not send to that net; production IG sets allow_sim_lan"});
    } else {
        add(r, {"local-addrs", CheckStatus::Pass,
                "no 10.1.95.0/24 or 192.168.173.0/24 on this host", ""});
    }
#endif

    if (listen) {
        try {
            HostConfig hc = cfg.host;
            // Doctor always binds loopback so it cannot be a LAN probe.
            hc.bind = "127.0.0.1:0";
            hc.allow_sim_lan = false;
            hc.allow_lan = false;
            HostSocket sock;
            // Port 0 is awkward with our parser; use 2300 if free, else skip.
            hc.bind = "127.0.0.1:2300";
            try {
                sock.open(hc);
                add(r, {"udp-2300", CheckStatus::Pass, "bound 127.0.0.1:2300", ""});
                sock.close();
            } catch (const std::exception& ex) {
                add(r, {"udp-2300", CheckStatus::Warn, ex.what(),
                        "another process owns 2300; stop it or pick another port"});
            }
        } catch (const std::exception& ex) {
            add(r, {"udp-2300", CheckStatus::Fail, ex.what(), ""});
        }
    }

    if (const char* e = std::getenv("S376_ALLOW_SIM_LAN")) {
        if (std::string(e) == "1")
            add(r, {"sim-lan-env", CheckStatus::Warn, "S376_ALLOW_SIM_LAN=1 is set",
                    "unset it in tests and CI"});
    } else {
        add(r, {"sim-lan-env", CheckStatus::Pass, "S376_ALLOW_SIM_LAN unset", ""});
    }

    const auto caches = scan_airport_caches("cache", cfg.render.cache_dir);
    if (caches.empty()) {
        add(r, {"cache-size", CheckStatus::Skip, "no airport cache dirs",
                "s376-cache fetch KSEA --out cache/ksea"});
    } else {
        for (const auto& s : caches) {
            const std::string name = "cache-" + lower_icao(s.icao);
            if (s.bytes == 0) {
                add(r, {name, CheckStatus::Skip, s.path + " not packed",
                        "s376-cache fetch " + s.icao + " --out " + s.path});
            } else {
                add(r, {name, CheckStatus::Pass,
                        format_byte_count(s.bytes) + " on disk (" + s.path + ")  " +
                            std::to_string(s.tiles) + " tiles",
                        ""});
            }
        }
    }

    if (bench) {
        const auto mesh_t0 = std::chrono::steady_clock::now();
        std::size_t verts = 0, lights = 0;
        for (const auto& a : all_public_airports()) {
            verts += build_airport_mesh(a).verts.size();
            lights += a.lights.size();
        }
        const double mesh_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - mesh_t0)
                .count();
        {
            std::ostringstream d;
            d << std::fixed << std::setprecision(2) << mesh_ms << " ms  " << verts << " verts  "
              << lights << " lights";
            add(r, {"mesh-build", CheckStatus::Pass, d.str(), ""});
        }
        const auto times = bench_channel_times(cfg, true);
        for (const auto& t : times) {
            std::ostringstream d;
            d << std::fixed << std::setprecision(2) << (t.cpu_ms * 1000.0) << " us cpu/view";
            if (t.gpu_ms >= 0)
                d << ", " << std::setprecision(3) << t.gpu_ms << " ms/frame gpu (composite share)";
            else
                d << ", gpu skipped (no usable DISPLAY)";
            add(r, {"frame-" + t.name, CheckStatus::Pass, d.str(), ""});
        }
    }

    return r;
}

std::string format_report(const DoctorReport& r) {
    std::ostringstream o;
    o << "s376-visual doctor\n";
    for (const auto& c : r.checks) {
        o << "  [" << st_name(c.status) << "] " << c.name << " — " << c.detail;
        if (!c.fix.empty() && c.status != CheckStatus::Pass)
            o << "\n           fix: " << c.fix;
        o << "\n";
    }
    o << r.fails << " fail, " << r.warns << " warn\n";
    return o.str();
}

std::string diagnostics_dump(const AppConfig& cfg, const HostState* host) {
    std::ostringstream o;
    o << "s376-visual " << version() << "\n";
    o << dump_config(cfg);
#ifdef S376_HAS_OSGEARTH
    o << "renderer=osgearth\n";
#elif defined(S376_HAS_OSG)
    o << "renderer=osg " << osgGetVersion() << "\n";
#else
    o << "renderer=null\n";
#endif
    if (host) {
        o << "datagrams=" << host->datagrams << " eyepoints=" << host->eyepoints << "\n";
        o << "encoding=" << xt4::word_order_name(host->word_order) << "/"
          << xt4::float_fmt_name(host->float_fmt) << "\n";
        if (host->have_eyepoint) {
            o << "eye lat=" << host->eye.lat_deg << " lon=" << host->eye.lon_deg
              << " alt_ft=" << host->eye.alt_ft << " hdg=" << host->eye.true_heading_deg()
              << " pitch=" << host->eye.pitch_deg << " roll=" << host->eye.roll_deg << "\n";
        }
        if (host->have_env) {
            o << "env tod=" << host->env.tod_hours << " vis_ft=" << host->env.visible_range_ft
              << " date=" << host->env.year << "-" << host->env.month << "-" << host->env.day
              << " precip=" << host->env.precip_type << "/" << host->env.precip_intensity
              << " wind=" << host->env.wind_speed_mph << "@" << host->env.wind_dir_deg
              << " lightning=" << host->env.lightning_trigger << "\n";
        }
        if (host->have_fog) {
            o << "fog select=" << host->fog.select << " rvr_ft=" << host->fog.rvr_ft
              << " height_ft=" << host->fog.height_ft << "\n";
        }
        if (host->have_cloud) {
            o << "cloud";
            const char* names[3] = {" lower", " upper", " top"};
            for (int i = 0; i < 3; ++i) {
                const auto& L = host->clouds[static_cast<std::size_t>(i)];
                o << names[i] << "=" << L.select << "/" << L.ceiling_ft << "-" << L.top_ft;
            }
            o << "\n";
        }
        for (int i = 0; i < xt4::kMaxActiveStorms; ++i) {
            const auto& c = host->storms[static_cast<std::size_t>(i)];
            if (!c.cfg.active)
                continue;
            o << "storm" << i << " sel=" << c.cfg.select << " csn=" << c.cfg.csn
              << " precip=" << c.cfg.precip_type << "/" << c.cfg.precip_intensity
              << " lightning=" << c.cfg.lightning;
            if (c.have_pose)
                o << " lat=" << c.pose.lat_deg << " lon=" << c.pose.lon_deg;
            o << "\n";
        }
        o << "peer=" << host->last_peer << "\n";
    }
    return o.str();
}

} // namespace s376
