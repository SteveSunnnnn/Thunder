#pragma once

#include "thunder/foundation/base/Hash.hpp"
#include "thunder/simulation/ai/UtilityAi.hpp"
#include "thunder/simulation/gameplay/NotificationRuntime.hpp"
#include "thunder/simulation/gameplay/OnActionRuntime.hpp"
#include "thunder/simulation/gameplay/ScriptedGameplay.hpp"
#include "thunder/simulation/kernel/World.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace thunder::save_detail {

inline constexpr char magic[8] = {'T','H','U','N','D','R','S','V'};
inline constexpr char compressed_magic[8] = {'T','H','U','N','D','R','Z','S'};
inline constexpr std::uint32_t version = 5;
inline constexpr std::uint32_t runtime_v3_version = 3;
// v4 introduced the tagged extension tail (market monetary, FX, financial, …).
// Guards that gate those sections must accept every version from v4 on, so a
// v5 writer can still decode v4 payloads written before newer sections existed.
inline constexpr std::uint32_t extension_sections_since_version = 4;
inline constexpr std::uint32_t legacy_version = 1;
inline constexpr std::uint32_t max_countries = 65'536;
inline constexpr std::uint32_t max_markets = 65'536;
inline constexpr std::uint32_t max_states = 1'000'000;
inline constexpr std::uint32_t max_provinces = 1'000'000;
inline constexpr std::uint32_t max_buildings = 5'000'000;
inline constexpr std::uint32_t max_pops = 20'000'000;
inline constexpr std::uint32_t max_records = 5'000'000;
inline constexpr std::uint32_t max_notifications = 1'000'000;
inline constexpr std::uint32_t max_on_action_invocations = 1'000'000;
inline constexpr std::uint32_t notification_section_tag = 0x3146544eu;
inline constexpr std::uint32_t gameplay_context_section_tag = 0x31544347u;
inline constexpr std::uint32_t on_action_section_tag = 0x31414e4fu;
inline constexpr std::uint32_t market_monetary_section_tag = 0x314e4f4du;
inline constexpr std::uint32_t fx_section_tag = 0x31305846u;
inline constexpr std::uint32_t financial_section_tag = 0x314e4946u;
inline constexpr std::uint32_t slot_section_tag = 0x31544c53u;
inline constexpr std::uint32_t global_script_section_tag = 0x31424c47u;
inline constexpr std::uint32_t grand_strategy_extension_tag = 0x31585347u;
inline constexpr std::uint32_t max_banks = 65'536u;
inline constexpr std::uint32_t max_bank_loans = 5'000'000u;
inline constexpr std::uint32_t max_context_bindings = 65'536u;
inline constexpr std::uint32_t max_context_collections = 4'096u;
inline constexpr std::uint32_t max_context_values = 1'000'000u;
inline constexpr std::uint32_t construction_section_tag = 0x31305143u;
inline constexpr std::uint32_t resistance_section_tag = 0x31534552u;
inline constexpr std::uint32_t geography_section_tag = 0x324f4547u;
// v5: per-POP fractional population-growth accumulators (milli-persons).
inline constexpr std::uint32_t population_growth_section_tag = 0x31575047u;

