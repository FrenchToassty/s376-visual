#include "s376/diagnostics.hpp"
#include "s376/host.hpp"
#include "s376/safety.hpp"
#include "s376/version.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef S376_HAS_OSG
#include <osg/Version>
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

} // namespace

DoctorReport run_doctor(const AppConfig& cfg, const std::string& config_path, bool listen) {
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
              << "\n";
        }
        o << "peer=" << host->last_peer << "\n";
    }
    return o.str();
}

} // namespace s376
