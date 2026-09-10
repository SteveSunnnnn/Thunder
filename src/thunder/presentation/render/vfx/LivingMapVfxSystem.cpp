#include "thunder/presentation/render/vfx/LivingMapVfxSystem.hpp"
#include <algorithm>
#include <cmath>

namespace thunder {

void LivingMapVfxSystem::spawn_train(float x0, float y0, float x1, float y1) {
    trains_.push_back({x0, y0, x1, y1, 25.0f, 0.0f});
}

void LivingMapVfxSystem::spawn_ship(float x0, float y0, float x1, float y1) {
    ships_.push_back({x0, y0, x1, y1, 12.0f, 0.0f});
}

void LivingMapVfxSystem::update(float dt) {
    // Update trains
    for (auto& tr : trains_) {
        const float dx = tr.target_x - tr.x;
        const float dy = tr.target_y - tr.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float step = tr.speed * dt;
        if (dist > 1.0f && step < dist) {
            tr.x += (dx / dist) * step;
            tr.y += (dy / dist) * step;
            tr.smoke_timer += dt;
            if (tr.smoke_timer >= 0.2f) {
                tr.smoke_timer = 0.0f;
                particles_.push_back({tr.x, tr.y, 4.0f, 0.8f, 1.2f});
            }
        } else {
            tr.x = tr.target_x;
            tr.y = tr.target_y;
        }
    }
    std::erase_if(trains_, [](const auto& tr) {
        const float dx = tr.target_x - tr.x;
        const float dy = tr.target_y - tr.y;
        return (dx * dx + dy * dy) <= 1.0f;
    });

    // Update ships
    for (auto& sh : ships_) {
        const float dx = sh.target_x - sh.x;
        const float dy = sh.target_y - sh.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float step = sh.speed * dt;
        if (dist > 1.0f && step < dist) {
            sh.x += (dx / dist) * step;
            sh.y += (dy / dist) * step;
            sh.wake_timer += dt;
            if (sh.wake_timer >= 0.3f) {
                sh.wake_timer = 0.0f;
                particles_.push_back({sh.x, sh.y, 6.0f, 0.5f, 2.0f});
            }
        } else {
            sh.x = sh.target_x;
            sh.y = sh.target_y;
        }
    }
    std::erase_if(ships_, [](const auto& sh) {
        const float dx = sh.target_x - sh.x;
        const float dy = sh.target_y - sh.y;
        return (dx * dx + dy * dy) <= 1.0f;
    });

    // Update particles
    for (auto& p : particles_) {
        p.lifetime_s -= dt;
        p.size += dt * 3.0f;
        p.alpha = std::max(0.0f, p.alpha - dt * 0.5f);
        p.y -= dt * 4.0f; // drift upwards
    }

    std::erase_if(particles_, [](const auto& p) { return p.lifetime_s <= 0.0f; });
}

void LivingMapVfxSystem::render(UiDrawList& ui, UiRect scissor) {
    // Render particles
    for (const auto& p : particles_) {
        const auto alpha_byte = static_cast<std::uint32_t>(p.alpha * 255.0f);
        const std::uint32_t col = (alpha_byte << 24) | 0x00f0f0f0u;
        ui.quad({p.x - p.size * 0.5f, p.y - p.size * 0.5f, p.size, p.size}, col, scissor);
    }

    // Render train icons
    for (const auto& tr : trains_) {
        ui.quad({tr.x - 3.0f, tr.y - 3.0f, 6.0f, 6.0f}, 0xff202020u, scissor);
    }

    // Render ship icons
    for (const auto& sh : ships_) {
        ui.quad({sh.x - 4.0f, sh.y - 4.0f, 8.0f, 8.0f}, 0xff5a3e26u, scissor);
    }
}

} // namespace thunder
