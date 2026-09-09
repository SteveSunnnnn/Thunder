#include "thunder/content/definition/DefinitionDatabase.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace thunder {

DefinitionDatabase::DefinitionDatabase(SymbolTable& symbols, const ScriptRegistry& registry)
    : symbols_(symbols), registry_(registry), compiler_(symbols, registry), localization_(symbols),
      gameplay_content_(symbols), research_content_(symbols), notification_content_(symbols),
      on_action_content_(symbols) {
    sym_country_ = symbols_.intern("country");
    sym_population_ = symbols_.intern("population");
    sym_gdp_ = symbols_.intern("gdp");
    sym_treasury_ = symbols_.intern("treasury");
    sym_tax_rate_ = symbols_.intern("tax_rate");
    sym_good_ = symbols_.intern("good");
    sym_building_type_ = symbols_.intern("building_type");
    sym_production_method_ = symbols_.intern("production_method");
    sym_need_profile_ = symbols_.intern("need_profile");
    sym_base_price_milli_ = symbols_.intern("base_price_milli");
    sym_workers_per_level_ = symbols_.intern("workers_per_level");
    sym_throughput_ppm_ = symbols_.intern("throughput_ppm");
    sym_input_ = symbols_.intern("input");
    sym_output_ = symbols_.intern("output");
    sym_need_ = symbols_.intern("need");
    sym_quantity_milli_ = symbols_.intern("quantity_milli");
    sym_category_ = symbols_.intern("category");
    sym_staple_ = symbols_.intern("staple");
    sym_luxury_ = symbols_.intern("luxury");
    sym_military_ = symbols_.intern("military");
    sym_industrial_ = symbols_.intern("industrial");
    sym_currency_ = symbols_.intern("currency");
    sym_map_color_ = symbols_.intern("map_color");
    sym_red_ = symbols_.intern("r");
    sym_green_ = symbols_.intern("g");
    sym_blue_ = symbols_.intern("b");
    sym_alpha_ = symbols_.intern("a");
    sym_building_ = symbols_.intern("building");
    sym_pop_ = symbols_.intern("pop");
    sym_type_ = symbols_.intern("type");
    sym_province_ = symbols_.intern("province");
    sym_state_ = symbols_.intern("state");
    sym_level_ = symbols_.intern("level");
    sym_size_ = symbols_.intern("size");
    sym_employer_ = symbols_.intern("employer");
    sym_literacy_permyriad_ = symbols_.intern("literacy_permyriad");
    sym_qualification_permyriad_ = symbols_.intern("qualification_permyriad");
    sym_wealth_milli_ = symbols_.intern("wealth_milli");
    sym_political_strength_milli_ = symbols_.intern("political_strength_milli");
    sym_visual_ = symbols_.intern("visual");
    sym_monetary_metal_ = symbols_.intern("monetary_metal");
    sym_gold_ = symbols_.intern("gold");
    sym_silver_ = symbols_.intern("silver");
    sym_construction_goods_ = symbols_.intern("construction_goods");
    country_lookup_.reserve(512u);
    runtime_country_lookup_.reserve(512u);
}

void DefinitionDatabase::register_schema(GenericDefinitionSchema schema) {
    // Deterministic: sorted by category key, duplicate category replaces.
    auto it = std::find_if(generic_schemas_.begin(), generic_schemas_.end(),
        [&](const GenericDefinitionSchema& s){ return s.category == schema.category; });
    if (it != generic_schemas_.end()) { *it = std::move(schema); return; }
    generic_schemas_.push_back(std::move(schema));
    std::sort(generic_schemas_.begin(), generic_schemas_.end(),
        [](const GenericDefinitionSchema& a, const GenericDefinitionSchema& b){ return a.category < b.category; });
}

bool DefinitionDatabase::has_schema(std::string_view category) const noexcept {
    return std::any_of(generic_schemas_.begin(), generic_schemas_.end(),
        [&](const GenericDefinitionSchema& s){ return s.category == category; });
}

