#include "s376/xt4.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

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
                      << #b << "=" << _b << " eps=" << (eps) << "\n";                              \
        }                                                                                          \
    } while (0)

static std::vector<std::uint8_t> unhex(const char* s) {
    std::vector<std::uint8_t> o;
    for (std::size_t i = 0; s[i] && s[i + 1]; i += 2) {
        unsigned v = 0;
        std::sscanf(s + i, "%2x", &v);
        o.push_back(static_cast<std::uint8_t>(v));
    }
    return o;
}

// Golden vectors produced by the s376bridge Python builders (synthetic only).
static const char* kBeIeeeKsea =
    "2d3fab01000000480000006400000004000000000001ffff0001002ac334570a40200000c1"
    "2000004047bb59ff4fd6d8c05e93b2314013ec407bf000000000000000000000000000";
static const char* kLeIeeeKsea =
    "3f2d01ab000048000000640000000400000000000100ffff01002a0034c30a572040000020"
    "c10000474059bb4fffd8d65ec0b2934031ec137b4000f0000000000000000000000000";
static const char* kBeVaxKsea =
    "2d3fab01000000480000006400000004000000000001ffff0001002a570ac4340000412000"
    "00c220b6c0fa7edacf433d9f608a009d91c3f400000000800044df0000000000000000";
static const char* kLeVaxEnv =
    "3f2d01ab00004a0000007c000000040000000000010000000000050007000400ea07000000"
    "00050000000000000092420060ea470000a041000000000000000000000000000000000000";
static const char* kHatReply =
    "2d3fab0100000040000000c800000000000000000001000000000000000000000000000000"
    "00000000000000449a5000000000000000000043d8800000000000";

static void check_ksea(const s376::xt4::Dcs& eye, unsigned count) {
    CHECK(eye.is_eyepoint());
    CHECK(eye.count == count);
    CHECK_NEAR(eye.lat_deg, 47.463684, 1e-7);
    CHECK_NEAR(eye.lon_deg, -122.307751, 1e-7);
    CHECK_NEAR(eye.alt_ft, 447.0, 1e-3);
    CHECK_NEAR(eye.true_heading_deg(), 180.34, 1e-3);
    CHECK_NEAR(eye.pitch_deg, 2.5, 1e-4);
    CHECK_NEAR(eye.roll_deg, -10.0, 1e-4);
}

