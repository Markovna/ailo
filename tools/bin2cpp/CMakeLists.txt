cmake_minimum_required(VERSION 3.22)
project(bin2cpp)

set(CMAKE_CXX_STANDARD 23)

# always build optimized, even in Debug configs
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_RELEASE}")

add_executable(bin2cpp
        src/main.cpp
)

target_link_libraries(bin2cpp PRIVATE
        ailo_fileio
)
