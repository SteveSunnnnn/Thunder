#pragma once

#include "thunder/foundation/base/StrongId.hpp"
#include "thunder/scripting/ScriptProgram.hpp"
#include "thunder/scripting/Scope.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace thunder {

class ScriptRegistry;
class World;

using NotificationInstanceId = StrongId<struct NotificationInstanceTag, std::uint64_t>;

enum class NotificationPriority : std::uint8_t { Low, Normal, High, Critical };
enum class NotificationDedupePolicy : std::uint8_t { Stack, Suppress, Replace };
enum class NotificationState : std::uint8_t { Unread, Read, Actioned, Dismissed, Expired };

struct NotificationActionDefinition {
    std::string key;
    std::string label_key;
    std::optional<ScriptProgram> allow;
    std::optional<ScriptProgram> effect;
};

struct NotificationDefinition {
    std::string key;
    ScopeType scope = ScopeType::None;
    std::string title_key;
    std::string body_key;
    std::string icon_key;
    std::string category_key;
    NotificationPriority priority = NotificationPriority::Normal;
    NotificationDedupePolicy dedupe = NotificationDedupePolicy::Stack;
    std::uint64_t lifetime_ticks = 0u;
    std::optional<ScriptProgram> potential;
    std::vector<NotificationActionDefinition> actions;
};

struct NotificationInstance {
    static constexpr std::uint32_t no_action = std::numeric_limits<std::uint32_t>::max();

    NotificationInstanceId id{};
    std::uint32_t definition = 0u;
    ScopeRef scope{};
    ScopeRef source{};
    ScopeRef map_target{};
    std::uint64_t created_tick = 0u;
    std::uint64_t updated_tick = 0u;
    std::uint64_t expires_tick = 0u;
    std::uint64_t dedupe_key = 0u;
    std::uint32_t occurrence_count = 1u;
    std::uint32_t chosen_action = no_action;
    NotificationState state = NotificationState::Unread;
};

class NotificationRuntime {
public:
    explicit NotificationRuntime(const ScriptRegistry& registry,
                                 const ScriptProgramDatabase* programs = nullptr);

    void set_program_database(const ScriptProgramDatabase* programs) noexcept;
    void clear_content();
    void clear_state() noexcept;
    std::uint32_t add_definition(NotificationDefinition definition);

    [[nodiscard]] NotificationInstanceId emit(std::uint32_t definition, const World& world,
                                              ScopeRef scope, std::uint64_t tick,
                                              ScopeRef source = {}, ScopeRef map_target = {},
                                              std::uint64_t dedupe_key = 0u);
    bool mark_read(NotificationInstanceId id, std::uint64_t tick);
    bool dismiss(NotificationInstanceId id, std::uint64_t tick);
    bool choose_action(NotificationInstanceId id, std::uint32_t action, World& world,
                       std::uint64_t tick);
    std::size_t update(std::uint64_t tick) noexcept;

    [[nodiscard]] const NotificationInstance* find_instance(NotificationInstanceId id) const noexcept;
    [[nodiscard]] std::span<const NotificationDefinition> definitions() const noexcept { return definitions_; }
    [[nodiscard]] std::span<const NotificationInstance> instances() const noexcept { return instances_; }
    [[nodiscard]] std::uint64_t next_instance_id() const noexcept { return next_instance_id_; }
    [[nodiscard]] std::size_t unread_count() const noexcept;

    void validate_state(std::span<const NotificationInstance> instances,
                        std::uint64_t next_instance_id, const World& world,
                        std::uint64_t current_tick) const;
    void restore_state(std::vector<NotificationInstance> instances,
                       std::uint64_t next_instance_id);
    [[nodiscard]] std::uint64_t checksum_state(std::span<const NotificationInstance> instances,
                                               std::uint64_t next_instance_id) const noexcept;
    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    static constexpr std::size_t max_instances = 1'000'000u;
    [[nodiscard]] bool active(NotificationState state) const noexcept;
    [[nodiscard]] bool optional_scope_valid(const World& world, ScopeRef scope) const noexcept;
    [[nodiscard]] NotificationInstance* mutable_instance(NotificationInstanceId id) noexcept;

    ScriptVm vm_;
    std::vector<NotificationDefinition> definitions_;
    std::vector<NotificationInstance> instances_;
    std::uint64_t next_instance_id_ = 1u;
};

} // namespace thunder