class Writer {
public:
    void raw(const void* p, std::size_t n) { const auto* b=static_cast<const std::byte*>(p); out.insert(out.end(), b, b+n); }
    void u8(std::uint8_t v){out.push_back(static_cast<std::byte>(v));}
    void u16(std::uint16_t v){for(unsigned s=0;s<16;s+=8)u8(static_cast<std::uint8_t>(v>>s));}
    void u32(std::uint32_t v){for(unsigned s=0;s<32;s+=8)u8(static_cast<std::uint8_t>(v>>s));}
    void u64(std::uint64_t v){for(unsigned s=0;s<64;s+=8)u8(static_cast<std::uint8_t>(v>>s));}
    void i32(std::int32_t v){u32(std::bit_cast<std::uint32_t>(v));}
    void i64(std::int64_t v){u64(std::bit_cast<std::uint64_t>(v));}
    void f64(double v){u64(std::bit_cast<std::uint64_t>(v));}
    void boolean(bool v){u8(v?1u:0u);}
    void string(std::string_view v){if(v.size()>1'048'576u)throw std::invalid_argument("save string too large");u32(static_cast<std::uint32_t>(v.size()));raw(v.data(),v.size());}
    std::vector<std::byte> out;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> b):bytes(b){}
    void need(std::size_t n){if(n>bytes.size()-pos)throw std::runtime_error("truncated Thunder save");}
    std::uint8_t u8(){need(1);return std::to_integer<std::uint8_t>(bytes[pos++]);}
    std::uint16_t u16(){std::uint16_t v=0;for(unsigned s=0;s<16;s+=8)v=static_cast<std::uint16_t>(v|static_cast<std::uint16_t>(u8())<<s);return v;}
    std::uint32_t u32(){std::uint32_t v=0;for(unsigned s=0;s<32;s+=8)v|=static_cast<std::uint32_t>(u8())<<s;return v;}
    std::uint64_t u64(){std::uint64_t v=0;for(unsigned s=0;s<64;s+=8)v|=static_cast<std::uint64_t>(u8())<<s;return v;}
    std::int32_t i32(){return std::bit_cast<std::int32_t>(u32());}
    std::int64_t i64(){return std::bit_cast<std::int64_t>(u64());}
    double f64(){return std::bit_cast<double>(u64());}
    bool boolean(){const auto v=u8();if(v>1u)throw std::runtime_error("invalid boolean in Thunder save");return v!=0u;}
    std::string string(){const auto n=u32();if(n>1'048'576u)throw std::runtime_error("save string too large");need(n);std::string s(reinterpret_cast<const char*>(bytes.data()+pos),n);pos+=n;return s;}
    std::uint32_t count(std::uint32_t cap){const auto n=u32();if(n>cap)throw std::runtime_error("save entity count exceeds safety cap");return n;}
    [[nodiscard]] std::uint32_t peek_u32(){const auto saved=pos;const auto value=u32();pos=saved;return value;}
    [[nodiscard]] bool done() const noexcept{return pos==bytes.size();}
    std::span<const std::byte> bytes; std::size_t pos=0;
};

template<class Id> void wid(Writer& w, Id id){w.u32(id.value());}
template<class Id> Id rid(Reader& r){return Id{static_cast<typename Id::rep_type>(r.u32())};}

struct DecodedGameplayState {
    std::vector<GameplayInstance> instances;
    std::vector<GameplayLogEntry> log;
    std::uint64_t next_instance_id = 0u;
    bool context_section_present = false;
};

struct DecodedGlobalScriptState { bool present = false; };

struct DecodedAiState {
    std::vector<AiActionState> actions;
    std::vector<AiPlanState> plans;
};

struct DecodedNotificationState {
    std::vector<NotificationInstance> instances;
    std::uint64_t next_instance_id = 1u;
    bool present = false;
};

struct DecodedOnActionState {
    std::vector<ScheduledOnActionInvocation> queue;
    std::uint64_t next_invocation_id = 1u;
    bool present = false;
};

struct DecodedMarketMonetaryState { bool present = false; };
struct DecodedFxState { bool present = false; };
struct DecodedFinancialState { bool present = false; };
struct DecodedConstructionState { bool present = false; };
struct DecodedResistanceState { bool present = false; };
struct DecodedGeographyState { bool present = false; };
struct DecodedSlotState { bool present = false; };
struct DecodedPopulationGrowthState { bool present = false; };

