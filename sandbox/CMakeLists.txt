add_executable(sandbox
        main.cpp
        SandboxPlugin.cpp
        SandboxPlugin.h
)
target_link_libraries(sandbox PRIVATE ailo)

# Assets are loaded by relative path from the build folder, where ailo_assets puts them.
set_target_properties(sandbox PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_dependencies(sandbox ailo_assets)
