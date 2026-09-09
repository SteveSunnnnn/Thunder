#pragma once

#include "thunder/presentation/render/PhysicalLighting.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace thunder {

enum class ParticleVfxKind : std::uint8_t {
    FactoryChimneySmoke = 0,    // Dark coal/soot smoke billowing upward from industrial centers
    ArtilleryMuzzleFlash = 1,   // Bright fiery orange flash for frontline combat
    BattlefieldGunpowder = 2,   // Lingering white-gray gunpowder haze on active warfronts
    SteamTrainPlume = 3,        // Periodic steam puffs along 3D railway tracks
    NavalWakeFoam = 4           // White water foam ribbon trailing moving ships
};

struct Particle3D {
    Vec3 position{};
    Vec3 velocity{};
    Vec3 color{1.0f, 1.0f, 1.0f};
    float size = 1.0f;
    float growth_rate = 1.0f;
    float alpha = 1.0f;
    float age_seconds = 0.0f;
    float max_lifetime_seconds = 2.0f;
    float rotation_rad = 0.0f;
    float rotation_speed = 0.0f;
    ParticleVfxKind kind = ParticleVfxKind::FactoryChimneySmoke;
};

struct GpuParticleVertex {
    Vec3 position;
    float size;
    Vec3 color;
    float alpha;
    float rotation;
    std::uint32_t kind;
};

class LivingMapVfx3D {
public:
    LivingMapVfx3D() = default;

    // Factory chimney smoke generator (buoyant thermal lift + wind drift)
    void spawn_factory_smoke(const Vec3& chimney_pos, float intensity = 1.0f, const Vec3& wind = {5.0f, 2.0f, 0.0f});

    // Battlefield combat effects (artillery flash + expanding smoke plume)
    void spawn_artillery_explosion(const Vec3& ground_pos, float caliber_scale = 1.0f);

    // Locomotive steam puff along railway track
    void spawn_train_steam(const Vec3& train_pos, const Vec3& train_forward, float speed = 15.0f);

    // Naval wake trail on water surface
    void spawn_ship_wake(const Vec3& ship_stern_pos, const Vec3& ship_forward, float speed = 10.0f);

    // Updates physical simulation of all active 3D particles (buoyancy, drag, wind, turbulence)
    void update(float dt_seconds, const Vec3& global_wind = {3.0f, 1.0f, 0.0f}) noexcept;

    // Packs active particles into GPU vertex buffer for instanced billboard rendering
    [[nodiscard]] std::size_t generate_gpu_vertices(std::span<GpuParticleVertex> out_vertices) const noexcept;

    [[nodiscard]] std::size_t active_particle_count() const noexcept { return particles_.size(); }
    void clear() noexcept { particles_.clear(); }

private:
    std::vector<Particle3D> particles_;
};

} // namespace thunder