void encode_gameplay_context_section(Writer&, const ScriptedGameplayRuntime&, const World&);
void decode_gameplay_context_section(Reader&, DecodedGameplayState&);
void encode_global_script_section(Writer&, const GlobalScriptStore&, const World&);
void decode_global_script_section(Reader&, World&, DecodedGlobalScriptState&);
void encode_gameplay(Writer&, const ScriptedGameplayRuntime&);
DecodedGameplayState decode_gameplay(Reader&, const ScriptedGameplayRuntime&);
void encode_ai(Writer&, const UtilityAiEngine&);
DecodedAiState decode_ai(Reader&, const UtilityAiEngine&, bool has_plans);
void encode_notification_section(Writer&, const NotificationRuntime&);
DecodedNotificationState decode_notification_section(Reader&, const NotificationRuntime&);
void encode_market_monetary_section(Writer&, const MarketStore&);
DecodedMarketMonetaryState decode_market_monetary_section(Reader&, MarketStore&);
void encode_fx_section(Writer&, const CurrencyStore&, const CountryStore&);
DecodedFxState decode_fx_section(Reader&, CurrencyStore&, CountryStore&);
void encode_financial_section(Writer&, const World&);
DecodedFinancialState decode_financial_section(Reader&, World&);
void encode_construction_section(Writer&, const ConstructionStore&, const World&);
DecodedConstructionState decode_construction_section(Reader&, ConstructionStore&);
void encode_resistance_section(Writer&, const GeographyStore&);
DecodedResistanceState decode_resistance_section(Reader&, GeographyStore&);
void encode_geography_section(Writer&, const GeographyStore&);
DecodedGeographyState decode_geography_section(Reader&, GeographyStore&);
void encode_population_growth_section(Writer&, const PopStore&);
DecodedPopulationGrowthState decode_population_growth_section(Reader&, PopStore&);
void encode_slot_section(Writer&, const World&);
DecodedSlotState decode_slot_section(Reader&, World&);
void encode_on_action_section(Writer&, const OnActionRuntime&);
DecodedOnActionState decode_on_action_section(Reader&, const OnActionRuntime&);

std::uint64_t legacy_ai_checksum_v3(const UtilityAiEngine&, std::span<const AiActionState>) noexcept;
std::uint64_t runtime_checksum(std::uint64_t, std::uint64_t) noexcept;
std::uint64_t runtime_checksum(std::uint64_t, std::uint64_t, std::uint64_t) noexcept;
std::uint64_t runtime_checksum(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) noexcept;
std::uint64_t runtime_checksum(std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) noexcept;

// Historical checksum readers are kept in their own translation unit so the
// current save codec does not mix compatibility policy with wire encoding.
std::uint64_t legacy_grand_checksum_v1(const GrandStrategyStore& gs) noexcept;
std::uint64_t legacy_country_checksum_v1(const CountryStore& countries) noexcept;
std::uint64_t legacy_pop_checksum_v1(const PopStore& pops) noexcept;
std::uint64_t legacy_market_checksum_v1(const MarketStore& markets) noexcept;
std::uint64_t legacy_market_checksum_v4_pre_mon1(const MarketStore& markets) noexcept;
std::uint64_t legacy_country_checksum_pre_fx(const CountryStore& countries) noexcept;
std::uint64_t legacy_geography_checksum_pre_resistance(const GeographyStore& geography) noexcept;
std::uint64_t legacy_construction_checksum_pre_weekly(const ConstructionStore& construction) noexcept;
std::uint64_t legacy_world_checksum_v4_pre_fx(const World& world) noexcept;
std::uint64_t legacy_currency_checksum_pre_integrity(const CurrencyStore& currencies) noexcept;
std::uint64_t legacy_world_checksum_pre_financial(const World& world) noexcept;
std::uint64_t legacy_world_checksum_pre_construction(const World& world) noexcept;
std::uint64_t legacy_world_checksum_pre_resistance(const World& world) noexcept;
std::uint64_t legacy_world_checksum_v3_pre_mon1(const World& world) noexcept;
std::uint64_t legacy_world_checksum_v4_pre_mon1(const World& world) noexcept;
std::uint64_t legacy_world_checksum_v4_pre_growth(const World& world) noexcept;
std::uint64_t legacy_world_checksum_v1(const World& world) noexcept;

} // namespace thunder::save_detail
