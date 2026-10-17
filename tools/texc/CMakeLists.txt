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
        src/TextureEncoder.cpp
        src/TextureEncoder.h
        src/TextureFormats.cpp
        src/TextureFormats.h
        ${CMAKE_SOURCE_DIR}/ailo/render/texture/TexturePackage.cpp
        ${CMAKE_SOURCE_DIR}/ailo/render/texture/TexturePacker.cpp
        ${CMAKE_SOURCE_DIR}/third_party/stb_image/src/stb_image.cpp
)

# Added here rather than at the top level so it gets the optimized flags above; only texc links it.
set(BASISU_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/basis_universal ${CMAKE_CURRENT_BINARY_DIR}/basis_universal EXCLUDE_FROM_ALL)
# basisu passes BASISU_SUPPORT_SSE through its directory's CMAKE_CXX_FLAGS, and its headers depend on it.
if (BASISU_SSE)
    target_compile_definitions(basisu_encoder INTERFACE BASISU_SUPPORT_SSE=1)
else()
    target_compile_definitions(basisu_encoder INTERFACE BASISU_SUPPORT_SSE=0)
endif()

target_include_directories(texc PRIVATE
        ${CMAKE_SOURCE_DIR}/ailo
        ${CMAKE_SOURCE_DIR}/third_party/stb_image/include
        ${CMAKE_SOURCE_DIR}/third_party/basis_universal
)

target_link_libraries(texc PRIVATE
        ailo_fileio
        ailo_json
        basisu_encoder
)
