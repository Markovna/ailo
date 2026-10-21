# Test checks for the test executables (ailo_tests, lightc_tests); unlike assert they also check in Release builds.
add_library(ailo_testing STATIC
        src/Testing.cpp
        src/Testing.h
)
target_include_directories(ailo_testing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
