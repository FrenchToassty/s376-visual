#include "s376/host.hpp"
#include "s376/safety.hpp"
#include "s376/state.hpp"
#include "s376/synth.hpp"
#include "s376/xt4.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

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

void test_synth_loop() {
    using namespace s376;
    SynthConfig sc;
    sc.word_order = xt4::WordOrder::Be;
    sc.float_fmt = xt4::FloatFmt::Ieee;
    SyntheticFlight f(sc);
    xt4::Decoder dec;
    int ok = 0;
    for (int i = 0; i < 200; ++i) {
        auto pose = f.step(0.05);
        auto dg = f.datagram(pose);
        auto parsed = dec.parse(dg.data(), dg.size());
        auto eye = dec.eyepoint(*parsed);
        CHECK(eye.has_value());
        CHECK_NEAR(eye->lat_deg, pose.lat_deg, 1e-7);
        CHECK_NEAR(eye->lon_deg, pose.lon_deg, 1e-7);
        ++ok;
    }
    CHECK(dec.stats().eyepoints == 200);
    CHECK(dec.stats().implausible == 0);
    CHECK(ok == 200);

    // Live UDP: bind loopback, send one synth datagram, decode.
    HostConfig hc;
    hc.bind = "127.0.0.1:18765";
    hc.hat_responder = true;
    hc.allow_sim_lan = false;
    SharedState st;
    HostReceiver rx(st, hc);
    rx.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sc.rate_hz = 20;
    run_synth_sender("127.0.0.1:18765", sc, 0.25, false, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    rx.stop();
    auto snap = st.load();
    CHECK(snap.datagrams >= 1);
    CHECK(snap.have_eyepoint);
    CHECK(snap.eye.lat_deg > 47.0 && snap.eye.lat_deg < 48.0);
}
