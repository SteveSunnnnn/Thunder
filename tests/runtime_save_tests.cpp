#include "thunder/runtime/engine/ThunderEngine.hpp"
#include "thunder/runtime/save/SaveGame.hpp"
#include "thunder/foundation/base/Hash.hpp"
#include "thunder/content/definition/DefinitionDatabase.hpp"
#include "thunder/scripting/ThunderScriptParser.hpp"
#include <cassert>
#include <array>
#include <iostream>
#include <fstream>
#include <iterator>
#include <vector>
#include <stdexcept>
#include <string_view>

using namespace thunder;

namespace {

std::size_t find_u32_le(const std::vector<std::byte>& bytes, std::uint32_t value);

struct BoundContextEventContent {
    SymbolTable symbols;
    DefinitionDatabase definitions;

    explicit BoundContextEventContent(ThunderEngine& engine)
        : definitions(symbols, engine.scripts()) {
        ThunderScriptParser parser{symbols};
        const auto parsed = parser.parse(R"THUNDER(
            script open_context_chain {
                scope = country
                effect = {
                    set_variable = { name = grant value = 7 }
                    FROM = { save_event_target_as = donor }
                    add_to_collection = { name = participants value = THIS }
                }
            }
            script context_option_allow {
                scope = country
                trigger = {
                    has_variable = grant
                    event_target:donor = { treasury_above = 1 }
                    every_in:participants = { treasury_above = 1 }
                }
            }
            script context_option_effect {
                scope = country
                effect = {
                    add_treasury = var:grant
                    event_target:donor = { add_treasury = -2 }
                    every_in:participants = { set_tax_rate = 0.15 }
                }
            }
            event contextual_event {
                scope = country
                effect = open_context_chain
                option = {
                    key = accept
                    allow = context_option_allow
                    effect = context_option_effect
                }
            }
        )THUNDER", "persistent_event_context.thunder");
        assert(parsed.ok());
        std::vector<ScriptCompileDiagnostic> diagnostics;
        assert(definitions.compile_scripts(parsed, diagnostics));
        assert(definitions.ingest_gameplay(parsed, diagnostics));
        assert(definitions.bind_gameplay(engine.gameplay(), engine.ai(), diagnostics));
        assert(diagnostics.empty());
    }
};

CountryId seed_world(ThunderEngine& engine, std::string_view tag = "AAA") {
    return engine.world().countries.create({std::string(tag), 1'000.0, 500.0, 1'000.0, 0.2});
}

void register_runtime_content(ThunderEngine& engine, bool reverse, bool with_plans = true) {
    GameplayDefinition event;
    event.key = "persist.event";
    event.kind = GameplayItemKind::Event;
    event.scope = ScopeType::Country;
    event.cooldown_ticks = 10;
    event.options.push_back({"accept", std::nullopt, std::nullopt});

    GameplayDefinition journal;
    journal.key = "persist.journal";
    journal.kind = GameplayItemKind::Journal;
    journal.scope = ScopeType::Country;

    GameplayDefinition decision;
    decision.key = "persist.decision";
    decision.kind = GameplayItemKind::Decision;
    decision.scope = ScopeType::Country;
    decision.cooldown_ticks = 20;

    AiActionDefinition low;
    low.key = "ai.low";
    low.scope = ScopeType::Country;
    low.base_utility = 1.0;
    low.cooldown_ticks = 5;

    AiActionDefinition high;
    high.key = "ai.high";
    high.scope = ScopeType::Country;
    high.base_utility = 2.0;
    high.cooldown_ticks = 7;

    AiPlanDefinition low_plan;
    low_plan.key = "plan.low";
    low_plan.scope = ScopeType::Country;
    low_plan.action_keys = {"ai.low"};
    low_plan.base_priority = 1.0;
    low_plan.commitment_ticks = 20;

    AiPlanDefinition high_plan;
    high_plan.key = "plan.high";
    high_plan.scope = ScopeType::Country;
    high_plan.action_keys = {"ai.high"};
    high_plan.base_priority = 2.0;
    high_plan.commitment_ticks = 20;

    if (!reverse) {
        engine.gameplay().add_definition(std::move(event));
        engine.gameplay().add_definition(std::move(journal));
        engine.gameplay().add_definition(std::move(decision));
        engine.ai().add_action(std::move(low));
        engine.ai().add_action(std::move(high));
        if (with_plans) {
            engine.ai().add_plan(std::move(low_plan));
            engine.ai().add_plan(std::move(high_plan));
        }
    } else {
        engine.gameplay().add_definition(std::move(decision));
        engine.gameplay().add_definition(std::move(journal));
        engine.gameplay().add_definition(std::move(event));
        engine.ai().add_action(std::move(high));
        engine.ai().add_action(std::move(low));
        if (with_plans) {
            engine.ai().add_plan(std::move(high_plan));
            engine.ai().add_plan(std::move(low_plan));
        }
    }
}

std::uint32_t gameplay_id(const ThunderEngine& engine, std::string_view key) {
    const auto definitions = engine.gameplay().definitions();
    for (std::uint32_t i = 0; i < definitions.size(); ++i) {
        if (definitions[i].key == key) return i;
    }
    throw std::runtime_error("missing test gameplay definition");
}

void create_runtime_state(ThunderEngine& engine, CountryId country) {
    const auto scope = ScopeRef::country(country);
    const auto event = gameplay_id(engine, "persist.event");
    const auto journal = gameplay_id(engine, "persist.journal");
    const auto decision = gameplay_id(engine, "persist.decision");

    assert(engine.gameplay().fire(event, engine.world(), scope, 0));
    assert(engine.gameplay().choose_event_option(0, 0, engine.world(), 0));
    assert(engine.gameplay().fire(journal, engine.world(), scope, 0));
    assert(engine.gameplay().take_decision(decision, engine.world(), scope, 0));
    assert(engine.ai().execute_planned_best(engine.world(), scope, 0));
}

void test_stable_key_restore_across_registration_order() {
    ThunderEngine source{{0u, 0x11223344u, 0x55667788u}};
    const auto source_country = seed_world(source);
    register_runtime_content(source, false);
    create_runtime_state(source, source_country);
    const auto before = source.engine_checksum();
    const auto save = source.make_save();
    assert(save.metadata.version == 5u);
    assert(save.metadata.runtime_checksum != 0u);

    ThunderEngine restored{{0u, 0x11223344u, 0x55667788u}};
    seed_world(restored, "TEMP");
    register_runtime_content(restored, true);
    restored.restore(save.bytes);

    assert(restored.engine_checksum() == before);
    assert(restored.gameplay().instances().size() == source.gameplay().instances().size());
    assert(restored.gameplay().log().size() == source.gameplay().log().size());
    assert(restored.ai().state().size() == source.ai().state().size());
    assert(restored.ai().plan_state().size() == source.ai().plan_state().size());

    const auto& restored_ai_state = restored.ai().state().front();
    assert(restored.ai().actions()[restored_ai_state.action].key == "ai.high");
    const auto& restored_plan_state = restored.ai().plan_state().front();
    assert(restored.ai().plans()[restored_plan_state.plan].key == "plan.high");
    bool found_selected_event = false;
    for (const auto& instance : restored.gameplay().instances()) {
        const auto& definition = restored.gameplay().definitions()[instance.definition];
        if (definition.key == "persist.event") {
            found_selected_event = true;
            assert(instance.selected_option != GameplayInstance::no_option);
            assert(definition.options[instance.selected_option].key == "accept");
        }
    }
    assert(found_selected_event);
}

void test_event_context_survives_choice_and_save_restore() {
    ThunderEngine source{{0u, 0x99887766u, 0x55443322u}};
    const auto recipient = source.world().countries.create(
        {"REC", 1'000.0, 500.0, 100.0, 0.2});
    const auto donor = source.world().countries.create(
        {"DON", 1'000.0, 500.0, 50.0, 0.2});
    BoundContextEventContent source_content{source};
    assert(source.gameplay().definitions().size() == 1u);
    assert(source.gameplay().fire(0u, source.world(), ScopeRef::country(recipient), 0u,
                                  ScopeRef::country(donor)));
    assert(source.gameplay().instances().size() == 1u);
    const auto instance_id = source.gameplay().instances().front().id;
    const auto& source_instance = source.gameplay().instances().front();
    assert(instance_id.valid() && source_instance.context.has_value());
    assert(source_instance.context->from == ScopeRef::country(donor));
    assert(source_instance.context->event_target(script_stable_key("donor")) ==
           ScopeRef::country(donor));
    assert(source_instance.context->variable(script_stable_key("grant")) ==
           ScriptArgument::numeric(7.0));
    assert(source_instance.context->collection(script_stable_key("participants")).size() == 1u);

    const auto save = source.make_save();
    ThunderEngine restored{{0u, 0x99887766u, 0x55443322u}};
    BoundContextEventContent restored_content{restored};
    restored.restore(save.bytes);
    const auto* restored_instance = restored.gameplay().find_instance(instance_id);
    assert(restored_instance != nullptr && restored_instance->context.has_value());
    assert(restored_instance->context->checksum() == source_instance.context->checksum());
    assert(restored.gameplay().next_instance_id() == source.gameplay().next_instance_id());
    assert(restored.engine_checksum() == source.engine_checksum());

    assert(source.gameplay().choose_event_option(instance_id, 0u, source.world(), 0u));
    assert(restored.gameplay().choose_event_option(instance_id, 0u, restored.world(), 0u));
    assert(source.engine_checksum() == restored.engine_checksum());
    assert(restored.world().countries.treasury(recipient) == 107.0);
    assert(restored.world().countries.treasury(donor) == 48.0);
    assert(restored.world().countries.tax_rate(recipient) == 0.15);
    assert(!restored.gameplay().find_instance(instance_id)->context.has_value());
}

void test_global_script_state_survives_save_restore() {
    constexpr std::uint32_t global_script_tag = 0x31424c47u;
    ThunderEngine source{{0u, 0x10203040u, 0x50607080u}};
    const auto country = seed_world(source, "GLB");
    auto& context = source.world().global_scripts.context();
    context.set_variable(script_stable_key("global_counter"), ScriptArgument::numeric(42.0));
    context.set_parameter(script_stable_key("global_mode"), ScriptArgument::symbol(0xabcdu));
    context.save_event_target(script_stable_key("capital"), ScopeRef::country(country));
    assert(context.add_to_collection(script_stable_key("owned_countries"),
                                     ScriptArgument::scope(ScopeRef::country(country))));
    assert(context.consume_random_draw(0x1234u) == 0u);
    assert(context.consume_random_draw(0x1234u) == 1u);
    assert(source.world().global_scripts.validate(source.world()));

    const auto before = source.engine_checksum();
    const auto save = source.make_save();
    const auto global_offset = find_u32_le(save.bytes, global_script_tag);
    assert(global_offset != save.bytes.size() && global_offset >= 60u);

    ThunderEngine restored{{0u, 0x10203040u, 0x50607080u}};
    seed_world(restored, "TEMP");
    restored.restore(save.bytes);

    assert(restored.engine_checksum() == before);
    auto& restored_context = restored.world().global_scripts.context();
    assert(restored_context.variable(script_stable_key("global_counter")) ==
           ScriptArgument::numeric(42.0));
    assert(restored_context.parameter(script_stable_key("global_mode")) ==
           ScriptArgument::symbol(0xabcdu));
    assert(restored_context.event_target(script_stable_key("capital")) ==
           ScopeRef::country(CountryId{0u}));
    assert(restored_context.collection(script_stable_key("owned_countries")).size() == 1u);
    assert(restored_context.collection(script_stable_key("owned_countries"))[0] ==
           ScriptArgument::scope(ScopeRef::country(CountryId{0u})));
    assert(restored_context.consume_random_draw(0x1234u) == 2u);
}

void test_construction_queue_survives_save_restore_and_rejects_bad_targets() {
    ThunderEngine source{{0u, 0x22334455u, 0x66778899u}};
    const auto country = seed_world(source, "CQ");
    const auto state = source.world().geography.create_state(
        {"cq_state", country, MarketId{}, ProvinceId{}});
    const auto province = source.world().geography.create_province(
        {"cq_province", state, country, MarketId{}, 12.0, 34.0, 100u});
    source.world().geography.set_state_capital(state, province);
    const auto project = source.world().construction.enqueue_monument(
        country, province, "monument.test", 100u, 10'000);
    assert(project.valid());
    assert(source.validate_world());

    const auto before = source.engine_checksum();
    const auto save = source.make_save();
    ThunderEngine restored{{0u, 0x22334455u, 0x66778899u}};
    restored.restore(save.bytes);
    assert(restored.engine_checksum() == before);
    assert(restored.world().construction.size() == 1u);
    assert(restored.world().construction.projects().front().id == project);

    ThunderEngine malformed{{0u, 0x22334455u, 0x66778899u}};
    const auto malformed_country = seed_world(malformed, "BAD");
    malformed.world().construction.enqueue_expansion(malformed_country, BuildingId{0u});
    assert(!malformed.validate_world());
    bool rejected = false;
    try {
        (void)malformed.make_save();
    } catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected);
}

void test_failed_runtime_validation_is_atomic() {
    ThunderEngine malformed{{0u, 0u, 0u}};
    seed_world(malformed);
    register_runtime_content(malformed, false);
    const auto event = gameplay_id(malformed, "persist.event");
    GameplayInstance invalid;
    invalid.definition = event;
    invalid.scope = {ScopeType::Country, 999u};
    invalid.opened_tick = 0;
    invalid.last_action_tick = 0;
    invalid.active = false;
    invalid.completed = true;
    malformed.gameplay().restore_state({invalid}, {});
    const auto malformed_save = malformed.make_save();

    ThunderEngine victim{{0u, 0u, 0u}};
    const auto victim_country = seed_world(victim, "VICTIM");
    register_runtime_content(victim, false);
    create_runtime_state(victim, victim_country);
    const auto checksum_before = victim.engine_checksum();
    const auto instances_before = victim.gameplay().instances().size();
    const auto ai_state_before = victim.ai().state().size();
    const auto tag_before = victim.world().countries.tag(CountryId{0});

    bool rejected = false;
    try {
        victim.restore(malformed_save.bytes);
    } catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected);
    assert(victim.engine_checksum() == checksum_before);
    assert(victim.gameplay().instances().size() == instances_before);
    assert(victim.ai().state().size() == ai_state_before);
    assert(victim.world().countries.tag(CountryId{0}) == tag_before);
}

void test_missing_runtime_definition_is_atomic() {
    ThunderEngine source{{0u, 0u, 0u}};
    const auto country = seed_world(source);
    register_runtime_content(source, false);
    create_runtime_state(source, country);
    const auto save = source.make_save();

    ThunderEngine target{{0u, 0u, 0u}};
    seed_world(target, "TARGET");
    GameplayDefinition unrelated;
    unrelated.key = "different.event";
    unrelated.kind = GameplayItemKind::Event;
    unrelated.scope = ScopeType::Country;
    target.gameplay().add_definition(std::move(unrelated));
    const auto before = target.engine_checksum();

    bool rejected = false;
    try {
        target.restore(save.bytes);
    } catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected);
    assert(target.engine_checksum() == before);
    assert(target.world().countries.tag(CountryId{0}) == "TARGET");
}


void test_legacy_v1_save_migration() {
    const std::string path = std::string(THUNDER_TEST_SOURCE_DIR) + "/tests/fixtures/thunder_1_0_v1_legacy.save";
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    const std::vector<char> raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) bytes[i] = static_cast<std::byte>(static_cast<unsigned char>(raw[i]));

    ThunderEngine migrated{{0u, 0xABCDEFu, 0x123456u}};
    auto& definitions = migrated.definitions();
    const auto grain = definitions.add_good({"grain", 1000});
    const NeedFlow needs[]{{grain, 10}};
    definitions.add_need_profile("basic", needs);
    register_runtime_content(migrated, false);
    migrated.restore(bytes);

    assert(migrated.world().countries.size() == 1u);
    assert(migrated.world().countries.tag(CountryId{0}) == "LEG");
    assert(migrated.world().pops.size() == 1u);
    assert(migrated.world().pops.population(PopId{0}) == 1234u);
    assert(migrated.world().grand_strategy.interest_groups().size() == 1u);
    assert(migrated.world().grand_strategy.investment_pools().size() == 1u);
    assert(migrated.world().grand_strategy.diplomatic_relations().empty());
    assert(migrated.world().grand_strategy.wars().empty());
    assert(migrated.gameplay().instances().empty());
    assert(migrated.gameplay().log().empty());
    assert(migrated.ai().state().empty());
    assert(migrated.ai().plan_state().empty());
}


void write_u32_le(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) bytes[offset++] = static_cast<std::byte>((value >> shift) & 0xffu);
}

void write_u64_le(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) bytes[offset++] = static_cast<std::byte>((value >> shift) & 0xffu);
}

