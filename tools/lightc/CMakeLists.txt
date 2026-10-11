cmake_minimum_required(VERSION 3.22)
project(lightc)

set(CMAKE_CXX_STANDARD 23)

# always build optimized, even in Debug configs
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_RELEASE}")

# The package code and stb_image are compiled into lightc instead of linking ailo_texture and stb_image, which are
# built with the engine's flags (see tools/texc).
add_executable(lightc
        src/main.cpp
        src/Ibl.cpp
        src/Ibl.h
        src/LightDefinition.cpp
        src/LightDefinition.h
        ${CMAKE_SOURCE_DIR}/ailo/render/texture/TexturePackage.cpp
        ${CMAKE_SOURCE_DIR}/third_party/stb_image/src/stb_image.cpp
)

target_include_directories(lightc PRIVATE
        ${CMAKE_SOURCE_DIR}/ailo
        ${CMAKE_SOURCE_DIR}/third_party/stb_image/include
)

target_link_libraries(lightc PRIVATE
        glm::glm
        ailo_fileio
        ailo_json
)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(lightc PRIVATE OpenMP::OpenMP_CXX)
    # FindOpenMP can fail to detect the runtime library (e.g. toolchain path with spaces),
    # so also pass the flag at link time to pull in libgomp
    target_link_options(lightc PRIVATE ${OpenMP_CXX_FLAGS})
endif()
