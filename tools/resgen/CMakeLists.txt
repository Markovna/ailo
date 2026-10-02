cmake_minimum_required(VERSION 3.22)
project(resgen)

set(CMAKE_CXX_STANDARD 23)

# always build optimized, even in Debug configs
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_RELEASE}")

add_executable(resgen
        src/main.cpp
        src/IrradianceMapGenerator.cpp
        src/IrradianceMapGenerator.h
)

target_link_libraries(resgen stb_image glm)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(resgen OpenMP::OpenMP_CXX)
    # FindOpenMP can fail to detect the runtime library (e.g. toolchain path with spaces),
    # so also pass the flag at link time to pull in libgomp
    target_link_options(resgen PRIVATE ${OpenMP_CXX_FLAGS})
endif()
