#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace thunder {

enum class DynamicVariableType : std::uint8_t {
    Int = 0,
    Double = 1,
    Bool = 2,
    EntityId = 3,
    StringHash = 4
};

struct DynamicVariable {
    std::uint64_t key_hash = 0;
    DynamicVariableType type = DynamicVariableType::Int;
    union ValueUnion {
        std::int64_t int_val;
        double double_val;
        bool bool_val;
        std::uint64_t id_val;
        std::uint64_t str_hash_val;
    } value{};
};

class DynamicVariableMap {
public:
    static std::uint64_t hash_key(std::string_view key) noexcept;

    void set_int(std::uint64_t key_hash, std::int64_t val) noexcept;
    void set_double(std::uint64_t key_hash, double val) noexcept;
    void set_bool(std::uint64_t key_hash, bool val) noexcept;
    void set_id(std::uint64_t key_hash, std::uint64_t id_val) noexcept;
    void set_string_hash(std::uint64_t key_hash, std::uint64_t str_hash) noexcept;

    void set_int(std::string_view key, std::int64_t val) noexcept { set_int(hash_key(key), val); }
    void set_double(std::string_view key, double val) noexcept { set_double(hash_key(key), val); }
    void set_bool(std::string_view key, bool val) noexcept { set_bool(hash_key(key), val); }
    void set_id(std::string_view key, std::uint64_t id_val) noexcept { set_id(hash_key(key), id_val); }
    void set_string_hash(std::string_view key, std::uint64_t str_hash) noexcept { set_string_hash(hash_key(key), str_hash); }

    [[nodiscard]] std::optional<std::int64_t> get_int(std::uint64_t key_hash) const noexcept;
    [[nodiscard]] std::optional<double> get_double(std::uint64_t key_hash) const noexcept;
    [[nodiscard]] std::optional<bool> get_bool(std::uint64_t key_hash) const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> get_id(std::uint64_t key_hash) const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> get_string_hash(std::uint64_t key_hash) const noexcept;

    [[nodiscard]] std::optional<std::int64_t> get_int(std::string_view key) const noexcept { return get_int(hash_key(key)); }
    [[nodiscard]] std::optional<double> get_double(std::string_view key) const noexcept { return get_double(hash_key(key)); }
    [[nodiscard]] std::optional<bool> get_bool(std::string_view key) const noexcept { return get_bool(hash_key(key)); }
    [[nodiscard]] std::optional<std::uint64_t> get_id(std::string_view key) const noexcept { return get_id(hash_key(key)); }
    [[nodiscard]] std::optional<std::uint64_t> get_string_hash(std::string_view key) const noexcept { return get_string_hash(hash_key(key)); }

    [[nodiscard]] bool has(std::uint64_t key_hash) const noexcept;
    [[nodiscard]] bool has(std::string_view key) const noexcept { return has(hash_key(key)); }

    bool remove(std::uint64_t key_hash) noexcept;
    bool remove(std::string_view key) noexcept { return remove(hash_key(key)); }

    void clear() noexcept { vars_.clear(); }
    [[nodiscard]] std::size_t size() const noexcept { return vars_.size(); }
    [[nodiscard]] std::span<const DynamicVariable> variables() const noexcept { return vars_; }

    [[nodiscard]] std::uint64_t checksum() const noexcept;

private:
    std::vector<DynamicVariable> vars_;
};

} // namespace thunder
