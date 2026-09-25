#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "System.h"
#include "World.h"

namespace ailo {

enum class Stage : uint8_t {
    Startup,     // once, before the first frame
    First,       // platform events, time
    PreUpdate,   // input processing
    Update,      // game / user logic
    PostUpdate,  // animation, transforms
    Render,
    Last,        // asset gc, cleanup
    Shutdown,    // once, after the last frame; runs in REVERSE registration order
    Count
};

// Always present in the world. Any system can request exit by taking AppControl&.
struct AppControl {
    bool exitRequested = false;
    void requestExit() { exitRequested = true; }
};

class App;

// A plugin is any type with `void build(App&)`. It inserts resources and registers systems.
// Plugins are plain structs (usually aggregates carrying their settings, e.g. `RenderPlugin { .shadowMapSize = 2048 }`).
// The App keeps the plugin alive for its whole lifetime, so systems may capture `this`.
template<typename P>
concept Plugin = requires(P& plugin, App& app) {
    { plugin.build(app) } -> std::same_as<void>;
};

class App {
public:
    App();
    ~App() = default;

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Builds the plugin immediately. Adding the same plugin type twice is a no-op.
    template<Plugin P>
    App& addPlugin(P plugin) {
        if (m_running) {
            throw std::logic_error("App::addPlugin called while the app is running");
        }
        if (!m_pluginTypes.insert(entt::type_hash<P>::value()).second) {
            return *this;
        }

        auto holder = std::make_unique<PluginHolder<P>>(std::move(plugin));
        P& stored = holder->plugin;
        m_plugins.push_back(std::move(holder));
        stored.build(*this);
        return *this;
    }

    template<Plugin P>
    bool hasPlugin() const {
        return m_pluginTypes.contains(entt::type_hash<P>::value());
    }

    // Systems run in registration order within a stage (Shutdown: reverse order).
    template<typename F>
    App& addSystem(Stage stage, F&& f, std::string name = {}) {
        if (m_running) {
            throw std::logic_error("App::addSystem called while the app is running");
        }
        m_stages[std::to_underlying(stage)].push_back(makeSystem(std::forward<F>(f), std::move(name)));
        return *this;
    }

    template<typename T, typename... Args>
    T& insertResource(Args&&... args) {
        return m_world.insertResource<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    T& resource() { return m_world.resource<T>(); }

    template<typename T>
    T* tryResource() { return m_world.tryResource<T>(); }

    World& world() { return m_world; }

    void run();

private:
    struct PluginHolderBase {
        virtual ~PluginHolderBase() = default;
    };

    template<typename P>
    struct PluginHolder final : PluginHolderBase {
        explicit PluginHolder(P&& plugin) : plugin(std::move(plugin)) {}
        P plugin;
    };

    // Runs a single frame (First..Last).
    void update();
    void runStage(Stage stage);
    void validate(std::span<const Stage> stages) const;

    // Destroyed in reverse order: systems (may capture resources/plugins), then world, then plugins.
    std::vector<std::unique_ptr<PluginHolderBase>> m_plugins;
    std::unordered_set<entt::id_type> m_pluginTypes;
    World m_world;
    std::array<std::vector<System>, std::to_underlying(Stage::Count)> m_stages;
    bool m_running = false;
};

}
