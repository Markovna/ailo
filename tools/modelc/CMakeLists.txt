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

# Added here rather than at the top level so it gets the optimized flags above; nothing else links it.
set(MESHOPT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/meshoptimizer ${CMAKE_CURRENT_BINARY_DIR}/meshoptimizer EXCLUDE_FROM_ALL)

target_include_directories(modelc PRIVATE
        ${CMAKE_SOURCE_DIR}/ailo
        ${CMAKE_SOURCE_DIR}/third_party/stb_image/include
)

target_link_libraries(modelc PRIVATE
        assimp::assimp
        meshoptimizer
        glm::glm
        ailo_fileio
        ailo_json
)
