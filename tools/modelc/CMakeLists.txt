cmake_minimum_required(VERSION 3.22)
project(modelc)

set(CMAKE_CXX_STANDARD 23)

# always build optimized, even in Debug configs
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_RELEASE}")

# The package code, the texture packer and stb_image are compiled into modelc instead of linking ailo_model,
# ailo_texture and stb_image, which are built with the engine's flags (see tools/texc). Assimp is built with them too.
add_executable(modelc
        src/main.cpp
        src/ModelCompiler.cpp
        src/ModelCompiler.h
        src/ModelDefinition.cpp
        src/ModelDefinition.h
        ${CMAKE_SOURCE_DIR}/ailo/render/model/ModelPackage.cpp
        ${CMAKE_SOURCE_DIR}/ailo/render/texture/TexturePackage.cpp
        ${CMAKE_SOURCE_DIR}/ailo/render/texture/TexturePacker.cpp
        ${CMAKE_SOURCE_DIR}/third_party/stb_image/src/stb_image.cpp
)

target_include_directories(modelc PRIVATE
        ${CMAKE_SOURCE_DIR}/ailo
        ${CMAKE_SOURCE_DIR}/third_party/stb_image/include
)

target_link_libraries(modelc PRIVATE
        assimp::assimp
        glm::glm
        ailo_fileio
        ailo_json
)
