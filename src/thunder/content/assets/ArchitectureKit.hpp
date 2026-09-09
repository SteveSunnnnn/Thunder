#pragma once
#include "thunder/foundation/base/Hash.hpp"
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>
namespace thunder {
enum class ArchitectureKind:std::uint8_t{Residential,Commercial,Factory,Farm,Mine,Port};
struct ArchitectureVariant{
    ArchitectureKind kind=ArchitectureKind::Residential;
    std::uint16_t earliest_year=0;
    std::uint16_t latest_year=65535;
    std::int32_t min_wealth_milli=0;
    std::int32_t max_wealth_milli=100'000;
    std::uint32_t weight=1;
    std::uint64_t mesh_hash=0;
    std::uint64_t material_hash=0;
    std::uint8_t max_lod=3;
    friend bool operator==(const ArchitectureVariant&,const ArchitectureVariant&)=default;
};
class ArchitectureKit{
public:
    void add(ArchitectureVariant v);
    [[nodiscard]] const ArchitectureVariant* select(ArchitectureKind kind,std::uint16_t year,std::int32_t wealth_milli,std::uint64_t stable_seed)const noexcept;
    [[nodiscard]]std::span<const ArchitectureVariant>variants()const noexcept{return variants_;}
    [[nodiscard]]std::uint64_t checksum()const noexcept;
    void write(const std::filesystem::path& path) const;
    [[nodiscard]] static ArchitectureKit read(const std::filesystem::path& path);
private:
    std::vector<ArchitectureVariant>variants_;
};
} // namespace thunder
