#include "App.h"

#include <format>
#include <ranges>
#include <stdexcept>

namespace ailo {

static constexpr std::array kFrameStages = {
    Stage::First, Stage::PreUpdate, Stage::Update, Stage::PostUpdate, Stage::Render, Stage::Last
};

void throwMissingSystemParam(std::string_view systemName, std::string_view typeName) {
    throw std::runtime_error(std::format(
        "system '{}' needs resource '{}' (is a plugin not added, or added in the wrong order?)", systemName, typeName));
}

App::App() {
    m_world.insertResource<AppControl>();
}

void App::run() {
    m_running = true;
    struct ResetRunning { bool& flag; ~ResetRunning() { flag = false; } } resetRunning { m_running };

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
        runStage(stage);
    }
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
