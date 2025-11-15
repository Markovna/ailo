cmake_minimum_required(VERSION 3.22)
project(ailo)

set(CMAKE_CXX_STANDARD 23)

# Find Vulkan package
find_package(Vulkan REQUIRED)

# Add GLFW library from third_party
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/glfw)
add_subdirectory(third_party/glm)
add_subdirectory(third_party/stb_image)
add_subdirectory(third_party/vkmemalloc)

# Add executable
add_executable(ailo
    ailo/main.cpp
    ailo/render/RenderAPI.cpp
    ailo/render/RenderAPI.h
)

# Link libraries
target_link_libraries(ailo Vulkan::Vulkan glfw glm::glm stb_image GPUOpen::VulkanMemoryAllocator)

# Include directories
target_include_directories(ailo PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/ailo
)

# Find glslc shader compiler
find_program(GLSLC glslc HINTS
    $ENV{VULKAN_SDK}/bin
    $ENV{VULKAN_SDK}/Bin
    ${Vulkan_GLSLC_EXECUTABLE}
)

if(NOT GLSLC)
    message(FATAL_ERROR "glslc shader compiler not found!")
endif()

# Shader compilation function
function(add_shader TARGET SHADER)
    get_filename_component(FILE_NAME ${SHADER} NAME)
    get_filename_component(FILE_EXT ${FILE_NAME} LAST_EXT)
    string(REPLACE "." "_" SHADER_NAME ${FILE_NAME})

    set(SPIRV "${CMAKE_BINARY_DIR}/shaders/${FILE_NAME}.spv")

    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders/"
        COMMAND ${GLSLC} ${CMAKE_SOURCE_DIR}/${SHADER} -o ${SPIRV}
        DEPENDS ${CMAKE_SOURCE_DIR}/${SHADER}
        COMMENT "Compiling shader ${FILE_NAME}"
    )

    set_source_files_properties(${SPIRV} PROPERTIES GENERATED TRUE)
    target_sources(${TARGET} PRIVATE ${SPIRV})
endfunction()

# Add shaders
add_shader(ailo shaders/shader.vert)
add_shader(ailo shaders/shader.frag)

# Create shaders directory in build folder
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)
