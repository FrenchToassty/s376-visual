#include "s376/config.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace s376 {
namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

std::string unquote(const std::string& s) {
    if (s.size() >= 2 &&
        ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')))
        return s.substr(1, s.size() - 2);
    return s;
}

bool parse_bool(const std::string& s, bool& out) {
    if (s == "true" || s == "1") {
        out = true;
        return true;
    }
    if (s == "false" || s == "0") {
        out = false;
        return true;
    }
    return false;
}

struct Toml {
    std::unordered_map<std::string, std::string> kv;
    std::vector<std::unordered_map<std::string, std::string>> tables; // [[channels]]
};

Toml parse_toml(const std::string& text, const std::string& path) {
    Toml t;
    std::string section;
    std::unordered_map<std::string, std::string>* cur = &t.kv;
    std::istringstream in(text);
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        auto hash = line.find('#');
        if (hash != std::string::npos)
            line = line.substr(0, hash);
        line = trim(line);
        if (line.empty())
            continue;
        if (line.size() >= 4 && line.rfind("[[", 0) == 0 && line.size() >= 4 &&
            line.substr(line.size() - 2) == "]]") {
            const std::string name = trim(line.substr(2, line.size() - 4));
            if (name != "channels")
                throw std::runtime_error(path + ":" + std::to_string(lineno) +
                                         ": unknown table array [[" + name + "]]");
            t.tables.emplace_back();
            cur = &t.tables.back();
            section = "channels";
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = trim(line.substr(1, line.size() - 2));
            cur = &t.kv;
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error(path + ":" + std::to_string(lineno) +
                                     ": expected key = value");
        const std::string key = trim(line.substr(0, eq));
        const std::string val = unquote(trim(line.substr(eq + 1)));
        const std::string full =
            section.empty() || section == "channels" ? key : section + "." + key;
        (*cur)[section == "channels" ? key : full] = val;
    }
    return t;
}

} // namespace

xt4::WordOrder parse_word_order(const std::string& s) {
    if (s == "auto" || s.empty())
        return xt4::WordOrder::Auto;
    if (s == "le")
        return xt4::WordOrder::Le;
    if (s == "be")
        return xt4::WordOrder::Be;
    throw std::runtime_error("word_order must be auto|le|be, got " + s);
}

xt4::FloatFmt parse_float_fmt(const std::string& s) {
    if (s == "auto" || s.empty())
        return xt4::FloatFmt::Auto;
    if (s == "vax")
        return xt4::FloatFmt::Vax;
    if (s == "ieee")
        return xt4::FloatFmt::Ieee;
    throw std::runtime_error("float_format must be auto|vax|ieee, got " + s);
}

AppConfig default_config() {
    AppConfig c;
    c.channels = {
        {"front", 0.0, 0.0, 0.0, 50.0, 32.5, 0, 1920, 1080, false, true},
        {"left", 35.515, 0.0, 0.0, 50.0, 32.5, 1, 1920, 1080, false, true},
        {"right", -35.25, 0.0, 0.0, 50.0, 32.5, 2, 1920, 1080, false, true},
    };
    return c;
}

static void fill_host(HostConfig& h, const std::unordered_map<std::string, std::string>& kv) {
    if (kv.count("host.bind"))
        h.bind = kv.at("host.bind");
    if (kv.count("host.reply_port"))
        h.reply_port = static_cast<std::uint16_t>(std::stoi(kv.at("host.reply_port")));
    if (kv.count("host.word_order"))
        h.word_order = parse_word_order(kv.at("host.word_order"));
    if (kv.count("host.float_format"))
        h.float_fmt = parse_float_fmt(kv.at("host.float_format"));
    if (kv.count("host.hat_responder"))
        parse_bool(kv.at("host.hat_responder"), h.hat_responder);
    if (kv.count("host.allow_sim_lan"))
        parse_bool(kv.at("host.allow_sim_lan"), h.allow_sim_lan);
    if (kv.count("host.allow_lan"))
        parse_bool(kv.at("host.allow_lan"), h.allow_lan);
}

