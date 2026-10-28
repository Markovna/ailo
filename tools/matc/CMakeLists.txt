cmake_minimum_required(VERSION 3.22)
project(matc)

set(CMAKE_CXX_STANDARD 23)

# always build optimized, even in Debug configs
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_RELEASE}")

add_executable(matc
        src/main.cpp
        src/MaterialDefinition.cpp
        src/MaterialDefinition.h
        src/MaterialInstanceDefinition.cpp
        src/MaterialInstanceDefinition.h
        src/ShaderGenerator.cpp
        src/ShaderGenerator.h
        src/ShaderCompiler.cpp
        src/ShaderCompiler.h
)

# The engine's shader templates, searched after any -I.
target_compile_definitions(matc PRIVATE MATC_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders")
# The engine's assets, for .matinst files that reference engine:/ materials.
target_compile_definitions(matc PRIVATE MATC_ENGINE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets")

# glslang, built from the submodule with the same toolchain as matc. Added here rather than in the root so it
# picks up the optimized flags above. Only GLSL -> SPIR-V is needed: no spirv-opt, HLSL or standalone tools.
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/glslang ${CMAKE_CURRENT_BINARY_DIR}/glslang)

target_link_libraries(matc PRIVATE
        ailo_fileio
        ailo_json
        ailo_material
        glslang::glslang
        glslang::SPIRV
        glslang::glslang-default-resource-limits
)
