#pragma once
#include <cstdint>
#include <string>

namespace thunder {

struct GameDate {
    int year = 1;
    unsigned month = 1;
    unsigned day = 1;
    unsigned hour = 0;
};


class GameClock {
public:
    explicit GameClock(GameDate start = {});

    void advance_tick(); // One Thunder base tick = 6 in-game hours.

    [[nodiscard]] const GameDate& date() const noexcept { return date_; }
    [[nodiscard]] std::uint64_t tick_index() const noexcept { return tick_index_; }
    [[nodiscard]] std::uint64_t day_index() const noexcept { return day_index_; }
    void restore_state(GameDate date, std::uint64_t tick_index, std::uint64_t day_index) noexcept;
    [[nodiscard]] static bool validate_state(GameDate date, std::uint64_t tick_index,
                                             std::uint64_t day_index) noexcept;
    [[nodiscard]] bool validate_state() const noexcept {
        return validate_state(date_, tick_index_, day_index_);
    }
    [[nodiscard]] std::uint64_t checksum() const noexcept;
    [[nodiscard]] bool is_daily_boundary() const noexcept;
    [[nodiscard]] bool is_weekly_boundary() const noexcept;
    [[nodiscard]] bool is_monthly_boundary() const noexcept;
    [[nodiscard]] bool is_yearly_boundary() const noexcept;
    [[nodiscard]] std::string to_string() const;

private:
    static bool is_leap(int year) noexcept;
    static unsigned days_in_month(int year, unsigned month) noexcept;

    GameDate date_;
    std::uint64_t tick_index_ = 0;
    std::uint64_t day_index_ = 0;
    bool daily_boundary_ = false;
    bool weekly_boundary_ = false;
    bool monthly_boundary_ = false;
    bool yearly_boundary_ = false;
};

} // namespace thunder
