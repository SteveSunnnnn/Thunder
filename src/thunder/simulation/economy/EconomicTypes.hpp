#pragma once
#include "thunder/foundation/base/StrongId.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

namespace thunder {

using EconomyAmount = std::int64_t;
using EconomyPrice = std::int64_t;
using PopulationCount = std::uint32_t;
using CurrencyKey = std::uint64_t;

inline constexpr EconomyAmount economy_scale = 1000;
inline constexpr std::int64_t ppm_scale = 1'000'000;

[[nodiscard]] constexpr CurrencyKey economy_stable_key(std::string_view text) noexcept {
    CurrencyKey value = 14695981039346656037ull;
    for (const char byte : text) {
        value ^= static_cast<unsigned char>(byte);
        value *= 1099511628211ull;
    }
    return value;
}

inline constexpr CurrencyKey default_currency_key =
    economy_stable_key("core.currency.default");

// Precious-metal numeraires. These are first-class currencies (CurrencyClass::Metal)
// that act as the FX anchor for metallic-standard national currencies. Class is
// derived from the key (see CurrencyStore::currency_class), so no disk field is
// needed and saves stay round-trip compatible.
inline constexpr CurrencyKey metal_gold_key = economy_stable_key("core.metal.gold");
inline constexpr CurrencyKey metal_silver_key = economy_stable_key("core.metal.silver");

enum class CurrencyClass : std::uint8_t {
    National = 0,  // Sovereign-issued currency (fiat or metallic-standard)
    Metal = 1,     // Precious-metal numeraire (gold, silver) held/converted as a currency
};
inline constexpr std::uint64_t market_settlement_account_namespace =
    0x0100000000000000ull;

[[nodiscard]] constexpr SettlementAccountId market_settlement_account_id(
    MarketId market) noexcept {
    return market.valid()
        ? SettlementAccountId{market_settlement_account_namespace |
                              static_cast<std::uint64_t>(market.value())}
        : SettlementAccountId{};
}

namespace detail {

[[nodiscard]] inline std::uint64_t abs_u64(std::int64_t value) noexcept {
    if (value >= 0) return static_cast<std::uint64_t>(value);
    return static_cast<std::uint64_t>(-(value + 1)) + 1u;
}

[[nodiscard]] inline std::uint64_t mul_div_u64_saturating(
    std::uint64_t value, std::uint64_t multiplier, std::uint64_t divisor,
    std::uint64_t saturation) noexcept {
    if (value == 0u || multiplier == 0u || divisor == 0u) return 0u;

    if (value <= std::numeric_limits<std::uint64_t>::max() / multiplier) {
        return std::min((value * multiplier) / divisor, saturation);
    }

#if defined(__GNUC__) || defined(__clang__)
    __extension__ using u128 = unsigned __int128;
    const auto quotient = (static_cast<u128>(value) * static_cast<u128>(multiplier))
        / static_cast<u128>(divisor);
    return quotient > static_cast<u128>(saturation)
        ? saturation
        : static_cast<std::uint64_t>(quotient);
#elif defined(_MSC_VER) && defined(_M_X64)
    std::uint64_t high = 0u;
    const std::uint64_t low = _umul128(value, multiplier, &high);
    if (high >= divisor) return saturation;
    std::uint64_t remainder = 0u;
    const std::uint64_t quotient = _udiv128(high, low, divisor, &remainder);
    (void)remainder;
    return std::min(quotient, saturation);
#else
    // Cold portable fallback: binary long multiplication/division with
    // saturation. Supported production compilers use one of the paths above.
    std::uint64_t quotient = 0u;
    std::uint64_t remainder = 0u;
    for (int bit = 63; bit >= 0; --bit) {
        const bool incoming = ((value >> static_cast<unsigned>(bit)) & 1u) != 0u;
        if (remainder > (std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(incoming)) / 2u)
            return saturation;
        remainder = remainder * 2u + static_cast<std::uint64_t>(incoming);
        // This fallback intentionally returns saturation if the exact wide
        // product cannot be represented without native wide arithmetic.
        if (remainder > divisor) return saturation;
    }
    (void)quotient;
    return saturation;
#endif
}

} // namespace detail

[[nodiscard]] inline EconomyAmount mul_div_nonnegative(
    EconomyAmount value, EconomyAmount multiplier, EconomyAmount divisor) noexcept {
    if (value <= 0 || multiplier <= 0 || divisor <= 0) return 0;
    constexpr auto saturation = static_cast<std::uint64_t>(std::numeric_limits<EconomyAmount>::max());
    return static_cast<EconomyAmount>(detail::mul_div_u64_saturating(
        static_cast<std::uint64_t>(value),
        static_cast<std::uint64_t>(multiplier),
        static_cast<std::uint64_t>(divisor),
        saturation));
}

[[nodiscard]] inline EconomyAmount saturating_add(EconomyAmount a, EconomyAmount b) noexcept {
    if (b > 0 && a > std::numeric_limits<EconomyAmount>::max() - b)
        return std::numeric_limits<EconomyAmount>::max();
    if (b < 0 && a < std::numeric_limits<EconomyAmount>::min() - b)
        return std::numeric_limits<EconomyAmount>::min();
    return a + b;
}

[[nodiscard]] inline EconomyAmount saturating_sub(EconomyAmount a, EconomyAmount b) noexcept {
    if (b == std::numeric_limits<EconomyAmount>::min())
        return a >= 0 ? std::numeric_limits<EconomyAmount>::max()
                      : saturating_add(a, std::numeric_limits<EconomyAmount>::max()) + 1;
    return saturating_add(a, -b);
}

[[nodiscard]] inline std::int64_t signed_ratio_ppm(
    EconomyAmount numerator, EconomyAmount denominator) noexcept {
    if (denominator <= 0 || numerator == 0) return 0;
    const bool negative = numerator < 0;
    constexpr std::uint64_t cap = static_cast<std::uint64_t>(ppm_scale * 4);
    const auto scaled = detail::mul_div_u64_saturating(
        detail::abs_u64(numerator),
        static_cast<std::uint64_t>(ppm_scale),
        static_cast<std::uint64_t>(denominator),
        cap);
    const auto signed_scaled = static_cast<std::int64_t>(scaled);
    return negative ? -signed_scaled : signed_scaled;
}

} // namespace thunder
