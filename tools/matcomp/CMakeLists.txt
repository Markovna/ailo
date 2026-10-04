cmake_minimum_required(VERSION 3.22)
project(matcomp)

set(CMAKE_CXX_STANDARD 23)

# always build optimized, even in Debug configs
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_RELEASE}")

add_executable(matcomp
        src/main.cpp
        src/Json.cpp
        src/Json.h
        src/MaterialDefinition.cpp
        src/MaterialDefinition.h
        src/ShaderGenerator.cpp
        src/ShaderGenerator.h
        src/ShaderCompiler.cpp
        src/ShaderCompiler.h
)

# shaderc from the Vulkan SDK. The SDK ships a separate debug build: its runtime library (/MDd) has to match ours.
find_package(Vulkan REQUIRED)
get_filename_component(VULKAN_SDK_LIB_DIR "${Vulkan_LIBRARY}" DIRECTORY)
find_library(SHADERC_COMBINED_LIB shaderc_combined HINTS ${VULKAN_SDK_LIB_DIR} $ENV{VULKAN_SDK}/Lib $ENV{VULKAN_SDK}/lib REQUIRED)
find_library(SHADERC_COMBINED_DEBUG_LIB shaderc_combinedd HINTS ${VULKAN_SDK_LIB_DIR} $ENV{VULKAN_SDK}/Lib $ENV{VULKAN_SDK}/lib)
if(NOT SHADERC_COMBINED_DEBUG_LIB)
    set(SHADERC_COMBINED_DEBUG_LIB ${SHADERC_COMBINED_LIB})
endif()

target_include_directories(matcomp PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(matcomp PRIVATE
        ailo_material
        $<$<CONFIG:Debug>:${SHADERC_COMBINED_DEBUG_LIB}>
        $<$<NOT:$<CONFIG:Debug>>:${SHADERC_COMBINED_LIB}>
)
