cmake_minimum_required(VERSION 3.22)
project(texc)

set(CMAKE_CXX_STANDARD 23)

add_executable(texc
        src/main.cpp
        src/TextureDefinition.cpp
        src/TextureDefinition.h
)

target_link_libraries(texc PRIVATE
        ailo_fileio
        ailo_json
        ailo_texture
)
