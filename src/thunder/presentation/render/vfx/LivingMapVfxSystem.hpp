#pragma once

#include "thunder/presentation/ui/StrategyUi.hpp"
#include <cstdint>
#include <vector>

namespace thunder {

struct SteamTrainVfx {
    float x = 0.0f;
    float y = 0.0f;
    float target_x = 0.0f;
    float target_y = 0.0f;
    float speed = 20.0f;
    float smoke_timer = 0.0f;
};

struct CargoShipVfx {
    float x = 0.0f;
    float y = 0.0f;
    float target_x = 0.0f;
    float target_y = 0.0f;
    float speed = 10.0f;
    float wake_timer = 0.0f;
};

struct SmokeParticle {
    float x = 0.0f;
    float y = 0.0f;
    float size = 4.0f;
    float alpha = 1.0f;
    float lifetime_s = 1.5f;
};

class LivingMapVfxSystem {
public:
    LivingMapVfxSystem() = default;

    void spawn_train(float x0, float y0, float x1, float y1);
    void spawn_ship(float x0, float y0, float x1, float y1);

    void update(float dt_seconds);
    void render(UiDrawList& ui, UiRect scissor = {});

    [[nodiscard]] std::size_t particle_count() const noexcept { return particles_.size(); }
    [[nodiscard]] std::size_t train_count() const noexcept { return trains_.size(); }
    [[nodiscard]] std::size_t ship_count() const noexcept { return ships_.size(); }

private:
    std::vector<SteamTrainVfx> trains_;
    std::vector<CargoShipVfx> ships_;
    std::vector<SmokeParticle> particles_;
};

} // namespace thunder
