add_library(stb_image STATIC src/stb_image.cpp src/stb_image_write.cpp)
target_include_directories(stb_image PUBLIC include)