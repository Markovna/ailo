#include "app/App.h"
#include "assets/AssetPlugin.h"
#include "AsteroidsPlugin.h"
#include "platform/PlatformPlugin.h"
#include "render/RenderPlugin.h"
#include "ui/ImGuiPlugin.h"

#include <iostream>
#include <stdexcept>

int main() {
    using namespace ailo;

    try {
        App()
            .addPlugin(PlatformPlugin { .window = { .title = "Asteroids", .width = 1920, .height = 1080 } })
            .addPlugin(AssetPlugin {})
            .addPlugin(RenderPlugin { .settings = {
                .shadowMapSize = 2048,
                .shadows = { .cascadeCount = 3, .distance = 120.0f, .casterDistance = 60.0f },
            } })
            .addPlugin(ImGuiPlugin {})
            .addPlugin(AsteroidsPlugin {})
            .run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
