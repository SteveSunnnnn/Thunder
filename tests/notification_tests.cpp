#include "thunder/content/definition/DefinitionDatabase.hpp"
#include "thunder/runtime/engine/ThunderEngine.hpp"
#include "thunder/scripting/ThunderScriptParser.hpp"
#include "thunder/scripting/SymbolTable.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace thunder;

namespace {

constexpr std::string_view notification_content = R"(
script notification_eligible {
    scope = country
    trigger = { treasury_above = 10 }
    effect = { add_treasury = 5 }
}

notification fiscal_notice {
    scope = country
    title = fiscal.title
    body = fiscal.body
    icon = ui.fiscal
    category = economy
    priority = high
    dedupe = replace
    lifetime_ticks = 8
    potential = notification_eligible
    action = {
        key = apply
        label = fiscal.apply
        allow = notification_eligible
        effect = notification_eligible
    }
}

notification suppressed_notice {
    scope = country
    title = suppress.title
    body = suppress.body
    dedupe = suppress
}

notification stacked_notice {
    scope = country
    title = stack.title
    body = stack.body
    dedupe = stack
}
)";

struct BoundContent {
    SymbolTable symbols;
    ScriptRegistry registry = ScriptRegistry::make_builtin();
    DefinitionDatabase definitions{symbols, registry};
    NotificationRuntime runtime{registry};

    BoundContent() {
        ThunderScriptParser parser{symbols};
        const auto parsed = parser.parse(notification_content, "notification_test.thunder");
        assert(parsed.ok());
        std::vector<ScriptCompileDiagnostic> diagnostics;
        assert(definitions.ingest(parsed, diagnostics));
        assert(definitions.compile_scripts(parsed, diagnostics));
        assert(definitions.bind_notifications(runtime, diagnostics));
        assert(diagnostics.empty());
    }
};

std::uint32_t definition_id(const NotificationRuntime& runtime, std::string_view key) {
    const auto definitions = runtime.definitions();
    for (std::uint32_t i = 0; i < definitions.size(); ++i)
        if (definitions[i].key == key) return i;
    throw std::runtime_error("missing notification definition in test");
}

CountryId seed_country(World& world, std::string_view tag = "TST", double treasury = 20.0) {
    return world.countries.create({std::string{tag}, 1'000'000.0, 100.0, treasury, 0.2});
}

NotificationDefinition make_definition(std::string key,
                                       NotificationDedupePolicy dedupe,
                                       std::uint64_t lifetime = 0u) {
    NotificationDefinition definition;
    definition.key = std::move(key);
    definition.scope = ScopeType::Country;
    definition.title_key = definition.key + ".title";
    definition.body_key = definition.key + ".body";
    definition.dedupe = dedupe;
    definition.lifetime_ticks = lifetime;
    definition.actions.push_back({"inspect", definition.key + ".inspect", std::nullopt, std::nullopt});
    definition.actions.push_back({"accept", definition.key + ".accept", std::nullopt, std::nullopt});
    return definition;
}

void test_content_runtime_dedupe_actions_and_expiry() {
    BoundContent content;
    World world;
    const auto country = seed_country(world);
    const auto other = seed_country(world, "SRC", 5.0);
    const auto scope = ScopeRef::country(country);

    const auto fiscal = definition_id(content.runtime, "fiscal_notice");
    const auto fiscal_id = content.runtime.emit(fiscal, world, scope, 1u,
                                                ScopeRef::country(other), {}, 77u);
    assert(fiscal_id.valid());
    assert(content.runtime.mark_read(fiscal_id, 2u));
    const auto replaced = content.runtime.emit(fiscal, world, scope, 3u, scope, {}, 77u);
    assert(replaced == fiscal_id);
    const auto* fiscal_instance = content.runtime.find_instance(fiscal_id);
    assert(fiscal_instance != nullptr);
    assert(fiscal_instance->occurrence_count == 2u);
    assert(fiscal_instance->state == NotificationState::Unread);
    assert(fiscal_instance->source == scope);
    assert(fiscal_instance->expires_tick == 11u);
    assert(content.runtime.choose_action(fiscal_id, 0u, world, 4u));
    assert(world.countries.treasury(country) == 25.0);
    assert(content.runtime.find_instance(fiscal_id)->state == NotificationState::Actioned);

    const auto suppressed = definition_id(content.runtime, "suppressed_notice");
    const auto suppress_id = content.runtime.emit(suppressed, world, scope, 5u,
                                                  ScopeRef::country(other), {}, 9u);
    assert(content.runtime.mark_read(suppress_id, 6u));
    assert(content.runtime.emit(suppressed, world, scope, 7u, scope, {}, 9u) == suppress_id);
    const auto* suppressed_instance = content.runtime.find_instance(suppress_id);
    assert(suppressed_instance->state == NotificationState::Read);
    assert(suppressed_instance->source == ScopeRef::country(other));
    assert(suppressed_instance->occurrence_count == 2u);

    const auto stacked = definition_id(content.runtime, "stacked_notice");
    const auto first_stack = content.runtime.emit(stacked, world, scope, 8u);
    const auto second_stack = content.runtime.emit(stacked, world, scope, 8u);
    assert(first_stack.valid() && second_stack.valid() && first_stack != second_stack);

    World ineligible_world;
    const auto ineligible = seed_country(ineligible_world, "LOW", 5.0);
    assert(!content.runtime.emit(fiscal, ineligible_world, ScopeRef::country(ineligible), 0u).valid());

    NotificationRuntime expiring{content.registry};
    const auto expiring_definition = expiring.add_definition(
        make_definition("expires", NotificationDedupePolicy::Stack, 4u));
    const auto expiring_id = expiring.emit(expiring_definition, world, scope, 10u);
    assert(expiring.update(13u) == 0u);
    assert(expiring.update(14u) == 1u);
    assert(expiring.find_instance(expiring_id)->state == NotificationState::Expired);
}

