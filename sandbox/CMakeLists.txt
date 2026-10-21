add_executable(sandbox
        main.cpp
        SandboxPlugin.cpp
        SandboxPlugin.h
)
target_link_libraries(sandbox PRIVATE ailo)

# Assets are loaded by relative path from the build folder, where sandbox_assets puts them.
set_target_properties(sandbox PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_dependencies(sandbox sandbox_assets)

# Offline asset build (build/README.md): gen.py writes build_assets.ninja, which compiles every asset in assets/ with
# the tool its extension maps to in config.json and copies everything else into <build>/assets/. CMake only generates
# the manifest the first time; after that it regenerates itself when assets are added or removed.
set(SANDBOX_ASSET_MANIFEST ${CMAKE_BINARY_DIR}/build_assets.ninja)
add_custom_command(
        OUTPUT ${SANDBOX_ASSET_MANIFEST}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/build/gen.py
                --source ${CMAKE_CURRENT_SOURCE_DIR}/assets
                --build-dir ${CMAKE_BINARY_DIR}
                --config ${CMAKE_SOURCE_DIR}/build/config.json
                --tools-dir ${AILO_ASSET_TOOLS_DIR}
        DEPENDS ${CMAKE_SOURCE_DIR}/build/gen.py ${CMAKE_SOURCE_DIR}/build/config.json
        COMMENT "Generating build_assets.ninja"
)

# run.py runs ninja, then cleandead, which removes the outputs of assets that no longer exist, and rebuilds assets whose
# inputs were replaced by older files. The manifest's path is relative to the build folder: ninja regenerates it only
# if the path matches its build edge (build_assets.ninja) exactly.
add_custom_target(sandbox_assets ALL
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/build/run.py --ninja ${AILO_NINJA}
                --manifest build_assets.ninja --state asset_build/inputs.json
        DEPENDS ${SANDBOX_ASSET_MANIFEST}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building sandbox assets"
        USES_TERMINAL
)
add_dependencies(sandbox_assets ${AILO_ASSET_TOOLS})
