add_executable(asteroids
        main.cpp
        AsteroidsPlugin.cpp
        AsteroidsPlugin.h
)
target_link_libraries(asteroids PRIVATE ailo)

# Built into its own folder, <build>/asteroids/, next to its assets: the sandbox already owns <build>/assets/ and
# <build>/build_assets.ninja. Assets are loaded by relative path from that folder.
set_target_properties(asteroids PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_CURRENT_BINARY_DIR}>)
add_dependencies(asteroids asteroids_assets)

# Offline asset build (build/README.md)
add_custom_target(asteroids_assets ALL
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/build/run.py --ninja ${AILO_NINJA}
                --source ${CMAKE_CURRENT_SOURCE_DIR}/assets
                --config ${CMAKE_SOURCE_DIR}/build/config.json
                --tools-dir ${AILO_ASSET_TOOLS_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Building asteroids assets"
        USES_TERMINAL
)
add_dependencies(asteroids_assets ${AILO_ASSET_TOOLS})