void register_save_definitions(ThunderEngine& engine, bool reverse) {
    auto alpha = make_definition("notification.alpha", NotificationDedupePolicy::Replace, 8u);
    auto beta = make_definition("notification.beta", NotificationDedupePolicy::Stack);
    if (reverse) {
        engine.notifications().add_definition(std::move(beta));
        engine.notifications().add_definition(std::move(alpha));
    } else {
        engine.notifications().add_definition(std::move(alpha));
        engine.notifications().add_definition(std::move(beta));
    }
}

void test_stable_key_save_restore_and_continuation() {
    ThunderEngine source{{0u, 0x12345678u, 0xabcdef01u}};
    const auto country = source.world().countries.create({"SRC", 1000.0, 100.0, 50.0, 0.2});
    register_save_definitions(source, false);
    const auto alpha = definition_id(source.notifications(), "notification.alpha");
    const auto beta = definition_id(source.notifications(), "notification.beta");
    const auto alpha_id = source.notifications().emit(alpha, source.world(),
        ScopeRef::country(country), 0u, {}, {}, 11u);
    const auto beta_id = source.notifications().emit(beta, source.world(),
        ScopeRef::country(country), 0u);
    assert(source.notifications().choose_action(beta_id, 1u, source.world(), 0u));
    const auto checksum_before = source.engine_checksum();
    const auto save = source.make_save();

    ThunderEngine restored{{0u, 0x12345678u, 0xabcdef01u}};
    restored.world().countries.create({"OLD", 1.0, 1.0, 1.0, 0.1});
    register_save_definitions(restored, true);
    restored.restore(save.bytes);
    assert(restored.engine_checksum() == checksum_before);
    assert(restored.notifications().instances().size() == 2u);
    const auto* restored_alpha = restored.notifications().find_instance(alpha_id);
    const auto* restored_beta = restored.notifications().find_instance(beta_id);
    assert(restored_alpha != nullptr && restored_beta != nullptr);
    assert(restored.notifications().definitions()[restored_alpha->definition].key ==
           "notification.alpha");
    const auto& restored_beta_definition =
        restored.notifications().definitions()[restored_beta->definition];
    assert(restored_beta_definition.key == "notification.beta");
    assert(restored_beta_definition.actions[restored_beta->chosen_action].key == "accept");
    assert(restored.notifications().next_instance_id() == source.notifications().next_instance_id());

    source.advance_ticks(8u);
    restored.advance_ticks(8u);
    assert(source.engine_checksum() == restored.engine_checksum());
    assert(source.notifications().find_instance(alpha_id)->state == NotificationState::Expired);
}

void test_missing_definition_restore_is_atomic() {
    ThunderEngine source{{0u, 0u, 0u}};
    const auto country = source.world().countries.create({"SRC", 1000.0, 100.0, 20.0, 0.2});
    register_save_definitions(source, false);
    const auto alpha = definition_id(source.notifications(), "notification.alpha");
    assert(source.notifications().emit(alpha, source.world(), ScopeRef::country(country), 0u).valid());
    const auto save = source.make_save();

    ThunderEngine victim{{0u, 0u, 0u}};
    victim.world().countries.create({"VICTIM", 1000.0, 50.0, 7.0, 0.2});
    victim.notifications().add_definition(
        make_definition("notification.unrelated", NotificationDedupePolicy::Stack));
    const auto checksum_before = victim.engine_checksum();
    bool rejected = false;
    try {
        victim.restore(save.bytes);
    } catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected);
    assert(victim.engine_checksum() == checksum_before);
    assert(victim.world().countries.tag(CountryId{0}) == "VICTIM");
    assert(victim.notifications().instances().empty());
}

void test_state_and_content_validation() {
    BoundContent content;
    World world;
    const auto country = seed_country(world);
    const auto id = definition_id(content.runtime, "stacked_notice");
    NotificationInstance invalid;
    invalid.id = NotificationInstanceId{1u};
    invalid.definition = id;
    invalid.scope = ScopeRef::country(country);
    invalid.created_tick = 1u;
    invalid.updated_tick = 1u;
    invalid.occurrence_count = 0u;
    bool rejected = false;
    try {
        content.runtime.validate_state({&invalid, 1u}, 2u, world, 1u);
    } catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected);

    ThunderScriptParser parser{content.symbols};
    const auto parsed = parser.parse(R"(
notification broken {
    scope = country
    title = broken.title
    body = broken.body
    action = { key = same label = one }
    action = { key = same label = two }
}
)", "broken_notification.thunder");
    assert(parsed.ok());
    std::vector<ScriptCompileDiagnostic> diagnostics;
    assert(!content.definitions.ingest(parsed, diagnostics));
    assert(!diagnostics.empty());

    NotificationDefinition duplicate = make_definition(
        "runtime.duplicate", NotificationDedupePolicy::Stack);
    NotificationRuntime runtime{content.registry};
    runtime.add_definition(duplicate);
    rejected = false;
    try {
        runtime.add_definition(std::move(duplicate));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

} // namespace

int main() {
    test_content_runtime_dedupe_actions_and_expiry();
    test_stable_key_save_restore_and_continuation();
    test_missing_definition_restore_is_atomic();
    test_state_and_content_validation();
    std::cout << "Thunder 1.0 notification runtime tests: PASS\n";
    return 0;
}
