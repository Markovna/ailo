add_executable(sandbox
        main.cpp
        SandboxPlugin.cpp
        SandboxPlugin.h
)
target_link_libraries(sandbox PRIVATE ailo)

# Assets are loaded by relative path from the build folder, where sandbox_assets puts them.
set_target_properties(sandbox PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_dependencies(sandbox sandbox_assets)

add_custom_target(sandbox_assets ALL
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/build/run.py --ninja ${AILO_NINJA}
                --source ${CMAKE_CURRENT_SOURCE_DIR}/assets
                --config ${CMAKE_SOURCE_DIR}/build/config.json
                --tools-dir ${AILO_ASSET_TOOLS_DIR}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building sandbox assets"
        USES_TERMINAL
)
add_dependencies(sandbox_assets ${AILO_ASSET_TOOLS})
