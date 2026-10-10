cmake_minimum_required(VERSION 3.22)
project(texc)

set(CMAKE_CXX_STANDARD 23)

# always build optimized, even in Debug configs
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_RELEASE}")

# The packer and stb_image are compiled into texc instead of linking ailo_texture and stb_image, which are built with
# the engine's flags: decoding is where texc spends its time, and the flags above only apply to targets made here.
add_executable(texc
        src/main.cpp
        src/TextureDefinition.cpp
        src/TextureDefinition.h
        ${CMAKE_SOURCE_DIR}/ailo/render/texture/TexturePackage.cpp
        ${CMAKE_SOURCE_DIR}/ailo/render/texture/TexturePacker.cpp
        ${CMAKE_SOURCE_DIR}/third_party/stb_image/src/stb_image.cpp
)

target_include_directories(texc PRIVATE
        ${CMAKE_SOURCE_DIR}/ailo
        ${CMAKE_SOURCE_DIR}/third_party/stb_image/include
)

target_link_libraries(texc PRIVATE
        ailo_fileio
        ailo_json
)
