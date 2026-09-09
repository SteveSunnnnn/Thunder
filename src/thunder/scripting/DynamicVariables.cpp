#include "thunder/scripting/DynamicVariables.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace thunder {

namespace {

struct Fnv1a64 {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    void add(std::uint64_t v) noexcept {
        h ^= v;
        h *= 0x100000001b3ULL;
    }
    void add_bytes(const void* data, std::size_t len) noexcept {
        const auto* ptr = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < len; ++i) {
            h ^= static_cast<std::uint64_t>(ptr[i]);
            h *= 0x100000001b3ULL;
        }
    }
    [[nodiscard]] std::uint64_t value() const noexcept { return h; }
};

} // namespace

std::uint64_t DynamicVariableMap::hash_key(std::string_view key) noexcept {
    Fnv1a64 h;
    h.add_bytes(key.data(), key.size());
    return h.value();
}

void DynamicVariableMap::set_int(std::uint64_t key_hash, std::int64_t val) noexcept {
    for (auto& v : vars_) {
        if (v.key_hash == key_hash) {
            v.type = DynamicVariableType::Int;
            v.value.int_val = val;
            return;
        }
    }
    DynamicVariable var{};
    var.key_hash = key_hash;
    var.type = DynamicVariableType::Int;
    var.value.int_val = val;
    vars_.push_back(var);
}

void DynamicVariableMap::set_double(std::uint64_t key_hash, double val) noexcept {
    for (auto& v : vars_) {
        if (v.key_hash == key_hash) {
            v.type = DynamicVariableType::Double;
            v.value.double_val = val;
            return;
        }
    }
    DynamicVariable var{};
    var.key_hash = key_hash;
    var.type = DynamicVariableType::Double;
    var.value.double_val = val;
    vars_.push_back(var);
}

void DynamicVariableMap::set_bool(std::uint64_t key_hash, bool val) noexcept {
    for (auto& v : vars_) {
        if (v.key_hash == key_hash) {
            v.type = DynamicVariableType::Bool;
            v.value.bool_val = val;
            return;
        }
    }
    DynamicVariable var{};
    var.key_hash = key_hash;
    var.type = DynamicVariableType::Bool;
    var.value.bool_val = val;
    vars_.push_back(var);
}

void DynamicVariableMap::set_id(std::uint64_t key_hash, std::uint64_t id_val) noexcept {
    for (auto& v : vars_) {
        if (v.key_hash == key_hash) {
            v.type = DynamicVariableType::EntityId;
            v.value.id_val = id_val;
            return;
        }
    }
    DynamicVariable var{};
    var.key_hash = key_hash;
    var.type = DynamicVariableType::EntityId;
    var.value.id_val = id_val;
    vars_.push_back(var);
}

void DynamicVariableMap::set_string_hash(std::uint64_t key_hash, std::uint64_t str_hash) noexcept {
    for (auto& v : vars_) {
        if (v.key_hash == key_hash) {
            v.type = DynamicVariableType::StringHash;
            v.value.str_hash_val = str_hash;
            return;
        }
    }
    DynamicVariable var{};
    var.key_hash = key_hash;
    var.type = DynamicVariableType::StringHash;
    var.value.str_hash_val = str_hash;
    vars_.push_back(var);
}

std::optional<std::int64_t> DynamicVariableMap::get_int(std::uint64_t key_hash) const noexcept {
    for (const auto& v : vars_) {
        if (v.key_hash == key_hash && v.type == DynamicVariableType::Int) return v.value.int_val;
    }
    return std::nullopt;
}

std::optional<double> DynamicVariableMap::get_double(std::uint64_t key_hash) const noexcept {
    for (const auto& v : vars_) {
        if (v.key_hash == key_hash && v.type == DynamicVariableType::Double) return v.value.double_val;
    }
    return std::nullopt;
}

std::optional<bool> DynamicVariableMap::get_bool(std::uint64_t key_hash) const noexcept {
    for (const auto& v : vars_) {
        if (v.key_hash == key_hash && v.type == DynamicVariableType::Bool) return v.value.bool_val;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> DynamicVariableMap::get_id(std::uint64_t key_hash) const noexcept {
    for (const auto& v : vars_) {
        if (v.key_hash == key_hash && v.type == DynamicVariableType::EntityId) return v.value.id_val;
    }
    return std::nullopt;
}

std::optional<std::uint64_t> DynamicVariableMap::get_string_hash(std::uint64_t key_hash) const noexcept {
    for (const auto& v : vars_) {
        if (v.key_hash == key_hash && v.type == DynamicVariableType::StringHash) return v.value.str_hash_val;
    }
    return std::nullopt;
}

bool DynamicVariableMap::has(std::uint64_t key_hash) const noexcept {
    for (const auto& v : vars_) {
        if (v.key_hash == key_hash) return true;
    }
    return false;
}

bool DynamicVariableMap::remove(std::uint64_t key_hash) noexcept {
    for (auto it = vars_.begin(); it != vars_.end(); ++it) {
        if (it->key_hash == key_hash) {
            vars_.erase(it);
            return true;
        }
    }
    return false;
}

std::uint64_t DynamicVariableMap::checksum() const noexcept {
    Fnv1a64 h;
    h.add(vars_.size());
    for (const auto& v : vars_) {
        h.add(v.key_hash);
        h.add(static_cast<std::uint8_t>(v.type));
        // Read the union member that is actually active. Reading id_val for a
        // Bool is undefined behaviour, and it also bypassed Fnv1a64's
        // -0.0/NaN normalisation for Double, so equal worlds could hash
        // differently depending on FPU state.
        switch (v.type) {
            case DynamicVariableType::Int: {
                const std::int64_t value = v.value.int_val;
                h.add(value);
                break;
            }
            case DynamicVariableType::Double: {
                // Canonicalise NaN and -0.0 exactly as thunder::Fnv1a64 does, so
                // equal worlds hash equally regardless of FPU NaN payloads.
                constexpr std::uint64_t canonical_nan = 0x7ff8000000000000ULL;
                const double value = v.value.double_val;
                if (std::isnan(value)) {
                    h.add(canonical_nan);
                } else {
                    const double normalized = (value == 0.0) ? 0.0 : value;
                    h.add_bytes(&normalized, sizeof(normalized));
                }
                break;
            }
            case DynamicVariableType::Bool: {
                const unsigned value = v.value.bool_val ? 1u : 0u;
                h.add(value);
                break;
            }
            case DynamicVariableType::EntityId: {
                const std::uint64_t value = v.value.id_val;
                h.add(value);
                break;
            }
            case DynamicVariableType::StringHash: {
                const std::uint64_t value = v.value.str_hash_val;
                h.add(value);
                break;
            }
        }
    }
    return h.value();
}

} // namespace thunder
