cmake_minimum_required(VERSION 3.22)
project(ailo)

set(CMAKE_CXX_STANDARD 23)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")

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
add_subdirectory(third_party/entt)
add_subdirectory(third_party/assimp)

add_subdirectory(third_party/imgui)

# Add executable
add_executable(ailo
    ailo/main.cpp
    ailo/render/RenderAPI.cpp
    ailo/render/RenderAPI.h
    ailo/render/ImGuiProcessor.cpp
    ailo/render/ImGuiProcessor.h
    ailo/input/InputSystem.cpp
    ailo/input/InputSystem.h
    ailo/input/InputTypes.h
    ailo/Engine.cpp
    ailo/Engine.h
    ailo/Application.cpp
    ailo/Application.h
    ailo/render/vulkan/VulkanUtils.cpp
    ailo/render/vulkan/VulkanUtils.h
    ailo/render/RenderPrimitive.cpp
    ailo/render/RenderPrimitive.h
    ailo/render/Renderer.cpp
    ailo/render/Renderer.h
    ailo/ecs/Scene.cpp
    ailo/ecs/Scene.h
    ailo/utils/Utils.h
    ailo/render/Shader.cpp
    ailo/render/Shader.h
    ailo/OS.cpp
    ailo/OS.h
    ailo/render/Material.cpp
    ailo/render/Material.h
    ailo/render/Texture.cpp
    ailo/render/Texture.h
    ailo/render/Mesh.cpp
    ailo/render/Mesh.h
    ailo/ecs/Transform.h
    ailo/ecs/Transform.cpp
    ailo/render/CommandBuffer.cpp
    ailo/render/CommandBuffer.h
    ailo/render/UniqueVkHandle.cpp
    ailo/render/UniqueVkHandle.h
    ailo/render/SwapChain.cpp
    ailo/render/SwapChain.h
    ailo/render/FrameBufferCache.cpp
    ailo/render/FrameBufferCache.h
    ailo/render/VulkanConstants.h
    ailo/render/VulkanDevice.cpp
    ailo/render/VulkanDevice.h
    ailo/render/Constants.h
    ailo/render/ResourceContainer.h
    ailo/render/vulkan/Texture.cpp
    ailo/render/vulkan/Texture.h
    ailo/common/LRUCache.h
    ailo/render/RenderPassCache.cpp
    ailo/render/RenderPassCache.h
    ailo/render/PipelineCache.cpp
    ailo/render/PipelineCache.h
    ailo/render/Program.cpp
    ailo/render/Program.h
    ailo/render/vulkan/Resources.cpp
    ailo/render/vulkan/Resources.h
        ailo/resources/ResourcePtr.cpp
        ailo/resources/ResourcePtr.h
        ailo/irradiance/IrradianceMapGenerator.cpp
        ailo/irradiance/IrradianceMapGenerator.h
)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(ailo OpenMP::OpenMP_CXX)
endif()

# Link libraries
target_link_libraries(ailo
        Vulkan::Vulkan
        glfw
        glm::glm
        stb_image
        GPUOpen::VulkanMemoryAllocator
        EnTT::EnTT
        imgui
        assimp::assimp
)

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
    # Parse optional arguments: NAME and DEFINES
    set(options "")
    set(oneValueArgs NAME)
    set(multiValueArgs DEFINES)
    cmake_parse_arguments(SHADER_ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    get_filename_component(FILE_NAME ${SHADER} NAME_WE)
    get_filename_component(FILE_EXT ${SHADER} EXT)
    string(SUBSTRING ${FILE_EXT} 1 -1 FILE_EXT)  # Remove the dot

    # Use custom name if provided, otherwise use filename
    if(SHADER_ARG_NAME)
        set(OUTPUT_NAME ${SHADER_ARG_NAME})
    else()
        set(OUTPUT_NAME ${FILE_NAME})
    endif()

    # Add defines suffix if defines are provided
    if(SHADER_ARG_DEFINES)
        string(REPLACE ";" "_" DEFINES_SUFFIX "${SHADER_ARG_DEFINES}")
    endif()

    set(SPIRV ${CMAKE_BINARY_DIR}/shaders/${OUTPUT_NAME}.${FILE_EXT}.spv)

    # Build define flags for glslc
    set(DEFINE_FLAGS "")
    foreach(DEFINE ${SHADER_ARG_DEFINES})
        list(APPEND DEFINE_FLAGS "-D${DEFINE}")
    endforeach()

    add_custom_command(
            OUTPUT ${SPIRV}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders/"
            COMMAND ${GLSLC} -I${CMAKE_SOURCE_DIR}/shaders ${DEFINE_FLAGS} ${CMAKE_SOURCE_DIR}/${SHADER} -o ${SPIRV}
            DEPENDS ${CMAKE_SOURCE_DIR}/${SHADER}
            COMMENT "Compiling shader ${OUTPUT_NAME}.${FILE_EXT}.spv with defines: ${SHADER_ARG_DEFINES}"
    )

    set_source_files_properties(${SPIRV} PROPERTIES GENERATED TRUE)
    target_sources(${TARGET} PRIVATE ${SPIRV})
endfunction()

# Add shaders
add_shader(ailo shaders/shader.vert)
add_shader(ailo shaders/shader.frag)
add_shader(ailo shaders/imgui.vert)
add_shader(ailo shaders/imgui.frag)
add_shader(ailo shaders/pbr.vert)
add_shader(ailo shaders/pbr.frag NAME pbr DEFINES USE_NORMAL_MAP)
add_shader(ailo shaders/skybox.vert)
add_shader(ailo shaders/skybox.frag)
