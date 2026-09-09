#include "thunder/simulation/kernel/GameClock.hpp"
#include "thunder/foundation/base/Hash.hpp"
#include <bit>
#include <cstddef>
#include <span>
#include <string_view>
#include <iomanip>
#include <sstream>

namespace thunder {

GameClock::GameClock(GameDate start) : date_(start) {}

bool GameClock::is_leap(int year) noexcept {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

unsigned GameClock::days_in_month(int year, unsigned month) noexcept {
    static constexpr unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && is_leap(year)) return 29;
    return days[month - 1];
}

void GameClock::advance_tick() {
    daily_boundary_ = weekly_boundary_ = monthly_boundary_ = yearly_boundary_ = false;
    ++tick_index_;
    date_.hour += 6;
    if (date_.hour < 24) return;

    date_.hour = 0;
    ++day_index_;
    ++date_.day;
    daily_boundary_ = true;
    weekly_boundary_ = (day_index_ % 7u) == 0u;

    if (date_.day <= days_in_month(date_.year, date_.month)) return;
    date_.day = 1;
    ++date_.month;
    monthly_boundary_ = true;

    if (date_.month <= 12) return;
    date_.month = 1;
    ++date_.year;
    yearly_boundary_ = true;
}

void GameClock::restore_state(GameDate date, std::uint64_t tick_index, std::uint64_t day_index) noexcept {
    date_ = date; tick_index_ = tick_index; day_index_ = day_index;
    daily_boundary_ = weekly_boundary_ = monthly_boundary_ = yearly_boundary_ = false;
}

bool GameClock::validate_state(GameDate date, std::uint64_t tick_index,
                               std::uint64_t day_index) noexcept {
    if (date.month < 1 || date.month > 12 || date.day < 1 ||
        date.day > days_in_month(date.year, date.month) || date.hour >= 24 ||
        date.hour % 6u != 0u)
        return false;

    // Recover the initial six-hour slot from the persisted state. This keeps
    // arbitrary historical start dates/times valid while proving that the
    // scheduling day counter agrees with tick progression and current hour.
    const auto tick_slot = static_cast<unsigned>(tick_index % 4u);
    const auto current_slot = date.hour / 6u;
    const auto initial_slot = (current_slot + 4u - tick_slot) % 4u;
    const auto crossed_day = initial_slot + tick_slot >= 4u ? 1u : 0u;
    const auto expected_day_index = tick_index / 4u + crossed_day;
    return day_index == expected_day_index;
}

std::uint64_t GameClock::checksum() const noexcept {
    const auto hash_byte = [](Fnv1a64& hash, std::uint8_t value) {
        const auto byte = static_cast<std::byte>(value);
        hash.add_bytes(std::span<const std::byte>{&byte, 1});
    };
    const auto hash_u32 = [&hash_byte](Fnv1a64& hash, std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    };
    const auto hash_u64 = [&hash_byte](Fnv1a64& hash, std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8)
            hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    };

    Fnv1a64 hash;
    hash.add(std::string_view{"Thunder.GameClock.v1"});
    hash_u32(hash, std::bit_cast<std::uint32_t>(date_.year));
    hash_u32(hash, date_.month);
    hash_u32(hash, date_.day);
    hash_u32(hash, date_.hour);
    hash_u64(hash, tick_index_);
    hash_u64(hash, day_index_);
    return hash.value();
}

bool GameClock::is_daily_boundary() const noexcept { return daily_boundary_; }
bool GameClock::is_weekly_boundary() const noexcept { return weekly_boundary_; }
bool GameClock::is_monthly_boundary() const noexcept { return monthly_boundary_; }
bool GameClock::is_yearly_boundary() const noexcept { return yearly_boundary_; }

std::string GameClock::to_string() const {
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(4) << date_.year << '-'
       << std::setw(2) << date_.month << '-' << std::setw(2) << date_.day << ' '
       << std::setw(2) << date_.hour << ":00";
    return ss.str();
}

} // namespace thunder