static void fill_render(RenderConfig& r, const std::unordered_map<std::string, std::string>& kv) {
    if (kv.count("render.engine"))
        r.engine = kv.at("render.engine");
    if (kv.count("render.earth_file"))
        r.earth_file = kv.at("render.earth_file");
    if (kv.count("render.cache_dir"))
        r.cache_dir = kv.at("render.cache_dir");
    if (kv.count("render.frame_rate"))
        r.frame_rate = std::stod(kv.at("render.frame_rate"));
    if (kv.count("render.window_title"))
        r.window_title = kv.at("render.window_title");
    if (kv.count("render.vsync"))
        parse_bool(kv.at("render.vsync"), r.vsync);
}

static ChannelConfig fill_channel(const std::unordered_map<std::string, std::string>& kv) {
    ChannelConfig c;
    if (kv.count("name"))
        c.name = kv.at("name");
    auto num = [&](const char* k, double& d) {
        if (kv.count(k))
            d = std::stod(kv.at(k));
    };
    auto inum = [&](const char* k, int& d) {
        if (kv.count(k))
            d = std::stoi(kv.at(k));
    };
    num("heading_offset_deg", c.heading_offset_deg);
    num("pitch_offset_deg", c.pitch_offset_deg);
    num("roll_offset_deg", c.roll_offset_deg);
    num("hfov_deg", c.hfov_deg);
    num("vfov_deg", c.vfov_deg);
    inum("monitor", c.monitor);
    inum("width", c.width);
    inum("height", c.height);
    if (kv.count("fullscreen"))
        parse_bool(kv.at("fullscreen"), c.fullscreen);
    if (kv.count("enabled"))
        parse_bool(kv.at("enabled"), c.enabled);
    return c;
}

AppConfig load_config(const std::string& path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("config file not found: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    const auto t = parse_toml(ss.str(), path);
    AppConfig cfg = default_config();
    fill_host(cfg.host, t.kv);
    fill_render(cfg.render, t.kv);
    if (t.kv.count("log.verbose"))
        parse_bool(t.kv.at("log.verbose"), cfg.log.verbose);
    if (t.kv.count("log.debug"))
        parse_bool(t.kv.at("log.debug"), cfg.log.debug);
    if (t.kv.count("log.dir"))
        cfg.log.dir = t.kv.at("log.dir");
    if (!t.tables.empty()) {
        cfg.channels.clear();
        for (const auto& tab : t.tables)
            cfg.channels.push_back(fill_channel(tab));
    }
    return cfg;
}

std::string dump_config(const AppConfig& cfg) {
    std::ostringstream o;
    o << "host.bind=" << cfg.host.bind << "\n";
    o << "host.reply_port=" << cfg.host.reply_port << "\n";
    o << "host.word_order=" << xt4::word_order_name(cfg.host.word_order) << "\n";
    o << "host.float_format=" << xt4::float_fmt_name(cfg.host.float_fmt) << "\n";
    o << "host.hat_responder=" << (cfg.host.hat_responder ? "true" : "false") << "\n";
    o << "host.allow_sim_lan=" << (cfg.host.allow_sim_lan ? "true" : "false") << "\n";
    o << "render.engine=" << cfg.render.engine << "\n";
    o << "render.earth_file=" << cfg.render.earth_file << "\n";
    o << "render.frame_rate=" << cfg.render.frame_rate << "\n";
    for (const auto& ch : cfg.channels) {
        o << "channel " << ch.name << " hdg=" << ch.heading_offset_deg << " fov=" << ch.hfov_deg
          << "x" << ch.vfov_deg << " " << ch.width << "x" << ch.height
          << (ch.enabled ? "" : " disabled") << "\n";
    }
    return o.str();
}

} // namespace s376