std::size_t find_u32_le(const std::vector<std::byte>& bytes, std::uint32_t value) {
    if (bytes.size() < 4u) return bytes.size();
    for (std::size_t offset = bytes.size() - 4u; offset >= 60u; --offset) {
        bool matches = true;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            const auto expected = static_cast<std::byte>((value >> shift) & 0xffu);
            if (bytes[offset + shift / 8u] != expected) { matches = false; break; }
        }
        if (matches) return offset;
        if (offset == 60u) break;
    }
    return bytes.size();
}

void refresh_save_payload_framing(std::vector<std::byte>& bytes) {
    assert(bytes.size() >= 60u);
    write_u64_le(bytes, 44u, static_cast<std::uint64_t>(bytes.size() - 60u));
    Fnv1a64 payload_hash;
    payload_hash.add_bytes(std::span<const std::byte>{bytes}.subspan(60u));
    write_u64_le(bytes, 52u, payload_hash.value());
}

std::uint64_t pre_mon1_market_checksum(const MarketStore& markets) {
    Fnv1a64 hash;
    hash.add(markets.good_count());
    for (std::size_t index = 0; index < markets.size(); ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        hash.add(markets.owner(market).value());
    }
    for (std::size_t index = 0; index < markets.size(); ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        for (const auto value : markets.price_row(market)) hash.add(value);
    }
    for (std::size_t index = 0; index < markets.size(); ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        for (const auto value : markets.supply_row(market)) hash.add(value);
    }
    for (std::size_t index = 0; index < markets.size(); ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        for (const auto value : markets.demand_row(market)) hash.add(value);
    }
    for (std::size_t index = 0; index < markets.size(); ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        for (const auto value : markets.inventory_row(market)) hash.add(value);
    }
    for (std::size_t index = 0; index < markets.size(); ++index) {
        const MarketId market{static_cast<MarketId::rep_type>(index)};
        for (const auto value : markets.shortage_row(market)) hash.add(value);
    }
    return hash.value();
}

