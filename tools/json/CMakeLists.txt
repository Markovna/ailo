add_library(ailo_json STATIC
        src/Json.cpp
        src/Json.h
)
target_include_directories(ailo_json PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
