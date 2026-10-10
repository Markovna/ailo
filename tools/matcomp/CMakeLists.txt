cmake_minimum_required(VERSION 3.22)
project(matcomp)

set(CMAKE_CXX_STANDARD 23)

# always build optimized, even in Debug configs
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_RELEASE}")

add_executable(matcomp
        src/main.cpp
        src/MaterialDefinition.cpp
        src/MaterialDefinition.h
        src/ShaderGenerator.cpp
        src/ShaderGenerator.h
        src/ShaderCompiler.cpp
        src/ShaderCompiler.h
)

# The engine's shader templates, searched after any -I.
target_compile_definitions(matcomp PRIVATE MATCOMP_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders")

# glslang, built from the submodule with the same toolchain as matcomp. Added here rather than in the root so it
# picks up the optimized flags above. Only GLSL -> SPIR-V is needed: no spirv-opt, HLSL or standalone tools.
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/glslang ${CMAKE_CURRENT_BINARY_DIR}/glslang)

target_link_libraries(matcomp PRIVATE
        ailo_fileio
        ailo_json
        ailo_material
        glslang::glslang
        glslang::SPIRV
        glslang::glslang-default-resource-limits
)
