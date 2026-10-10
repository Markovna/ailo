add_library(ailo_fileio STATIC
        src/FileIO.cpp
        src/FileIO.h
)
target_include_directories(ailo_fileio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
