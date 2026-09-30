#include "App.h"

#include <format>
#include <ranges>
#include <stdexcept>

#include "utils/Utils.h"

namespace ailo {

static constexpr std::array kFrameStages = {
    Stage::First, Stage::PreUpdate, Stage::Update, Stage::FixedUpdate, Stage::PostUpdate, Stage::Render, Stage::Last
};

void throwMissingSystemParam(std::string_view systemName, std::string_view typeName) {
    throw std::runtime_error(std::format(
        "system '{}' needs resource '{}' (is a plugin not added, or added in the wrong order?)", systemName, typeName));
}

App::App() {
    m_world.insertResource<AppControl>();
    m_world.insertResource<FixedTime>();
}

void App::run() {
    m_running = true;
    auto runningGuard = utils::scope_exit {[&] { m_running = false; }};

    // Validated right before each phase, so Startup systems can insert resources used by later stages.
    validate(std::array { Stage::Startup });
    runStage(Stage::Startup);

    validate(kFrameStages);
    auto& control = m_world.resource<AppControl>();
    while (!control.exitRequested) {
        update();
    }

    validate(std::array { Stage::Shutdown });
    runStage(Stage::Shutdown);
}

void App::update() {
    for (auto stage : kFrameStages) {
        if (stage == Stage::FixedUpdate) {
            runFixedStages();
        } else {
            runStage(stage);
        }
    }
}

void App::runFixedStages() {
    auto& fixed = m_world.resource<FixedTime>();
    if (fixed.step <= 0.0f) {
        throw std::logic_error("FixedTime::step must be positive");
    }

    while (fixed.accumulator >= fixed.step) {
        runStage(Stage::FixedUpdate);
        fixed.accumulator -= fixed.step;
        fixed.elapsed += fixed.step;
        fixed.tick++;
    }
    fixed.alpha = fixed.accumulator / fixed.step;
}

void App::runStage(Stage stage) {
    auto& systems = m_stages[std::to_underlying(stage)];

    // Shutdown mirrors destruction: plugins added later (which depend on earlier ones) shut down first.
    if (stage == Stage::Shutdown) {
        for (auto& system : std::views::reverse(systems)) {
            system.run(m_world);
        }
        return;
    }

    for (auto& system : systems) {
        system.run(m_world);
    }
}

void App::validate(std::span<const Stage> stages) const {
    for (auto stage : stages) {
        for (auto& system : m_stages[std::to_underlying(stage)]) {
            system.validate(m_world);
        }
    }
}

}
