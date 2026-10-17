cmake_minimum_required(VERSION 3.22)
project(resgen)

set(CMAKE_CXX_STANDARD 23)

# always build optimized, even in Debug configs
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_RELEASE}")

add_executable(resgen
        src/main.cpp
)

target_link_libraries(resgen PRIVATE
        ailo_fileio
)
