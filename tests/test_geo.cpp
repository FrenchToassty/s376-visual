#include "s376/geo.hpp"

#include <cmath>
#include <iostream>

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

void test_geo() {
    auto p = s376::geo::lla_to_ecef(0, 0, 0);
    CHECK_NEAR(p.x, s376::geo::kWgsA, 1.0);
    CHECK_NEAR(p.y, 0, 1.0);
    CHECK_NEAR(p.z, 0, 1.0);

    auto q = s376::geo::lla_to_ecef(90, 0, 0);
    CHECK_NEAR(q.x, 0, 2.0);
    CHECK_NEAR(q.z, s376::geo::kWgsB, 2.0);

    s376::geo::Enu f, r, d;
    s376::geo::body_to_enu(0, 0, 0, f, r, d);
    CHECK_NEAR(f.n, 1, 1e-9);
    CHECK_NEAR(f.e, 0, 1e-9);
    CHECK_NEAR(r.e, 1, 1e-9);

    s376::geo::body_to_enu(90, 0, 0, f, r, d);
    CHECK_NEAR(f.e, 1, 1e-9);
    CHECK_NEAR(f.n, 0, 1e-9);

    CHECK(s376::geo::day_of_year(2026, 1, 1) == 1);
    CHECK(s376::geo::day_of_year(2026, 6, 21) == 172);

    double el = 0;
    auto s = s376::geo::sun_ecef(0, 0, 12.0, 3, 21, &el);
    CHECK(el > 70); // noon at equator around equinox
    const double mag = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
    CHECK_NEAR(mag, 1.0, 1e-9);
}
