#include "s376/state.hpp"

#include <chrono>

namespace s376 {

void SharedState::apply(const xt4::Datagram& dg, xt4::Decoder& dec, const std::string& peer,
                        std::uint64_t now_ns, HostState& out_snapshot) {
    std::lock_guard<std::mutex> g(mu_);
    s_.datagrams += 1;
    s_.last_rx_ns = now_ns;
    s_.last_peer = peer;
    s_.word_order = dg.word_order;
    s_.float_fmt = dg.float_fmt;

    xt4::Datagram mut = dg;
    if (auto eye = dec.eyepoint(mut)) {
        s_.eye = *eye;
        s_.have_eyepoint = true;
        s_.eyepoints += 1;
        s_.float_fmt = mut.float_fmt;
    }
    const xt4::FloatFmt fmt = s_.float_fmt;
    for (const auto& p : dg.packets) {
        switch (p.type) {
        case xt4::kTypeEnv:
            s_.env = xt4::decode_env(p, fmt);
            s_.have_env = true;
            break;
        case xt4::kTypeFog:
            s_.fog = xt4::decode_fog(p, fmt);
            s_.have_fog = true;
            break;
        case xt4::kTypeAcLights:
            s_.lights = xt4::decode_lights(p);
            break;
        case xt4::kTypeDisplay:
            s_.display = xt4::decode_display(p);
            break;
        case xt4::kTypeLightSwitch: {
            auto sw = xt4::decode_light_switch(p);
            if (sw.switch_num >= 0 && sw.switch_num < xt4::kMaxLightSwitches)
                s_.light_steps[static_cast<std::size_t>(sw.switch_num)] = sw.step;
            break;
        }
        default:
            break;
        }
    }
    out_snapshot = s_;
}

} // namespace s376