bool DefinitionDatabase::ingest(const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics) {
    bool ok = parsed.ok();
    for (const auto& d : parsed.diagnostics) diagnostics.push_back({d.message, d.line});
    // Generic schemas run first so domain errors are reported before hard-coded country path.
    for (auto& schema : generic_schemas_) {
        if (schema.ingest) ok &= schema.ingest(parsed, diagnostics);
    }

    auto integer = [&](const ScriptNode& node, std::int64_t minimum, std::int64_t maximum,
                       std::string_view label, std::int64_t& output) {
        if (node.kind != ScriptValueKind::Number || !std::isfinite(node.number) ||
            std::trunc(node.number) != node.number || node.number < static_cast<double>(minimum) ||
            node.number > static_cast<double>(maximum)) {
            diagnostics.push_back({std::string{label} + " must be an integer in range", node.line});
            ok = false;
            return false;
        }
        output = static_cast<std::int64_t>(node.number);
        return true;
    };
    auto parse_flow = [&](const ScriptNode& node, EconomyFlowSpec& flow) {
        if (node.kind != ScriptValueKind::Block) {
            diagnostics.push_back({"economy flow must be a block", node.line});
            ok = false;
            return false;
        }
        bool has_good = false;
        bool has_quantity = false;
        for (const auto& field : node.children) {
            if (field.key == sym_good_) {
                if (field.kind != ScriptValueKind::Symbol) {
                    diagnostics.push_back({"economy flow good must be a symbol", field.line});
                    ok = false;
                } else {
                    flow.good = field.symbol;
                    has_good = true;
                }
            } else if (field.key == sym_quantity_milli_) {
                std::int64_t value = 0;
                if (integer(field, 1, 9'000'000'000'000'000ll,
                            "economy flow quantity_milli", value)) {
                    flow.quantity_milli = value;
                    has_quantity = true;
                }
            } else {
                diagnostics.push_back({"unknown economy flow field: " + std::string{symbols_.text(field.key)}, field.line});
                ok = false;
            }
        }
        if (!has_good || !has_quantity) {
            diagnostics.push_back({"economy flow requires good and quantity_milli", node.line});
            ok = false;
            return false;
        }
        return true;
    };
    auto upsert = [](auto& definitions, auto definition) {
        const auto found = std::find_if(definitions.begin(), definitions.end(),
            [&](const auto& existing) { return existing.key == definition.key; });
        if (found == definitions.end()) definitions.push_back(std::move(definition));
        else *found = std::move(definition);
    };

    for (const auto& object : parsed.objects) {
        if (object.type == sym_good_) {
            GoodContentDefinition definition;
            definition.key = object.name;
            for (const auto& field : object.fields) {
                if (field.key == sym_base_price_milli_) {
                    std::int64_t value = 0;
                    if (integer(field, 1, 9'000'000'000'000'000ll,
                                "good base_price_milli", value)) definition.base_price_milli = value;
                } else if (field.key == sym_category_) {
                    if (field.kind != ScriptValueKind::Symbol) {
                        diagnostics.push_back({"good category must be a symbol", field.line});
                        ok = false;
                    } else if (field.symbol != sym_staple_ && field.symbol != sym_luxury_ &&
                               field.symbol != sym_military_ && field.symbol != sym_industrial_ &&
                               field.symbol != sym_currency_) {
                        diagnostics.push_back({"unknown good category: " +
                                               std::string{symbols_.text(field.symbol)}, field.line});
                        ok = false;
                    } else {
                        definition.category = field.symbol;
                    }
                } else if (field.key == sym_monetary_metal_) {
                    if (field.kind != ScriptValueKind::Symbol) {
                        diagnostics.push_back({"good monetary_metal must be a symbol", field.line});
                        ok = false;
                    } else if (field.symbol != sym_gold_ && field.symbol != sym_silver_) {
                        diagnostics.push_back({"unknown monetary_metal (expected gold or silver): " +
                                               std::string{symbols_.text(field.symbol)}, field.line});
                        ok = false;
                    } else {
                        definition.monetary_metal = field.symbol;
                    }
                } else {
                    diagnostics.push_back({"unknown good field: " + std::string{symbols_.text(field.key)}, field.line});
                    ok = false;
                }
            }
            upsert(goods_, std::move(definition));
        } else if (object.type == sym_building_type_) {
            BuildingTypeContentDefinition definition;
            definition.key = object.name;
            for (const auto& field : object.fields) {
                if (field.key == sym_workers_per_level_) {
                    std::int64_t value = 0;
                    if (integer(field, 1, std::numeric_limits<std::uint32_t>::max(),
                                "building_type workers_per_level", value)) {
                        definition.workers_per_level = static_cast<std::uint32_t>(value);
                    }
                } else if (field.key == sym_visual_) {
                    if (field.kind != ScriptValueKind::Symbol) {
                        diagnostics.push_back({"building_type visual must be a symbol", field.line});
                        ok = false;
                    } else {
                        definition.visual = field.symbol;
                    }
                } else if (field.key == sym_input_ || field.key == sym_output_) {
                    EconomyFlowSpec flow;
                    if (parse_flow(field, flow)) {
                        (field.key == sym_input_ ? definition.inputs : definition.outputs).push_back(flow);
                    }
                } else {
                    diagnostics.push_back({"unknown building_type field: " + std::string{symbols_.text(field.key)}, field.line});
                    ok = false;
                }
            }
            upsert(building_types_, std::move(definition));
        } else if (object.type == sym_production_method_) {
            ProductionMethodContentDefinition definition;
            definition.key = object.name;
            bool has_building_type = false;
            for (const auto& field : object.fields) {
                if (field.key == sym_building_type_) {
                    if (field.kind != ScriptValueKind::Symbol) {
                        diagnostics.push_back({"production_method building_type must be a symbol", field.line});
                        ok = false;
                    } else {
                        definition.building_type = field.symbol;
                        has_building_type = true;
                    }
                } else if (field.key == sym_throughput_ppm_) {
                    std::int64_t value = 0;
                    if (integer(field, 1, 4'000'000, "production_method throughput_ppm", value)) {
                        definition.throughput_ppm = static_cast<std::int32_t>(value);
                    }
                } else if (field.key == sym_input_ || field.key == sym_output_) {
                    EconomyFlowSpec flow;
                    if (parse_flow(field, flow)) {
                        (field.key == sym_input_ ? definition.inputs : definition.outputs).push_back(flow);
                    }
                } else {
                    diagnostics.push_back({"unknown production_method field: " + std::string{symbols_.text(field.key)}, field.line});
                    ok = false;
                }
            }
            if (!has_building_type) {
                diagnostics.push_back({"production_method requires building_type", object.line});
                ok = false;
            }
            upsert(production_methods_, std::move(definition));
        } else if (object.type == sym_need_profile_) {
            NeedProfileContentDefinition definition;
            definition.key = object.name;
            for (const auto& field : object.fields) {
                if (field.key == sym_need_) {
                    EconomyFlowSpec flow;
                    if (parse_flow(field, flow)) definition.needs.push_back(flow);
                } else {
                    diagnostics.push_back({"unknown need_profile field: " + std::string{symbols_.text(field.key)}, field.line});
                    ok = false;
                }
            }
            upsert(need_profiles_, std::move(definition));
        } else if (object.type == sym_building_) {
            BuildingContentDefinition definition;
            definition.key = object.name;
            bool has_type = false;
            bool has_province = false;
            bool has_state = false;
            for (const auto& field : object.fields) {
                if (field.key == sym_type_) {
                    if (field.kind != ScriptValueKind::Symbol) {
                        diagnostics.push_back({"building type must be a symbol", field.line});
                        ok = false;
                    } else {
                        definition.type = field.symbol;
                        has_type = true;
                    }
                } else if (field.key == sym_province_ || field.key == sym_state_) {
                    if (field.kind != ScriptValueKind::Symbol) {
                        diagnostics.push_back({"building province/state must be a symbol", field.line});
                        ok = false;
                    } else if (field.key == sym_province_) {
                        definition.province = field.symbol;
                        has_province = true;
                    } else {
                        definition.state = field.symbol;
                        has_state = true;
                    }
                } else if (field.key == sym_production_method_) {
                    if (field.kind != ScriptValueKind::Symbol) {
                        diagnostics.push_back({"building production_method must be a symbol", field.line});
                        ok = false;
                    } else {
                        definition.production_method = field.symbol;
                    }
                } else if (field.key == sym_level_) {
                    std::int64_t value = 0;
                    if (integer(field, 1, std::numeric_limits<std::uint16_t>::max(),
                                "building level", value))
                        definition.level = static_cast<std::uint16_t>(value);
                } else {
                    diagnostics.push_back({"unknown building field: " +
                                               std::string{symbols_.text(field.key)}, field.line});
                    ok = false;
                }
            }
            if (!has_type || (has_province == has_state)) {
                diagnostics.push_back({"building requires type and exactly one province/state", object.line});
                ok = false;
            }
            upsert(buildings_, std::move(definition));
        } else if (object.type == sym_pop_) {
            PopContentDefinition definition;
            definition.key = object.name;
            bool has_size = false;
            bool has_need_profile = false;
            bool has_province = false;
            bool has_state = false;
            for (const auto& field : object.fields) {
                if (field.key == sym_size_) {
                    std::int64_t value = 0;
                    if (integer(field, 1, std::numeric_limits<PopulationCount>::max(),
                                "pop size", value)) {
                        definition.size = static_cast<PopulationCount>(value);
                        has_size = true;
                    }
                } else if (field.key == sym_need_profile_) {
                    if (field.kind != ScriptValueKind::Symbol) {
                        diagnostics.push_back({"pop need_profile must be a symbol", field.line});
                        ok = false;
                    } else {
                        definition.need_profile = field.symbol;
                        has_need_profile = true;
                    }
                } else if (field.key == sym_province_ || field.key == sym_state_) {
                    if (field.kind != ScriptValueKind::Symbol) {
                        diagnostics.push_back({"pop province/state must be a symbol", field.line});
                        ok = false;
                    } else if (field.key == sym_province_) {
                        definition.province = field.symbol;
                        has_province = true;
                    } else {
                        definition.state = field.symbol;
                        has_state = true;
                    }
                } else if (field.key == sym_employer_) {
                    if (field.kind != ScriptValueKind::Symbol) {
                        diagnostics.push_back({"pop employer must be a symbol", field.line});
                        ok = false;
                    } else {
                        definition.employer = field.symbol;
                    }
                } else if (field.key == sym_literacy_permyriad_ ||
                           field.key == sym_qualification_permyriad_ ||
                           field.key == sym_political_strength_milli_) {
                    std::int64_t value = 0;
                    const auto label = field.key == sym_literacy_permyriad_
                        ? "pop literacy_permyriad"
                        : field.key == sym_qualification_permyriad_
                            ? "pop qualification_permyriad"
                            : "pop political_strength_milli";
                    const auto maximum = field.key == sym_political_strength_milli_
                        ? static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())
                        : 10'000;
                    if (integer(field, 0, maximum, label, value)) {
                        if (field.key == sym_literacy_permyriad_)
                            definition.literacy_permyriad = static_cast<std::uint16_t>(value);
                        else if (field.key == sym_qualification_permyriad_)
                            definition.qualification_permyriad = static_cast<std::uint16_t>(value);
                        else
                            definition.political_strength_milli = static_cast<std::uint32_t>(value);
                    }
                } else if (field.key == sym_wealth_milli_) {
                    std::int64_t value = 0;
                    if (integer(field, std::numeric_limits<std::int32_t>::min(),
                                std::numeric_limits<std::int32_t>::max(), "pop wealth_milli", value))
                        definition.wealth_milli = static_cast<std::int32_t>(value);
                } else {
                    diagnostics.push_back({"unknown pop field: " +
                                               std::string{symbols_.text(field.key)}, field.line});
                    ok = false;
                }
            }
            if (!has_size || !has_need_profile || (has_province == has_state)) {
                diagnostics.push_back({"pop requires size, need_profile and exactly one province/state", object.line});
                ok = false;
            }
            upsert(pops_, std::move(definition));
        } else if (object.type == sym_construction_goods_) {
            // Construction goods basket: anonymous block, `input = { good quantity_milli }`
            // rows accumulate across files (mods layer additional materials).
            for (const auto& field : object.fields) {
                if (field.key == sym_input_) {
                    EconomyFlowSpec flow;
                    if (parse_flow(field, flow)) construction_goods_inputs_.push_back(flow);
                } else {
                    diagnostics.push_back({"unknown construction_goods field: " +
                                           std::string{symbols_.text(field.key)}, field.line});
                    ok = false;
                }
            }
        }
    }

    for (const auto& object : parsed.objects) {
        if (object.type != sym_country_) continue;
        CountryDefinition def;
        def.tag = object.name;
        bool object_ok = true;
        for (const auto& field : object.fields) {
            if (field.key == sym_population_) {
                if (field.kind != ScriptValueKind::Number) { diagnostics.push_back({"country population must be numeric", field.line}); object_ok = false; }
                else def.population = field.number;
            } else if (field.key == sym_gdp_) {
                if (field.kind != ScriptValueKind::Number) { diagnostics.push_back({"country gdp must be numeric", field.line}); object_ok = false; }
                else def.gdp = field.number;
            } else if (field.key == sym_treasury_) {
                if (field.kind != ScriptValueKind::Number) { diagnostics.push_back({"country treasury must be numeric", field.line}); object_ok = false; }
                else def.treasury = field.number;
            } else if (field.key == sym_tax_rate_) {
                if (field.kind != ScriptValueKind::Number) { diagnostics.push_back({"country tax_rate must be numeric", field.line}); object_ok = false; }
                else def.tax_rate = field.number;
            } else if (field.key == sym_map_color_) {
                if (field.kind != ScriptValueKind::Block) {
                    diagnostics.push_back({"country map_color must be a block", field.line});
                    object_ok = false;
                } else {
                    bool seen[4] = {false, false, false, false};
                    for (const auto& channel : field.children) {
                        int slot = -1;
                        if (channel.key == sym_red_) slot = 0;
                        else if (channel.key == sym_green_) slot = 1;
                        else if (channel.key == sym_blue_) slot = 2;
                        else if (channel.key == sym_alpha_) slot = 3;
                        else {
                            diagnostics.push_back({"unknown map_color channel: " +
                                                       std::string{symbols_.text(channel.key)}, channel.line});
                            object_ok = false;
                            continue;
                        }
                        std::int64_t value = 0;
                        if (seen[slot] || !integer(channel, 0, 255, "map_color channel", value)) {
                            object_ok = false;
                            continue;
                        }
                        seen[slot] = true;
                        def.map_color[static_cast<std::size_t>(slot)] = static_cast<std::uint8_t>(value);
                    }
                    for (const bool channel : seen) {
                        if (!channel) {
                            diagnostics.push_back({"map_color requires r, g, b and a", field.line});
                            object_ok = false;
                            break;
                        }
                    }
                }
            } else {
                // Every other definition type rejects unknown fields; country
                // silently ignored them, so a typo like `treasur = 100` produced
                // a country with default values and no diagnostic at all.
                diagnostics.push_back({"unknown country field: " + std::string{symbols_.text(field.key)}, field.line});
                object_ok = false;
            }
        }
        if (!object_ok) { ok = false; continue; }
        const auto found = country_lookup_.find(def.tag.value());
        if (found == country_lookup_.end()) {
            const auto index = static_cast<std::uint32_t>(countries_.size());
            country_lookup_.emplace(def.tag.value(), index);
            countries_.push_back(def);
        } else {
            // Later parsed files replace earlier definitions. VFS already resolves same-path
            // overlays; this additionally makes separate-file mod replacement deterministic.
            countries_[found->second] = def;
        }
    }
    ok &= research_content_.ingest(parsed, diagnostics);
    ok &= notification_content_.ingest(parsed, diagnostics);
    ok &= on_action_content_.ingest(parsed, diagnostics);
    return ok && diagnostics.empty();
}