std::uint64_t pre_mon1_world_checksum(const World& world) {
    Fnv1a64 hash;
    hash.add(world.countries.checksum());
    hash.add(pre_mon1_market_checksum(world.markets));
    hash.add(world.buildings.checksum());
    hash.add(world.pops.checksum());
    hash.add(world.geography.checksum());
    hash.add(world.grand_strategy.checksum());
    return hash.value();
}

void add_market_monetary_fixture(ThunderEngine& engine, bool custom_monetary_state) {
    const auto grain = engine.definitions().add_good({"settlement_grain", 1'000});
    (void)grain;
    const auto first = engine.world().countries.create(
        {"M01", 10'000.0, 500.0, 100.0, 0.2});
    const auto second = engine.world().countries.create(
        {"M02", 20'000.0, 750.0, 200.0, 0.2});
    engine.world().markets.resize(2u, engine.definitions());
    engine.world().markets.set_owner(MarketId{0u}, first);
    engine.world().markets.set_owner(MarketId{1u}, second);
    if (custom_monetary_state) {
        engine.world().markets.set_currency_key(
            MarketId{0u}, economy_stable_key("currency.alpha"));
        engine.world().markets.set_currency_key(
            MarketId{1u}, economy_stable_key("currency.beta"));
        engine.world().markets.set_clearing_cash(MarketId{0u}, 987'654'321);
        engine.world().markets.set_clearing_cash(MarketId{1u}, -123'456'789);
    }
}

void add_matching_market_definitions(ThunderEngine& engine) {
    const auto grain = engine.definitions().add_good({"settlement_grain", 1'000});
    (void)grain;
}

void test_market_monetary_roundtrip_and_stable_accounts() {
    constexpr std::uint32_t mon1_tag = 0x314e4f4du;
    ThunderEngine source{{0u, 0x44556677u, 0x8899aabbu}};
    add_market_monetary_fixture(source, true);
    const auto save = source.make_save();
    assert(find_u32_le(save.bytes, mon1_tag) != save.bytes.size());

    const auto first_account = source.world().markets.settlement_account(MarketId{0u});
    const auto second_account = source.world().markets.settlement_account(MarketId{1u});
    assert(first_account == market_settlement_account_id(MarketId{0u}));
    assert(second_account == market_settlement_account_id(MarketId{1u}));
    assert(first_account != second_account);

    ThunderEngine restored{{0u, 0x44556677u, 0x8899aabbu}};
    add_matching_market_definitions(restored);
    restored.restore(save.bytes);
    assert(restored.world().checksum() == source.world().checksum());
    assert(restored.world().markets.settlement_account(MarketId{0u}) == first_account);
    assert(restored.world().markets.settlement_account(MarketId{1u}) == second_account);
    assert(restored.world().markets.currency_key(MarketId{0u}) ==
           economy_stable_key("currency.alpha"));
    assert(restored.world().markets.currency_key(MarketId{1u}) ==
           economy_stable_key("currency.beta"));
    assert(restored.world().markets.clearing_cash(MarketId{0u}) == 987'654'321);
    assert(restored.world().markets.clearing_cash(MarketId{1u}) == -123'456'789);
}

void add_financial_definitions(ThunderEngine& engine) {
    const auto good = engine.definitions().add_good({"bank_test_good", 1'000});
    const std::array<RecipeFlow, 1> output{{{good, 1'000}}};
    (void)engine.definitions().add_building_type("bank_test_building", 100, {}, output);
}

void test_financial_section_roundtrip_and_atomic_validation() {
    constexpr std::uint32_t fin1_tag = 0x314e4946u;
    ThunderEngine source{{0u, 0x10203040u, 0x50607080u}};
    add_financial_definitions(source);
    const auto country = source.world().countries.create({"BNK", 1'000.0, 100.0, 100.0, 0.2});
    source.world().markets.resize(1, source.definitions());
    source.world().markets.set_owner(MarketId{0u}, country);
    const auto building = source.world().buildings.create({MarketId{0u}, BuildingTypeId{0u}, 1u, 1'000, 50'000});
    source.world().trade_policies.resize(1);
    source.world().trade_policies.set(country, {125'000, 25'000, 250'000});
    (void)source.world().trade_policies.reserve_capacity(country, 10'000);
    const auto bank = source.world().banks.create({bank_stable_key("bank.roundtrip"), country,
        default_currency_key, 1'000'000, 400'000, 100'000, 80'000, 10'000, 60'000});
    const auto funded = source.world().banks.fund_building(bank, building, 100'000, 60'000, 104);
    assert(funded == 100'000);
    source.world().buildings.cash_mut()[building.value()] = saturating_add(
        source.world().buildings.cash(building), funded);
    assert(source.world().banks.validate(1, 1));
    const auto checksum = source.world().checksum();
    const auto save = source.make_save();
    const auto fin_offset = find_u32_le(save.bytes, fin1_tag);
    assert(fin_offset != save.bytes.size());

    ThunderEngine restored{{0u, 0x10203040u, 0x50607080u}};
    add_financial_definitions(restored);
    restored.restore(save.bytes);
    assert(restored.world().checksum() == checksum);
    assert(restored.world().banks.size() == 1u);
    assert(restored.world().banks.loan_count() == 1u);
    assert(restored.world().banks.balance_sheet_balanced(BankId{0u}));
    assert(restored.world().trade_policies.get(country).import_tariff_ppm == 125'000);
    assert(restored.world().trade_policies.used_capacity(country) == 10'000);

    // FIN1 layout starts with tag, policy count, fixed policy rows, bank count,
    // then the first stable bank key. A zero key must reject atomically.
    auto corrupt = save.bytes;
    const auto bank_key_offset = fin_offset + 12u + source.world().trade_policies.size() * 24u;
    write_u64_le(corrupt, bank_key_offset, 0u);
    refresh_save_payload_framing(corrupt);
    const auto before = restored.engine_checksum();
    bool rejected = false;
    try { restored.restore(corrupt); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
    assert(restored.engine_checksum() == before);
}

void test_grand_strategy_extension_roundtrip() {
    ThunderEngine source{{0u, 0x13579bdu, 0x2468aceu}};
    const auto good = source.definitions().add_good({"gsx_good", 1'000});
    const std::array<RecipeFlow, 1> output{{{good, 10}}};
    const auto building_type = source.definitions().add_building_type("gsx_building", 100, {}, output);
    const auto first = source.world().countries.create({"GS1", 1'000.0, 500.0, 100.0, 0.2});
    const auto second = source.world().countries.create({"GS2", 1'000.0, 500.0, 100.0, 0.2});
    source.world().markets.resize(2u, source.definitions());
    source.world().markets.set_owner(MarketId{0u}, first);
    source.world().markets.set_owner(MarketId{1u}, second);
    const auto state = source.world().geography.create_state({"gsx_state", second, MarketId{1u}, {}});
    const auto province = source.world().geography.create_province({"gsx_province", state, second, MarketId{1u}, 1.0, 2.0, 100});
    source.world().geography.set_state_capital(state, province);
    const auto building = source.world().buildings.create({MarketId{1u}, building_type, 1u, 1'000, 0, province});

    auto& gs = source.world().grand_strategy;
    const auto design = gs.add_ship_design({first, 11u, 22u, 33u, 44'000});
    const auto sea = gs.add_sea_zone({55u, first, 660'000u});
    const auto navy = gs.add_navy({first, state, 8'000u, 770'000u, design,
                                   NavalMission::BlockadePort, sea});
    const auto treaty = gs.create_treaty_ex(first, second, TreatyKind::Custom, 66u,
                                            7u, 99u, 1'200, 77u);
    (void)gs.add_treaty_article({treaty, TreatyKind::TradeAgreement, 88u, 99u, true});
    (void)gs.add_treaty_participant({treaty, first, 1u});
    (void)gs.add_treaty_participant({treaty, second, 0u});
    const auto play = gs.start_diplomatic_play(first, second, 101u);
    (void)gs.add_diplomatic_sway({play, first, second, SwayOfferType::ConcessionTreaty, 102u, true});
    (void)gs.add_war_goal({play, first, second, WarGoalType::ConquerState, state, true, false});
    (void)gs.add_war_goal({play, first, second, WarGoalType::WarReparations, StateId{}, false, false});
    (void)gs.add_commander({first, state, CommanderTrait::LogisticsMaster, 4u});
    (void)gs.add_naval_battle({sea, navy, navy, -123, false});
    (void)gs.add_parliament({first, 103u, true, 52u, 13u, 120u, 75u, 45u, 104u,
                             800'000u, 200'000u, MigrationPolicy::MigrationControls});
    (void)gs.add_cultural_acceptance({first, CultureId{9u}, CulturalAcceptanceLevel::Accepted});

    const auto checksum = source.world().checksum();
    const auto save = source.make_save();
    constexpr std::uint32_t gsx_tag = 0x31585347u;
    assert(find_u32_le(save.bytes, gsx_tag) != save.bytes.size());

    ThunderEngine restored{{0u, 0x13579bdu, 0x2468aceu}};
    const auto restored_good = restored.definitions().add_good({"gsx_good", 1'000});
    const std::array<RecipeFlow, 1> restored_output{{{restored_good, 10}}};
    (void)restored.definitions().add_building_type("gsx_building", 100, {}, restored_output);
    restored.restore(save.bytes);
    assert(restored.world().checksum() == checksum);
    const auto& restored_gs = restored.world().grand_strategy;
    assert(restored_gs.treatys().size() == 1u);
    assert(restored_gs.treatys()[0].treaty_key_hash == 77u);
    assert(restored_gs.treatys()[0].start_tick == 7u);
    assert(restored_gs.treatys()[0].expiry_tick == 99u);
    assert(restored_gs.navys()[0].mission == NavalMission::BlockadePort);
    assert(restored_gs.navys()[0].assigned_zone == sea);
    assert(restored_gs.treaty_articles().size() == 1u);
    assert(restored_gs.treaty_participants().size() == 2u);
    assert(restored_gs.parliaments()[0].migration_policy == MigrationPolicy::MigrationControls);
    assert(restored_gs.cultural_acceptances()[0].level == CulturalAcceptanceLevel::Accepted);
    assert(restored_gs.diplomatic_sways()[0].accepted);
    assert(restored_gs.war_goals()[0].state_target == state);
    assert(restored_gs.war_goals()[1].goal_type == WarGoalType::WarReparations);
    assert(!restored_gs.war_goals()[1].state_target.valid());
    assert(restored_gs.commanders()[0].trait == CommanderTrait::LogisticsMaster);
    assert(restored_gs.sea_zones()[0].blockade_efficiency_ppm == 660'000u);
    assert(restored_gs.naval_battles()[0].progress_milli == -123);
    (void)building;
}

void test_pre_gsx_save_keeps_idle_navy_compatible() {
    ThunderEngine source{{0u, 0x55667788u, 0x99aabbccu}};
    const auto good = source.definitions().add_good({"pre_gsx_good", 1'000});
    const std::array<RecipeFlow, 1> output{{{good, 10}}};
    const auto building_type = source.definitions().add_building_type("pre_gsx_building", 100, {}, output);
    const auto country = source.world().countries.create({"PGS", 1'000.0, 500.0, 100.0, 0.2});
    source.world().markets.resize(1u, source.definitions());
    source.world().markets.set_owner(MarketId{0u}, country);
    const auto state = source.world().geography.create_state({"pre_gsx_state", country, MarketId{0u}, {}});
    const auto design = source.world().grand_strategy.add_ship_design({country, 1u, 2u, 3u, 4'000});
    (void)source.world().grand_strategy.add_navy({country, state, 1'000u, 800'000u, design});
    (void)building_type;

    const auto checksum = source.world().checksum();
    auto save = source.make_save();
    constexpr std::uint32_t gsx_tag = 0x31585347u;
    const auto gsx_offset = find_u32_le(save.bytes, gsx_tag);
    assert(gsx_offset != save.bytes.size());
    // Simulate a v4 save written before GSX1 existed: keep the historical
    // sections and payload checksum, but remove the new extension tail.
    save.bytes.resize(gsx_offset);
    refresh_save_payload_framing(save.bytes);

    ThunderEngine restored{{0u, 0x55667788u, 0x99aabbccu}};
    const auto restored_good = restored.definitions().add_good({"pre_gsx_good", 1'000});
    const std::array<RecipeFlow, 1> restored_output{{{restored_good, 10}}};
    (void)restored.definitions().add_building_type("pre_gsx_building", 100, {}, restored_output);
    restored.restore(save.bytes);
    assert(restored.world().checksum() == checksum);
    assert(restored.world().grand_strategy.navys().size() == 1u);
    assert(restored.world().grand_strategy.navys()[0].mission == NavalMission::None);
    assert(!restored.world().grand_strategy.navys()[0].assigned_zone.valid());
}

void test_destroyed_entity_slots_roundtrip_and_recycle_order() {
    ThunderEngine source{{0u, 0x77889900u, 0x00112233u}};
    const auto good = source.definitions().add_good({"slot_test_good", 1'000});
    const NeedFlow needs[]{{good, 10}};
    const auto need_profile = source.definitions().add_need_profile("slot_test_needs", needs);
    const RecipeFlow output[]{{good, 1'000}};
    const auto building_type = source.definitions().add_building_type(
        "slot_test_building", 100, {}, output);
    const auto country = source.world().countries.create({"SLOT", 1'000.0, 100.0, 50.0, 0.2});
    source.world().markets.resize(1, source.definitions());
    source.world().markets.set_owner(MarketId{0u}, country);

    const auto b0 = source.world().buildings.create({MarketId{0u}, building_type});
    const auto b1 = source.world().buildings.create({MarketId{0u}, building_type});
    const auto b2 = source.world().buildings.create({MarketId{0u}, building_type});
    source.world().buildings.destroy(b0);
    source.world().buildings.destroy(b2);
    const auto p0 = source.world().pops.create(
        {MarketId{0u}, 100u, BuildingId{}, need_profile});
    const auto p1 = source.world().pops.create(
        {MarketId{0u}, 200u, BuildingId{}, need_profile});
    source.world().pops.destroy(p0);
    const auto expected_building_recycle =
        source.world().buildings.slot_pool().free_list().back();
    const auto expected_pop_recycle = source.world().pops.slot_pool().free_list().back();
    const auto checksum = source.world().checksum();
    const auto save = source.make_save();

    ThunderEngine restored{{0u, 0x77889900u, 0x00112233u}};
    const auto restored_good = restored.definitions().add_good({"slot_test_good", 1'000});
    const NeedFlow restored_needs[]{{restored_good, 10}};
    const auto restored_profile = restored.definitions().add_need_profile(
        "slot_test_needs", restored_needs);
    const RecipeFlow restored_output[]{{restored_good, 1'000}};
    const auto restored_type = restored.definitions().add_building_type(
        "slot_test_building", 100, {}, restored_output);
    restored.restore(save.bytes);
    assert(restored.world().checksum() == checksum);
    assert(restored.world().buildings.slot_pool().free_list().size() == 2u);
    assert(restored.world().pops.slot_pool().free_list().size() == 1u);
    assert(restored.world().buildings.slot_pool().free_list().back() == expected_building_recycle);
    assert(restored.world().pops.slot_pool().free_list().back() == expected_pop_recycle);
    assert(!restored.world().buildings.slot_pool().is_index_alive(b0.value()));
    assert(restored.world().buildings.slot_pool().is_index_alive(b1.value()));
    assert(!restored.world().buildings.slot_pool().is_index_alive(b2.value()));
    assert(!restored.world().pops.slot_pool().is_index_alive(p0.value()));
    assert(restored.world().pops.slot_pool().is_index_alive(p1.value()));

    const auto recycled_building = restored.world().buildings.create(
        {MarketId{0u}, restored_type});
    const auto recycled_pop = restored.world().pops.create(
        {MarketId{0u}, 300u, BuildingId{}, restored_profile});
    assert(recycled_building.value() == expected_building_recycle);
    assert(recycled_pop.value() == expected_pop_recycle);
}

void test_allocator_generation_enters_world_checksum() {
    World first;
    World recycled;
    const BuildingInit init{MarketId{0u}, BuildingTypeId{0u}};
    (void)first.buildings.create(init);
    const auto old = recycled.buildings.create(init);
    recycled.buildings.destroy(old);
    (void)recycled.buildings.create(init);

    // Both worlds expose one identical live SoA row.  The recycled slot has a
    // different generation, so a checksum that ignores allocator state would
    // incorrectly treat their future handle streams as equivalent.
    assert(first.buildings.size() == recycled.buildings.size());
    assert(first.buildings.checksum() == recycled.buildings.checksum());
    assert(first.checksum() != recycled.checksum());
}

void test_pre_mon1_v4_migrates_with_legacy_world_checksum() {
    constexpr std::uint32_t mon1_tag = 0x314e4f4du;
    ThunderEngine source{{0u, 0x11112222u, 0x33334444u}};
    add_market_monetary_fixture(source, false);
    auto save = source.make_save();
    const auto mon1_offset = find_u32_le(save.bytes, mon1_tag);
    assert(mon1_offset != save.bytes.size() && mon1_offset >= 60u);
    save.bytes.resize(mon1_offset);
    write_u64_le(save.bytes, 28u, pre_mon1_world_checksum(source.world()));
    refresh_save_payload_framing(save.bytes);

    ThunderEngine restored{{0u, 0x11112222u, 0x33334444u}};
    add_matching_market_definitions(restored);
    restored.restore(save.bytes);
    assert(restored.world().markets.size() == 2u);
    for (std::uint32_t index = 0; index < 2u; ++index) {
        const MarketId market{index};
        assert(restored.world().markets.settlement_account(market) ==
               market_settlement_account_id(market));
        assert(restored.world().markets.currency_key(market) == default_currency_key);
        assert(restored.world().markets.clearing_cash(market) == 0);
    }
}

void test_corrupt_or_duplicate_mon1_is_atomic() {
    constexpr std::uint32_t mon1_tag = 0x314e4f4du;
    ThunderEngine source{{0u, 0xabcddcbau, 0x13572468u}};
    add_market_monetary_fixture(source, true);
    const auto save = source.make_save();
    const auto mon1_offset = find_u32_le(save.bytes, mon1_tag);
    assert(mon1_offset != save.bytes.size());

    ThunderEngine victim{{0u, 0xabcddcbau, 0x13572468u}};
    add_matching_market_definitions(victim);
    seed_world(victim, "SAFE");
    const auto checksum_before = victim.engine_checksum();
    const auto reject_atomically = [&](std::vector<std::byte> malformed) {
        bool rejected = false;
        try {
            victim.restore(malformed);
        } catch (const std::exception&) {
            rejected = true;
        }
        assert(rejected);
        assert(victim.engine_checksum() == checksum_before);
        assert(victim.world().countries.tag(CountryId{0u}) == "SAFE");
    };

    auto bad_account = save.bytes;
    write_u64_le(bad_account, mon1_offset + 8u, 0u);
    refresh_save_payload_framing(bad_account);
    reject_atomically(std::move(bad_account));

    auto zero_currency = save.bytes;
    write_u64_le(zero_currency, mon1_offset + 16u, 0u);
    refresh_save_payload_framing(zero_currency);
    reject_atomically(std::move(zero_currency));

    auto bad_count = save.bytes;
    write_u32_le(bad_count, mon1_offset + 4u, 1u);
    refresh_save_payload_framing(bad_count);
    reject_atomically(std::move(bad_count));

    auto duplicate = save.bytes;
    duplicate.insert(duplicate.end(), save.bytes.begin() +
                         static_cast<std::ptrdiff_t>(mon1_offset), save.bytes.end());
    refresh_save_payload_framing(duplicate);
    reject_atomically(std::move(duplicate));
}

std::uint64_t old_ai_v3_checksum(const UtilityAiEngine& ai) {
    Fnv1a64 hash;
    std::uint64_t action_xor = 0;
    std::uint64_t action_sum = 0;
    for (const auto& action : ai.actions()) {
        Fnv1a64 one;
        one.add(std::string_view{action.key});
        one.add(static_cast<std::uint8_t>(action.scope));
        one.add(action.base_utility);
        one.add(action.cooldown_ticks);
        const auto value = one.value();
        action_xor ^= value;
        action_sum += value * 0x9e3779b97f4a7c15ull;
    }
    hash.add(ai.actions().size()); hash.add(action_xor); hash.add(action_sum);
    std::uint64_t state_xor = 0;
    std::uint64_t state_sum = 0;
    for (const auto& record : ai.state()) {
        Fnv1a64 one;
        one.add(std::string_view{ai.actions()[record.action].key});
        one.add(static_cast<std::uint8_t>(record.scope.type)); one.add(record.scope.raw_id); one.add(record.last_tick);
        const auto value = one.value();
        state_xor ^= value;
        state_sum += value * 0x517cc1b727220a95ull;
    }
    hash.add(ai.state().size()); hash.add(state_xor); hash.add(state_sum);
    return hash.value();
}

void test_population_growth_section_roundtrip() {
    // v5 demographic state: per-POP fractional growth accumulators persist and
    // participate in the world checksum.
    ThunderEngine source{{0u, 0x60708090u, 0xa0b0c0d0u}};
    const auto good = source.definitions().add_good({"growth_test_good", 1'000});
    const std::array<NeedFlow, 1> needs{{{good, 1'000}}};
    const auto profile = source.definitions().add_need_profile("growth_profile", needs);
    const auto country = seed_world(source);
    source.world().markets.resize(1u, source.definitions());
    source.world().markets.set_owner(MarketId{0u}, country);
    PopInit init{};
    init.market = MarketId{0u};
    init.size = 4'000u;
    init.need_profile = profile;
    const auto pop = source.world().pops.create(init);
    source.world().pops.growth_progress_mut()[pop.value()] = 123'456;
    const auto checksum = source.world().checksum();
    const auto save = source.make_save();
    assert(save.metadata.version == 5u);

    ThunderEngine restored{{0u, 0x60708090u, 0xa0b0c0d0u}};
    restored.definitions().add_good({"growth_test_good", 1'000});
    const std::array<NeedFlow, 1> restored_needs{{{GoodId{0u}, 1'000}}};
    restored.definitions().add_need_profile("growth_profile", restored_needs);
    restored.restore(save.bytes);
    assert(restored.world().checksum() == checksum);
    assert(restored.world().pops.growth_progress_milli(PopId{0u}) == 123'456);
}

void test_runtime_v3_read_only_migration() {
    ThunderEngine source{{0u, 0x33445566u, 0x778899AAu}};
    const auto country = seed_world(source);
    register_runtime_content(source, false, false);
    create_runtime_state(source, country);
    auto save = source.make_save();
    assert(save.metadata.version == 5u);
    assert(source.ai().plans().empty());
    assert(source.ai().plan_state().empty());

    // Current v4 appends a zero plan-state count and the tagged stable gameplay
    // context extension to the v3 payload. Remove both, rewrite framing/checksums,
    // and verify the v3 migration path against an authentic prior-runtime layout.
    assert(save.bytes.size() >= 64u);
    constexpr std::uint32_t gameplay_context_tag = 0x31544347u;
    const auto context_offset = find_u32_le(save.bytes, gameplay_context_tag);
    assert(context_offset != save.bytes.size() && context_offset >= 64u);
    save.bytes.resize(context_offset - 4u);
    write_u32_le(save.bytes, 8u, 3u);
    write_u64_le(save.bytes, 28u, pre_mon1_world_checksum(source.world()));
    auto legacy_instances = std::vector<GameplayInstance>{source.gameplay().instances().begin(),
                                                          source.gameplay().instances().end()};
    for (auto& instance : legacy_instances) {
        instance.id = {};
        instance.context.reset();
    }
    Fnv1a64 runtime_hash;
    runtime_hash.add(source.gameplay().checksum_state(legacy_instances, source.gameplay().log(), 0u));
    runtime_hash.add(old_ai_v3_checksum(source.ai()));
    write_u64_le(save.bytes, 36u, runtime_hash.value());
    write_u64_le(save.bytes, 44u, static_cast<std::uint64_t>(save.bytes.size() - 60u));
    Fnv1a64 payload_hash;
    payload_hash.add_bytes(std::span<const std::byte>(reinterpret_cast<const std::byte*>(save.bytes.data()), save.bytes.size()).subspan(60u));
    write_u64_le(save.bytes, 52u, payload_hash.value());

    ThunderEngine restored{{0u, 0x33445566u, 0x778899AAu}};
    register_runtime_content(restored, true, false);
    restored.restore(save.bytes);
    assert(restored.engine_checksum() == source.engine_checksum());
    assert(restored.ai().plan_state().empty());
    assert(restored.gameplay().instances().size() == source.gameplay().instances().size());
}

void test_duplicate_keys_rejected() {
    ThunderEngine engine{{0u, 0u, 0u}};
    GameplayDefinition first;
    first.key = "duplicate";
    first.scope = ScopeType::Country;
    engine.gameplay().add_definition(first);
    bool gameplay_rejected = false;
    try { engine.gameplay().add_definition(first); } catch (const std::invalid_argument&) { gameplay_rejected = true; }
    assert(gameplay_rejected);

    AiActionDefinition action;
    action.key = "duplicate.ai";
    action.scope = ScopeType::Country;
    engine.ai().add_action(action);
    bool ai_rejected = false;
    try { engine.ai().add_action(action); } catch (const std::invalid_argument&) { ai_rejected = true; }
    assert(ai_rejected);

    AiPlanDefinition plan;
    plan.key = "duplicate.plan";
    plan.scope = ScopeType::Country;
    plan.action_keys = {"duplicate.ai"};
    engine.ai().add_plan(plan);
    bool plan_rejected = false;
    try { engine.ai().add_plan(plan); } catch (const std::invalid_argument&) { plan_rejected = true; }
    assert(plan_rejected);
}

void test_compressed_save_and_tamper_protection() {
    ThunderEngine engine;
    for (int i = 0; i < 50; ++i) {
        engine.world().countries.create({"TAG" + std::to_string(i), 50000.0 + i * 1000.0, 100.0, 20.0, 0.20});
    }
    EconomyDefinitions defs;

    const auto raw_save = engine.make_save();
    const auto comp_save = SaveGameCodec::encode_compressed(
        engine.world(), engine.clock(), engine.gameplay(),
        engine.ai(), engine.notifications(), engine.on_actions(),
        0, 0, 3);

#if defined(THUNDER_HAS_ZSTD)
    assert(comp_save.bytes.size() < raw_save.bytes.size());
#else
    assert(comp_save.bytes.size() == raw_save.bytes.size());
#endif

    ThunderEngine restored;
    const auto meta = SaveGameCodec::decode(
        comp_save.bytes, restored.world(), restored.clock(),
        restored.gameplay(), restored.ai(), restored.notifications(),
        restored.on_actions(), defs);

    assert(meta.world_checksum == raw_save.metadata.world_checksum);
    assert(restored.world().checksum() == engine.world().checksum());

    auto tampered_bytes = comp_save.bytes;
    assert(tampered_bytes.size() > 40);
    tampered_bytes[tampered_bytes.size() - 5] ^= std::byte{0xff};
    bool tamper_caught = false;
    try {
        ThunderEngine tampered_engine;
        SaveGameCodec::decode(
            tampered_bytes, tampered_engine.world(), tampered_engine.clock(),
            tampered_engine.gameplay(), tampered_engine.ai(), tampered_engine.notifications(),
            tampered_engine.on_actions(), defs);
    } catch (const std::runtime_error&) {
        tamper_caught = true;
    }
    assert(tamper_caught);
}

} // namespace

int main() {
    std::cout << "[RUNNING test_stable_key_restore_across_registration_order]\n" << std::flush;
    test_stable_key_restore_across_registration_order();
    std::cout << "[RUNNING test_event_context_survives_choice_and_save_restore]\n" << std::flush;
    test_event_context_survives_choice_and_save_restore();
    std::cout << "[RUNNING test_global_script_state_survives_save_restore]\n" << std::flush;
    test_global_script_state_survives_save_restore();
    std::cout << "[RUNNING test_construction_queue_survives_save_restore_and_rejects_bad_targets]\n" << std::flush;
    test_construction_queue_survives_save_restore_and_rejects_bad_targets();
    std::cout << "[RUNNING test_failed_runtime_validation_is_atomic]\n" << std::flush;
    test_failed_runtime_validation_is_atomic();
    std::cout << "[RUNNING test_missing_runtime_definition_is_atomic]\n" << std::flush;
    test_missing_runtime_definition_is_atomic();
    std::cout << "[RUNNING test_legacy_v1_save_migration]\n" << std::flush;
    test_legacy_v1_save_migration();
    std::cout << "[RUNNING test_runtime_v3_read_only_migration]\n" << std::flush;
    test_runtime_v3_read_only_migration();
    std::cout << "[RUNNING test_population_growth_section_roundtrip]\n" << std::flush;
    test_population_growth_section_roundtrip();
    std::cout << "[RUNNING test_market_monetary_roundtrip_and_stable_accounts]\n" << std::flush;
    test_market_monetary_roundtrip_and_stable_accounts();
    std::cout << "[RUNNING test_financial_section_roundtrip_and_atomic_validation]\n" << std::flush;
    test_financial_section_roundtrip_and_atomic_validation();
    std::cout << "[RUNNING test_grand_strategy_extension_roundtrip]\n" << std::flush;
    test_grand_strategy_extension_roundtrip();
    std::cout << "[RUNNING test_pre_gsx_save_keeps_idle_navy_compatible]\n" << std::flush;
    test_pre_gsx_save_keeps_idle_navy_compatible();
    std::cout << "[RUNNING test_destroyed_entity_slots_roundtrip_and_recycle_order]\n" << std::flush;
    test_destroyed_entity_slots_roundtrip_and_recycle_order();
    std::cout << "[RUNNING test_allocator_generation_enters_world_checksum]\n" << std::flush;
    test_allocator_generation_enters_world_checksum();
    std::cout << "[RUNNING test_pre_mon1_v4_migrates_with_legacy_world_checksum]\n" << std::flush;
    test_pre_mon1_v4_migrates_with_legacy_world_checksum();
    std::cout << "[RUNNING test_corrupt_or_duplicate_mon1_is_atomic]\n" << std::flush;
    test_corrupt_or_duplicate_mon1_is_atomic();
    std::cout << "[RUNNING test_duplicate_keys_rejected]\n" << std::flush;
    test_duplicate_keys_rejected();
    std::cout << "[RUNNING test_compressed_save_and_tamper_protection]\n" << std::flush;
    test_compressed_save_and_tamper_protection();
    std::cout << "Thunder 1.0 runtime save tests: PASS\n";
    return 0;
}
