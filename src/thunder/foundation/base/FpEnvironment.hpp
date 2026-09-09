#pragma once
// Floating-point environment probe for the determinism contract
// (docs/DETERMINISM.md, "Floating point").
//
// Thunder hashes authoritative doubles by bit pattern (Fnv1a64 normalizes
// -0.0/NaN payload only), so any build that evaluates plain source
// expressions differently — FMA contraction, flush-to-zero/denormals-are-
// zero, a non-nearest rounding mode — silently diverges the world checksum
// instead of failing loudly. These probes fingerprint the FP environment so
// the drift is detectable at handshake/save time rather than after a desync.
//
// Deliberately header-only, like DeterministicRng: the fingerprint of the
// *simulation* translation units is the one that matters, and inlining keeps
// the probe expressions subject to the caller TU's own FP flags.

#include "thunder/foundation/base/Hash.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace thunder {

struct FpEnvironmentReport {
    // True when the compiler contracted a plain a*b+c into a fused op whose
    // result differs from strict double rounding. Contracted and strict
    // builds produce different checksums on identical inputs.
    bool contraction_active = false;
    // True when denormal inputs read as zero (DAZ) or denormal results flush
    // to zero (FTZ) — typical fallout of fast-math flags on x86.
    bool denormals_flushed = false;
    // False when round-to-nearest-even is not in effect (or binary64
    // semantics are otherwise broken, e.g. x87 excess precision).
    bool round_to_nearest = true;
    // True when the forced-strict reference differs from std::fma for the
    // probe constants. A sanity property of the platform's binary64, not of
    // the build: if this is false the contraction probe cannot discriminate.
    bool probe_discriminates = false;
    // FNV-1a over the raw probe result bits. Stable for a given build;
    // compare across machines and builds before trusting shared checksums.
    std::uint64_t fingerprint = 0;
};

[[nodiscard]] inline FpEnvironmentReport probe_fp_environment() noexcept {
    // Probe constants where strict IEEE-754 binary64 evaluation of a*b+c
    // (two roundings) differs from std::fma (one rounding):
    //   a*b = (1+eps)^2 = 1 + 2^-51 + 2^-104  --rounds-to-->  1 + 2^-51
    //   strict: (a*b) + c == 0 exactly, with c = -(1 + 2^-51)
    //   fused:  fma(a, b, c) == 2^-104
    constexpr double eps = std::numeric_limits<double>::epsilon();
    // Volatile inputs defeat compile-time folding; volatile intermediates
    // force separate roundings for the strict reference. The plain
    // expression stays contractible — that is the canary.
    volatile double a = 1.0 + eps;
    volatile double b = 1.0 + eps;
    volatile double c = -(1.0 + 2.0 * eps);
    volatile double strict_product = a * b;
    volatile double strict_sum = strict_product + c;
    const double strict = strict_sum;
    const double plain = a * b + c;
    const double fused = std::fma(a, b, c);

    // Denormal input (DAZ): denorm_min + 0.0 reads as +0.0 when flushed.
    volatile double denorm = (std::numeric_limits<double>::denorm_min)();
    volatile double denorm_in = denorm + 0.0;
    // Denormal output (FTZ): DBL_MIN * 0.25 is denormal; flushed to +0.0.
    volatile double min_normal = (std::numeric_limits<double>::min)();
    volatile double denorm_out = min_normal * 0.25;

    // Round-to-nearest-even: 1 + 2^-53 is an exact tie between 1.0 and
    // 1 + 2^-52 and must round to the even significand (1.0).
    volatile double one = 1.0;
    volatile double half_eps = eps * 0.5;
    volatile double rounded = one + half_eps;

    FpEnvironmentReport report;
    report.probe_discriminates =
        std::bit_cast<std::uint64_t>(strict) != std::bit_cast<std::uint64_t>(fused);
    report.contraction_active = report.probe_discriminates &&
        std::bit_cast<std::uint64_t>(plain) == std::bit_cast<std::uint64_t>(fused);
    report.denormals_flushed = (denorm_in == 0.0) || (denorm_out == 0.0);
    report.round_to_nearest = (rounded == 1.0);

    Fnv1a64 hash;
    hash.add(std::bit_cast<std::uint64_t>(strict));
    hash.add(std::bit_cast<std::uint64_t>(plain));
    hash.add(std::bit_cast<std::uint64_t>(fused));
    hash.add(std::bit_cast<std::uint64_t>(static_cast<double>(denorm_in)));
    hash.add(std::bit_cast<std::uint64_t>(static_cast<double>(denorm_out)));
    hash.add(std::bit_cast<std::uint64_t>(static_cast<double>(rounded)));
    report.fingerprint = hash.value();
    return report;
}

// Process-wide cached fingerprint. Function-local statics in inline
// functions are shared across translation units, so every caller observes
// the same value; initialization is thread-safe per the C++11 magic-statics
// rule, which keeps this callable from JobSystem workers.
[[nodiscard]] inline std::uint64_t fp_environment_fingerprint() noexcept {
    static const std::uint64_t cached = probe_fp_environment().fingerprint;
    return cached;
}

} // namespace thunder