bool DefinitionDatabase::compile_scripts(const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics) {
    return compiler_.compile(parsed, scripts_, diagnostics);
}

bool DefinitionDatabase::ingest_gameplay(const ScriptParseResult& parsed, std::vector<ScriptCompileDiagnostic>& diagnostics) {
    return gameplay_content_.ingest(parsed, diagnostics);
}

bool DefinitionDatabase::bind_gameplay(ScriptedGameplayRuntime& gameplay, UtilityAiEngine& ai,
                                        std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    return gameplay_content_.bind(scripts_, gameplay, ai, diagnostics);
}

bool DefinitionDatabase::bind_research(ResearchSystem& research,
                                        std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    return research_content_.bind(scripts_, research, diagnostics);
}

bool DefinitionDatabase::bind_notifications(
    NotificationRuntime& notifications,
    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    return notification_content_.bind(scripts_, notifications, diagnostics);
}

bool DefinitionDatabase::bind_on_actions(
    const ScriptedGameplayRuntime& gameplay, OnActionRuntime& on_actions,
    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    return on_action_content_.bind(scripts_, gameplay, on_actions, diagnostics);
}

bool DefinitionDatabase::bind_economy(
    EconomyDefinitions& economy,
    std::vector<ScriptCompileDiagnostic>& diagnostics) const {
    EconomyDefinitions staging;
    std::unordered_map<std::uint32_t, GoodId> good_ids;
    std::unordered_map<std::uint32_t, BuildingTypeId> building_type_ids;
    good_ids.reserve(goods_.size());
    building_type_ids.reserve(building_types_.size());

    try {
        for (const auto& definition : goods_) {
            GoodCategory category = GoodCategory::Staple;
            if (definition.category == sym_luxury_) category = GoodCategory::Luxury;
            else if (definition.category == sym_military_) category = GoodCategory::Military;
            else if (definition.category == sym_industrial_) category = GoodCategory::Industrial;
            else if (definition.category == sym_currency_) category = GoodCategory::Currency;
            CurrencyKey monetary_metal_key = 0;
            if (definition.monetary_metal == sym_gold_) monetary_metal_key = metal_gold_key;
            else if (definition.monetary_metal == sym_silver_) monetary_metal_key = metal_silver_key;
            if (monetary_metal_key != 0 && category != GoodCategory::Currency) {
                diagnostics.push_back({"good with monetary_metal must have category currency: " +
                                       std::string{symbols_.text(definition.key)}, 0});
                return false;
            }
            const auto id = staging.add_good({std::string{symbols_.text(definition.key)},
                                              definition.base_price_milli, category, monetary_metal_key});
            good_ids.emplace(definition.key.value(), id);
        }

        auto recipe_flows = [&](std::span<const EconomyFlowSpec> specs,
                                std::vector<RecipeFlow>& output,
                                std::string_view owner) {
            output.clear();
            output.reserve(specs.size());
            for (const auto& spec : specs) {
                const auto found = good_ids.find(spec.good.value());
                if (found == good_ids.end()) {
                    diagnostics.push_back({std::string{owner} + " references unknown good: " +
                                           std::string{symbols_.text(spec.good)}, 0});
                    return false;
                }
                output.push_back({found->second, spec.quantity_milli});
            }
            return true;
        };

        std::vector<RecipeFlow> inputs;
        std::vector<RecipeFlow> outputs;
        for (const auto& definition : building_types_) {
            const auto key = std::string{symbols_.text(definition.key)};
            if (!recipe_flows(definition.inputs, inputs, key) ||
                !recipe_flows(definition.outputs, outputs, key)) return false;
            const auto id = staging.add_building_type(key, definition.workers_per_level, inputs, outputs);
            building_type_ids.emplace(definition.key.value(), id);
        }

        for (const auto& definition : production_methods_) {
            const auto building = building_type_ids.find(definition.building_type.value());
            if (building == building_type_ids.end()) {
                diagnostics.push_back({"production_method references unknown building_type: " +
                                       std::string{symbols_.text(definition.building_type)}, 0});
                return false;
            }
            const auto key = std::string{symbols_.text(definition.key)};
            if (!recipe_flows(definition.inputs, inputs, key) ||
                !recipe_flows(definition.outputs, outputs, key)) return false;
            (void)staging.add_production_method(key, building->second,
                                                definition.throughput_ppm, inputs, outputs);
        }

        std::vector<NeedFlow> needs;
        for (const auto& definition : need_profiles_) {
            needs.clear();
            needs.reserve(definition.needs.size());
            for (const auto& spec : definition.needs) {
                const auto found = good_ids.find(spec.good.value());
                if (found == good_ids.end()) {
                    diagnostics.push_back({"need_profile references unknown good: " +
                                           std::string{symbols_.text(spec.good)}, 0});
                    return false;
                }
                needs.push_back({found->second, spec.quantity_milli});
            }
            (void)staging.add_need_profile(std::string{symbols_.text(definition.key)}, needs);
        }

        std::vector<RecipeFlow> construction_basket;
        if (!recipe_flows(construction_goods_inputs_, construction_basket, "construction_goods"))
            return false;
        staging.set_construction_inputs(construction_basket);
    } catch (const std::exception& error) {
        diagnostics.push_back({std::string{"economy content bind failed: "} + error.what(), 0});
        return false;
    }

    economy = std::move(staging);
    return true;
}

