#include "app/App.h"
#include "assets/AssetPlugin.h"
#include "platform/PlatformPlugin.h"
#include "render/RenderPlugin.h"
#include "sandbox/SandboxPlugin.h"

#include <iostream>
#include <stdexcept>

int main() {
    using namespace ailo;

    try {
        App()
            .addPlugin(PlatformPlugin { .window = { .title = "Ailo", .width = 2400, .height = 1400 } })
            .addPlugin(AssetPlugin {})
            .addPlugin(RenderPlugin { .settings = { .shadowMapSize = 1024 } })
            .addPlugin(SandboxPlugin {})
            .run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