void test_xt4() {
    using namespace s376::xt4;

    CHECK(build_dcs(0, 0, 0, 0, 0, 0, 1, FloatFmt::Vax).size() * 2 == 0x48);
    CHECK(build_hat_query().size() * 2 == 0x20);
    CHECK(build_lights(false, false, 0, FloatFmt::Vax).size() * 2 == 0x48);
    CHECK(build_env(12.f, 1000.f, 1, 1, 2000, FloatFmt::Vax).size() * 2 == 0x4A);
    CHECK(build_display(true).size() * 2 == 0x18);
    CHECK(build_fog(200.f, 1200.f, FloatFmt::Vax).size() * 2 == 36);
    CHECK(build_light_switch(3, 5).size() * 2 == 24);

    struct Enc {
        WordOrder o;
        FloatFmt f;
    };
    const Enc encs[] = {{WordOrder::Le, FloatFmt::Vax},
                        {WordOrder::Be, FloatFmt::Vax},
                        {WordOrder::Le, FloatFmt::Ieee},
                        {WordOrder::Be, FloatFmt::Ieee}};
    for (auto e : encs) {
        auto pk = build_dcs(47.463684, -122.307751, 447.0, 180.34, 2.5f, -10.f, 42, e.f);
        auto q = build_hat_query();
        auto li = build_lights(true, false, 3, e.f);
        auto dg = build_datagram({pk, q, li}, e.o);
        Decoder dec;
        auto parsed = dec.parse(dg.data(), dg.size());
        CHECK(parsed.has_value());
        CHECK(parsed->word_order == e.o);
        auto eye = dec.eyepoint(*parsed);
        CHECK(eye.has_value());
        CHECK(dec.locked_fmt() == e.f);
        check_ksea(*eye, 42);
        CHECK(parsed->packets.size() == 3);
        CHECK(parsed->packets[0].type == kTypeDcs);
        CHECK(parsed->packets[1].type == kTypeQuery);
        CHECK(parsed->packets[2].type == kTypeAcLights);
    }

    // Golden bytes from the Python builders — proves the C++ codec matches.
    {
        auto b = unhex(kBeIeeeKsea);
        Decoder dec;
        auto dg = dec.parse(b.data(), b.size());
        CHECK(dg.has_value());
        CHECK(dg->word_order == WordOrder::Be);
        auto eye = dec.eyepoint(*dg);
        CHECK(eye.has_value());
        CHECK(dec.locked_fmt() == FloatFmt::Ieee);
        check_ksea(*eye, 42);
    }
    {
        auto b = unhex(kLeIeeeKsea);
        Decoder dec;
        auto dg = dec.parse(b.data(), b.size());
        auto eye = dec.eyepoint(*dg);
        CHECK(eye.has_value());
        CHECK(dec.locked_fmt() == FloatFmt::Ieee);
        check_ksea(*eye, 42);
    }
    {
        auto b = unhex(kBeVaxKsea);
        Decoder dec;
        auto dg = dec.parse(b.data(), b.size());
        auto eye = dec.eyepoint(*dg);
        CHECK(eye.has_value());
        CHECK(dec.locked_fmt() == FloatFmt::Vax);
        check_ksea(*eye, 42);
    }
    {
        auto b = unhex(kLeVaxEnv);
        Decoder dec(WordOrder::Auto, FloatFmt::Vax);
        auto dg = dec.parse(b.data(), b.size());
        CHECK(dg.has_value());
        CHECK(!dg->packets.empty());
        auto env = decode_env(dg->packets[0], FloatFmt::Vax);
        CHECK_NEAR(env.tod_hours, 18.25, 1e-4);
        CHECK_NEAR(env.visible_range_ft, 30000.0, 1e-2);
        CHECK(env.month == 7 && env.day == 4 && env.year == 2026);
    }

    {
        auto tcas = build_dcs(47.5, -122.3, 5000, 10, 0, 0, 3, FloatFmt::Vax, 5, -1, 200);
        auto dg = build_datagram({tcas}, WordOrder::Le);
        Decoder dec;
        auto parsed = dec.parse(dg.data(), dg.size());
        CHECK(!dec.eyepoint(*parsed).has_value());
    }

    {
        auto b = unhex(kHatReply);
        int valid = 0;
        float hat = 0, elev = 0;
        CHECK(parse_hat_as_host(b.data(), b.size(), valid, hat, elev));
        CHECK(valid == 1);
        CHECK_NEAR(hat, 1234.5, 0.05);
        CHECK_NEAR(elev, 433.0, 0.05);
    }
    {
        auto b = build_hat_response(1234.5f, 433.0f);
        int valid = 0;
        float hat = 0, elev = 0;
        CHECK(parse_hat_as_host(b.data(), b.size(), valid, hat, elev));
        CHECK(valid == 1);
        CHECK_NEAR(hat, 1234.5, 0.05);
        CHECK_NEAR(elev, 433.0, 0.05);
    }

    {
        auto sw = build_light_switch(12, 4);
        Packet p;
        p.type = kTypeLightSwitch;
        p.words = sw;
        auto d = decode_light_switch(p);
        CHECK(d.switch_num == 12 && d.step == 4);
    }

    {
        Decoder dec;
        std::uint8_t junk[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        CHECK(!dec.parse(junk, sizeof(junk)).has_value());
        CHECK(dec.stats().bad_magic == 1);
    }
}