void DefinitionDatabase::instantiate_world(World& world) {
    runtime_country_lookup_.clear();
    world.countries.reserve(world.countries.size() + countries_.size());
    for (const auto& def : countries_) {
        const auto id = world.countries.create({std::string{symbols_.text(def.tag)}, def.population, def.gdp, def.treasury, def.tax_rate});
        runtime_country_lookup_.emplace(def.tag.value(), id);
    }
}

void DefinitionDatabase::apply_history(std::int32_t yyyymmdd, World& world) {
    ScriptVm vm{registry_};
    for (const auto& patch : scripts_.history()) {
        if (patch.yyyymmdd > yyyymmdd) continue;
        const auto id = runtime_country(patch.target);
        if (!id.valid()) continue;
        vm.apply(patch.effects, world, ScopeRef::country(id));
    }
}

const CountryDefinition* DefinitionDatabase::find_country(SymbolId tag) const noexcept {
    const auto it = country_lookup_.find(tag.value());
    return it == country_lookup_.end() ? nullptr : &countries_[it->second];
}

bool DefinitionDatabase::country_map_color(std::string_view tag,
                                            std::array<std::uint8_t, 4>& out) const noexcept {
    const auto symbol = symbols_.find(tag);
    const auto* country = find_country(symbol);
    if (country == nullptr) return false;
    out = country->map_color;
    return true;
}

