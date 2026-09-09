#include "thunder/content/definition/ContentLoader.hpp"
#include "thunder/content/definition/DefinitionDatabase.hpp"
#include "thunder/content/definition/ModManifest.hpp"
#include "thunder/content/definition/VirtualFileSystem.hpp"
#include "thunder/scripting/ScriptRegistry.hpp"
#include "thunder/scripting/SymbolTable.hpp"
#include "TestTempPath.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace thunder;

namespace {

ModPackage package_from(std::string_view source, std::filesystem::path root,
                        std::uint64_t content_hash, std::string_view source_name = "test.thundermod") {
    auto parsed = parse_mod_manifest(source, source_name);
    if (!parsed.ok()) {
        for (const auto& diagnostic : parsed.diagnostics) {
            std::cerr << diagnostic.message << '\n';
        }
    }
    assert(parsed.ok());
    return {std::move(parsed.manifest), std::move(root), content_hash};
}

std::vector<std::string> ids(const ModLoadPlan& plan) {
    std::vector<std::string> result;
    result.reserve(plan.entries.size());
    for (const auto& entry : plan.entries) result.push_back(entry.manifest.id);
    return result;
}

bool has_code(const ModLoadPlan& plan, ModDiagnosticCode code) {
    return std::any_of(plan.diagnostics.begin(), plan.diagnostics.end(),
                       [code](const ModDiagnostic& diagnostic) {
                           return diagnostic.code == code;
                       });
}

void test_manifest_parser_and_semantic_versions() {
    const auto parsed = parse_mod_manifest(R"MOD(
        # A manifest is independent of any particular game.
        id = "example.trade-rebalance"
        version = "2.4.1-beta.2+build.7"
        load_priority = -25
        required = thunder.base >=1.0.0, <2.0.0
        optional = example.ui-hooks@^3.1.0
        conflict = example.legacy <2.0.0
        load_before = example.total-conversion
        load_after = thunder.base
    )MOD", "trade.thundermod");

    assert(parsed.ok());
    assert(parsed.manifest.id == "example.trade-rebalance");
    assert(parsed.manifest.stable_id == stable_mod_id("example.trade-rebalance"));
    assert(parsed.manifest.version.to_string() == "2.4.1-beta.2+build.7");
    assert(parsed.manifest.load_priority == -25);
    assert(parsed.manifest.required_dependencies.size() == 1u);
    assert(parsed.manifest.optional_dependencies.size() == 1u);
    assert(parsed.manifest.conflicts.size() == 1u);

    const auto compatible = ModVersion::parse("1.8.0");
    const auto incompatible = ModVersion::parse("2.0.0");
    assert(compatible && incompatible);
    assert(parsed.manifest.required_dependencies[0].version.matches(*compatible));
    assert(!parsed.manifest.required_dependencies[0].version.matches(*incompatible));

    const auto prerelease = ModVersion::parse("1.0.0-rc.1");
    const auto release = ModVersion::parse("1.0.0");
    assert(prerelease && release && *prerelease < *release);
    assert(stable_mod_version_key(*release) == stable_mod_version_key(*release));
}

void test_registration_order_does_not_change_plan_or_hash() {
    std::vector<ModPackage> packages;
    packages.push_back(package_from(R"MOD(
        id = thunder.base
        version = 1.5.0
        load_priority = 0
    )MOD", "base", 0x101u));
    packages.push_back(package_from(R"MOD(
        id = example.economy
        version = 2.0.0
        load_priority = 10
        required = thunder.base ^1.0.0
    )MOD", "economy", 0x202u));
    packages.push_back(package_from(R"MOD(
        id = example.interface
        version = 3.0.0
        load_priority = 10
        optional = example.economy >=2.0.0
    )MOD", "interface", 0x303u));
    packages.push_back(package_from(R"MOD(
        id = example.compat
        version = 1.0.0
        load_priority = 10
        load_after = example.interface
    )MOD", "compat", 0x404u));

    const auto forward = build_mod_load_plan(packages);
    std::reverse(packages.begin(), packages.end());
    const auto reverse = build_mod_load_plan(packages);

    assert(forward.ok() && reverse.ok());
    assert(ids(forward) == ids(reverse));
    assert(forward.content_hash == reverse.content_hash);
    assert((ids(forward) == std::vector<std::string>{"thunder.base", "example.economy",
                                                      "example.interface", "example.compat"}));

    packages[0].package_content_hash ^= 0x10u;
    const auto changed_package = build_mod_load_plan(packages);
    assert(changed_package.ok());
    assert(changed_package.content_hash != forward.content_hash);
}

void test_explicit_priority_is_a_stable_ready_queue_tie_breaker() {
    std::vector<ModPackage> packages;
    packages.push_back(package_from(R"MOD(
        id = z.base
        version = 1.0.0
        load_priority = 100
    )MOD", "z", 1u));
    packages.push_back(package_from(R"MOD(
        id = a.independent
        version = 1.0.0
        load_priority = 0
    )MOD", "a", 2u));
    packages.push_back(package_from(R"MOD(
        id = b.feature
        version = 1.0.0
        load_priority = -100
        required = z.base =1.0.0
    )MOD", "b", 3u));
    packages.push_back(package_from(R"MOD(
        id = c.post
        version = 1.0.0
        load_priority = -200
        load_after = b.feature
    )MOD", "c", 4u));
    packages.push_back(package_from(R"MOD(
        id = aa.same-priority
        version = 1.0.0
        load_priority = 0
    )MOD", "aa", 5u));

    const auto plan = build_mod_load_plan(packages);
    assert(plan.ok());
    // Priority orders only currently-ready nodes; dependency edges always win.
    assert((ids(plan) == std::vector<std::string>{"a.independent", "aa.same-priority", "z.base",
                                                  "b.feature", "c.post"}));
    for (std::size_t i = 0u; i < plan.entries.size(); ++i) {
        assert(plan.entries[i].load_index == i);
    }
}

void test_cycle_diagnostic() {
    std::vector<ModPackage> packages;
    packages.push_back(package_from(R"MOD(
        id = cycle.a
        version = 1.0.0
        load_after = cycle.b
    )MOD", "a", 1u));
    packages.push_back(package_from(R"MOD(
        id = cycle.b
        version = 1.0.0
        load_after = cycle.c
    )MOD", "b", 2u));
    packages.push_back(package_from(R"MOD(
        id = cycle.c
        version = 1.0.0
        required = cycle.a
    )MOD", "c", 3u));

    const auto plan = build_mod_load_plan(packages);
    assert(!plan.ok());
    assert(plan.entries.empty());
    assert(plan.content_hash == 0u);
    assert(has_code(plan, ModDiagnosticCode::DependencyCycle));
}

void test_missing_required_and_version_mismatch_diagnostics() {
    std::vector<ModPackage> missing;
    missing.push_back(package_from(R"MOD(
        id = example.feature
        version = 1.0.0
        required = thunder.base >=1.0.0
    )MOD", "feature", 1u));
    auto plan = build_mod_load_plan(missing);
    assert(!plan.ok());
    assert(has_code(plan, ModDiagnosticCode::MissingRequiredDependency));

    std::vector<ModPackage> mismatch;
    mismatch.push_back(package_from(R"MOD(
        id = thunder.base
        version = 1.0.0
    )MOD", "base", 2u));
    mismatch.push_back(package_from(R"MOD(
        id = example.feature
        version = 1.0.0
        required = thunder.base >=2.0.0
    )MOD", "feature", 3u));
    plan = build_mod_load_plan(mismatch);
    assert(!plan.ok());
    assert(has_code(plan, ModDiagnosticCode::RequiredVersionMismatch));
}

void test_conflict_diagnostic() {
    std::vector<ModPackage> packages;
    packages.push_back(package_from(R"MOD(
        id = example.overhaul
        version = 3.0.0
        conflict = example.legacy <2.0.0
    )MOD", "overhaul", 1u));
    packages.push_back(package_from(R"MOD(
        id = example.legacy
        version = 1.5.0
    )MOD", "legacy", 2u));

    const auto plan = build_mod_load_plan(packages);
    assert(!plan.ok());
    assert(has_code(plan, ModDiagnosticCode::Conflict));
}

void test_load_plan_mounts_vfs_in_resolved_overlay_order() {
    const auto root = thunder_test::unique_temp_path("thunder_mod_pipeline_tests");
    const auto base = root / "base";
    const auto feature = root / "feature";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(base / "common");
    std::filesystem::create_directories(feature / "common");
    {
        std::ofstream output(base / "common/value.thunder");
        output << "country TST { treasury = 10 }\n";
    }
    {
        std::ofstream output(feature / "common/value.thunder");
        output << "country TST { treasury = 99 }\n";
    }

    std::vector<ModPackage> packages;
    packages.push_back(package_from(R"MOD(
        id = thunder.base
        version = 1.0.0
    )MOD", base, 0xaaaa));
    packages.push_back(package_from(R"MOD(
        id = example.feature
        version = 1.0.0
        required = thunder.base
    )MOD", feature, 0xbbbb));
    const auto plan = build_mod_load_plan(packages);
    assert(plan.ok());

    VirtualFileSystem vfs;
    vfs.mount_plan(plan);
    assert(vfs.has_load_plan());
    assert(vfs.load_plan_hash() == plan.content_hash);
    const auto files = vfs.enumerate();
    assert(files.size() == 1u);
    assert(files.front().mount_name == "example.feature");
    assert(VirtualFileSystem::read_text(files.front()).find("99") != std::string::npos);

    bool rejected_append = false;
    try {
        vfs.mount({"late", feature, 100});
    } catch (const std::logic_error&) {
        rejected_append = true;
    }
    assert(rejected_append);
    std::filesystem::remove_all(root);
}

void test_package_hash_reaches_effective_content_hash() {
    const auto root = thunder_test::unique_temp_path("thunder_mod_hash_pipeline_tests");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "common");
    {
        std::ofstream output(root / "common/base.thunder");
        output << "country TST { population = 100 treasury = 10 }\n";
    }

    std::vector<ModPackage> packages;
    packages.push_back(package_from(R"MOD(
        id = thunder.base
        version = 1.0.0
    )MOD", root, 0x1111u));
    const auto first_plan = build_mod_load_plan(packages);
    packages.front().package_content_hash = 0x2222u;
    const auto second_plan = build_mod_load_plan(packages);
    assert(first_plan.ok() && second_plan.ok());

    VirtualFileSystem first_vfs;
    first_vfs.mount_plan(first_plan);
    SymbolTable first_symbols;
    const auto first_registry = ScriptRegistry::make_builtin();
    DefinitionDatabase first_definitions{first_symbols, first_registry};
    const auto first_result = ContentLoader{first_symbols, first_registry}.load(
        first_vfs, first_definitions);

    VirtualFileSystem second_vfs;
    second_vfs.mount_plan(second_plan);
    SymbolTable second_symbols;
    const auto second_registry = ScriptRegistry::make_builtin();
    DefinitionDatabase second_definitions{second_symbols, second_registry};
    const auto second_result = ContentLoader{second_symbols, second_registry}.load(
        second_vfs, second_definitions);

    assert(first_result.ok() && second_result.ok());
    assert(first_result.file_count == 1u && second_result.file_count == 1u);
    assert(first_result.content_hash != second_result.content_hash);
    std::filesystem::remove_all(root);
}

void test_sample_content_definitions_load() {
    const std::filesystem::path defs_dir = "data/definitions";
    if (!std::filesystem::exists(defs_dir / "base.thundermod")) return;

    VirtualFileSystem vfs;
    vfs.mount({"base", defs_dir, 0});
    SymbolTable symbols;
    const auto registry = ScriptRegistry::make_builtin();
    DefinitionDatabase definitions{symbols, registry};
    const auto result = ContentLoader{symbols, registry}.load(vfs, definitions);

    if (!result.ok()) {
        for (const auto& d : result.diagnostics) {
            std::cerr << "Diagnostic: " << d.message << " at line " << d.line << "\n";
        }
    }
    assert(result.ok());
    assert(result.file_count >= 4u);
    assert(definitions.countries().size() >= 5u);
    assert(definitions.goods().size() >= 4u);
    const auto en_sym = symbols.intern("en");
    const auto zh_sym = symbols.intern("zh");
    const auto gbr_sym = symbols.intern("GBR");
    assert(definitions.localization().lookup(en_sym, gbr_sym) == "Great Britain");
    assert(definitions.localization().lookup(zh_sym, gbr_sym) == "大不列颠");
}

} // namespace

int main() {
    test_manifest_parser_and_semantic_versions();
    test_registration_order_does_not_change_plan_or_hash();
    test_explicit_priority_is_a_stable_ready_queue_tie_breaker();
    test_cycle_diagnostic();
    test_missing_required_and_version_mismatch_diagnostics();
    test_conflict_diagnostic();
    test_load_plan_mounts_vfs_in_resolved_overlay_order();
    test_package_hash_reaches_effective_content_hash();
    test_sample_content_definitions_load();
    std::cout << "Thunder mod pipeline tests passed\n";
    return 0;
}
