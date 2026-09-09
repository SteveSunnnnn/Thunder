#include "thunder/presentation/render/vfx/LivingMapVfx3D.hpp"
#include <cmath>
#include <algorithm>

namespace thunder {

namespace {
float fast_pseudo_rand(std::uint32_t seed) noexcept {
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);
    return static_cast<float>(seed & 0xFFFF) / 65535.0f;
}
} // namespace

void LivingMapVfx3D::spawn_factory_smoke(const Vec3& chimney_pos, float intensity, const Vec3& wind) {
    if (particles_.size() >= 10'000) return;
    const std::uint32_t seed = static_cast<std::uint32_t>(particles_.size() * 1337 + 7);
    const float rx = (fast_pseudo_rand(seed) - 0.5f) * 2.0f;
    const float ry = (fast_pseudo_rand(seed + 1) - 0.5f) * 2.0f;

    Particle3D p;
    p.position = chimney_pos + Vec3{rx * 0.5f, ry * 0.5f, 0.0f};
    p.velocity = Vec3{wind.x * 0.3f + rx * 0.5f, wind.y * 0.3f + ry * 0.5f, 4.0f * intensity};
    p.color = Vec3{0.18f, 0.17f, 0.16f} * (0.8f + fast_pseudo_rand(seed + 2) * 0.4f); // Coal soot gray-black
    p.size = 2.0f * intensity;
    p.growth_rate = 3.5f * intensity;
    p.alpha = 0.85f;
    p.age_seconds = 0.0f;
    p.max_lifetime_seconds = 4.0f + fast_pseudo_rand(seed + 3) * 2.0f;
    p.rotation_rad = fast_pseudo_rand(seed + 4) * 6.28f;
    p.rotation_speed = (fast_pseudo_rand(seed + 5) - 0.5f) * 0.8f;
    p.kind = ParticleVfxKind::FactoryChimneySmoke;
    particles_.push_back(p);
}

void LivingMapVfx3D::spawn_artillery_explosion(const Vec3& ground_pos, float caliber_scale) {
    if (particles_.size() >= 9'800) return;
    const std::uint32_t seed = static_cast<std::uint32_t>(particles_.size() * 37 + 101);

    // 1. Thunder fiery muzzle flash / blast fireball
    {
        Particle3D flash;
        flash.position = ground_pos + Vec3{0.0f, 0.0f, 1.0f};
        flash.velocity = Vec3{0.0f, 0.0f, 2.0f};
        flash.color = Vec3{2.5f, 1.4f, 0.3f}; // HDR intense incandescent flash
        flash.size = 6.0f * caliber_scale;
        flash.growth_rate = 12.0f * caliber_scale;
        flash.alpha = 1.0f;
        flash.age_seconds = 0.0f;
        flash.max_lifetime_seconds = 0.25f;
        flash.kind = ParticleVfxKind::ArtilleryMuzzleFlash;
        particles_.push_back(flash);
    }

    // 2. Billowing gunpowder smoke burst
    for (int i = 0; i < 4; ++i) {
        const float angle = (static_cast<float>(i) / 4.0f) * 6.283f + fast_pseudo_rand(seed + i);
        const float speed = (2.0f + fast_pseudo_rand(seed + 10 + i) * 3.0f) * caliber_scale;

        Particle3D smoke;
        smoke.position = ground_pos + Vec3{0.0f, 0.0f, 0.5f};
        smoke.velocity = Vec3{std::cos(angle) * speed, std::sin(angle) * speed, (3.0f + fast_pseudo_rand(seed + 20 + i) * 2.0f) * caliber_scale};
        smoke.color = Vec3{0.75f, 0.72f, 0.68f} * (0.9f + fast_pseudo_rand(seed + 30 + i) * 0.2f); // Gunpowder white-sulfur gray
        smoke.size = 3.0f * caliber_scale;
        smoke.growth_rate = 4.0f * caliber_scale;
        smoke.alpha = 0.9f;
        smoke.age_seconds = 0.0f;
        smoke.max_lifetime_seconds = 3.0f + fast_pseudo_rand(seed + 40 + i) * 1.5f;
        smoke.rotation_rad = fast_pseudo_rand(seed + 50 + i) * 6.28f;
        smoke.rotation_speed = (fast_pseudo_rand(seed + 60 + i) - 0.5f) * 1.2f;
        smoke.kind = ParticleVfxKind::BattlefieldGunpowder;
        particles_.push_back(smoke);
    }
}

void LivingMapVfx3D::spawn_train_steam(const Vec3& train_pos, const Vec3& train_forward, float speed) {
    if (particles_.size() >= 10'000) return;
    const std::uint32_t seed = static_cast<std::uint32_t>(particles_.size() * 19 + 53);

    Particle3D p;
    p.position = train_pos + Vec3{0.0f, 0.0f, 2.5f};
    // Steam blows backwards relative to train movement + upward buoyant expansion
    p.velocity = (train_forward * (-speed * 0.4f)) + Vec3{0.0f, 0.0f, 3.0f};
    p.color = Vec3{0.95f, 0.95f, 0.98f}; // Clean white condensed steam
    p.size = 1.5f;
    p.growth_rate = 3.0f;
    p.alpha = 0.7f;
    p.age_seconds = 0.0f;
    p.max_lifetime_seconds = 1.8f;
    p.rotation_rad = fast_pseudo_rand(seed) * 6.28f;
    p.rotation_speed = (fast_pseudo_rand(seed + 1) - 0.5f) * 1.0f;
    p.kind = ParticleVfxKind::SteamTrainPlume;
    particles_.push_back(p);
}

void LivingMapVfx3D::spawn_ship_wake(const Vec3& ship_stern_pos, const Vec3& ship_forward, float /*speed*/) {
    if (particles_.size() >= 10'000) return;

    // Wake spreads horizontally across water surface
    const Vec3 right{-ship_forward.y, ship_forward.x, 0.0f};

    for (int side = -1; side <= 1; side += 2) {
        Particle3D wake;
        wake.position = ship_stern_pos + right * (static_cast<float>(side) * 1.2f);
        wake.velocity = right * (static_cast<float>(side) * 0.8f);
        wake.color = Vec3{0.85f, 0.92f, 0.98f}; // Aerated sea foam
        wake.size = 1.8f;
        wake.growth_rate = 2.0f;
        wake.alpha = 0.6f;
        wake.age_seconds = 0.0f;
        wake.max_lifetime_seconds = 2.5f;
        wake.kind = ParticleVfxKind::NavalWakeFoam;
        particles_.push_back(wake);
    }
}

void LivingMapVfx3D::update(float dt_seconds, const Vec3& global_wind) noexcept {
    std::size_t write_idx = 0;
    for (std::size_t i = 0; i < particles_.size(); ++i) {
        auto& p = particles_[i];
        p.age_seconds += dt_seconds;
        if (p.age_seconds >= p.max_lifetime_seconds) continue;

        const float life_progress = p.age_seconds / p.max_lifetime_seconds;

        // Physics integration based on particle kind
        if (p.kind == ParticleVfxKind::FactoryChimneySmoke || p.kind == ParticleVfxKind::BattlefieldGunpowder) {
            // Wind drift + thermal lift deceleration + drag
            p.velocity = p.velocity + global_wind * (dt_seconds * 0.5f);
            p.velocity.z = std::max(0.5f, p.velocity.z - dt_seconds * 0.8f);
            p.position = p.position + p.velocity * dt_seconds;
            p.size += p.growth_rate * dt_seconds;
            p.alpha = (1.0f - life_progress) * 0.8f;
        } else if (p.kind == ParticleVfxKind::SteamTrainPlume) {
            p.velocity = p.velocity + global_wind * (dt_seconds * 0.3f);
            p.position = p.position + p.velocity * dt_seconds;
            p.size += p.growth_rate * dt_seconds;
            p.alpha = (1.0f - life_progress * life_progress) * 0.7f;
        } else if (p.kind == ParticleVfxKind::ArtilleryMuzzleFlash) {
            p.size += p.growth_rate * dt_seconds;
            p.alpha = 1.0f - (life_progress * 4.0f); // Quick fade
        } else if (p.kind == ParticleVfxKind::NavalWakeFoam) {
            p.position = p.position + p.velocity * dt_seconds;
            p.size += p.growth_rate * dt_seconds;
            p.alpha = (1.0f - life_progress) * 0.5f;
        }

        p.rotation_rad += p.rotation_speed * dt_seconds;
        particles_[write_idx++] = p;
    }
    particles_.resize(write_idx);
}

std::size_t LivingMapVfx3D::generate_gpu_vertices(std::span<GpuParticleVertex> out_vertices) const noexcept {
    const std::size_t count = std::min(particles_.size(), out_vertices.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto& p = particles_[i];
        out_vertices[i] = {
            .position = p.position,
            .size = p.size,
            .color = p.color,
            .alpha = std::clamp(p.alpha, 0.0f, 1.0f),
            .rotation = p.rotation_rad,
            .kind = static_cast<std::uint32_t>(p.kind)
        };
    }
    return count;
}

} // namespace thunder