CountryId DefinitionDatabase::runtime_country(SymbolId tag) const noexcept {
    const auto it = runtime_country_lookup_.find(tag.value());
    return it == runtime_country_lookup_.end() ? CountryId{} : it->second;
}

std::size_t DefinitionDatabase::immutable_bytes() const noexcept {
    std::size_t generic = 0;
    for (auto& s : generic_schemas_) if (s.immutable_bytes) generic += s.immutable_bytes();
    std::size_t economy_bytes = goods_.capacity() * sizeof(GoodContentDefinition) +
        building_types_.capacity() * sizeof(BuildingTypeContentDefinition) +
        production_methods_.capacity() * sizeof(ProductionMethodContentDefinition) +
        need_profiles_.capacity() * sizeof(NeedProfileContentDefinition);
    for (const auto& definition : building_types_)
        economy_bytes += (definition.inputs.capacity() + definition.outputs.capacity()) * sizeof(EconomyFlowSpec);
    for (const auto& definition : production_methods_)
        economy_bytes += (definition.inputs.capacity() + definition.outputs.capacity()) * sizeof(EconomyFlowSpec);
    for (const auto& definition : need_profiles_)
        economy_bytes += definition.needs.capacity() * sizeof(EconomyFlowSpec);
    return countries_.capacity() * sizeof(CountryDefinition) +
           buildings_.capacity() * sizeof(BuildingContentDefinition) +
           pops_.capacity() * sizeof(PopContentDefinition) + economy_bytes + scripts_.instruction_bytes() +
           localization_.memory_bytes() + gameplay_content_.memory_bytes() +
           research_content_.memory_bytes() + notification_content_.memory_bytes() +
           on_action_content_.memory_bytes() + generic +
           symbols_.memory_bytes();
}

} // namespace thunder
