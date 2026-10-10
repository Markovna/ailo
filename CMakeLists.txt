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
add_subdirectory(third_party/imgui)

set(BUILD_SHARED_LIBS ON)
add_subdirectory(third_party/assimp)
unset(BUILD_SHARED_LIBS)

# Jolt Physics. Its config defines (JPH_DEBUG_RENDERER, JPH_PROFILE_ENABLED, JPH_FLOATING_POINT_EXCEPTIONS_ENABLED, ...)
# are PUBLIC on the Jolt target and must match between the library and our code: never define them by hand.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)   # Jolt defaults to /MT; everything else uses /MD
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)                # keep CMake's per-config flags, same as our code
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)               # /Wall /WX breaks on newer compilers
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE ON CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
# GPU compute backends (only used by hair simulation)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/JoltPhysics/Build)

add_subdirectory(tools/resgen)

add_library(ailo_material STATIC
    ailo/render/material/MaterialEnums.cpp
    ailo/render/material/MaterialEnums.h
    ailo/render/material/MaterialPackage.cpp
    ailo/render/material/MaterialPackage.h
    ailo/render/material/Variant.h
)
target_include_directories(ailo_material PUBLIC ${CMAKE_SOURCE_DIR}/ailo)

# Texture package format and packer, shared by the engine and the offline asset compiler.
add_library(ailo_texture STATIC
    ailo/render/texture/TexturePackage.cpp
    ailo/render/texture/TexturePackage.h
    ailo/render/texture/TexturePacker.cpp
    ailo/render/texture/TexturePacker.h
)
target_include_directories(ailo_texture PUBLIC ${CMAKE_SOURCE_DIR}/ailo)
target_link_libraries(ailo_texture PRIVATE stb_image)

add_subdirectory(tools/fileio)
add_subdirectory(tools/json)
add_subdirectory(tools/matcomp)

# Add executable
add_executable(ailo
    ailo/main.cpp
    ailo/render/RenderAPI.cpp
    ailo/render/RenderAPI.h
    ailo/render/ImGuiRenderer.cpp
    ailo/render/ImGuiRenderer.h
    ailo/input/InputSystem.cpp
    ailo/input/InputSystem.h
    ailo/input/InputTypes.h
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
    ailo/render/MaterialInstance.cpp
    ailo/render/MaterialInstance.h
    ailo/render/Skybox.h
    ailo/render/Texture.cpp
    ailo/render/Texture.h
    ailo/render/Mesh.h
    ailo/render/Model.cpp
    ailo/render/Model.h
    ailo/ecs/Transform.h
    ailo/ecs/Transform.cpp
    ailo/ecs/Hierarchy.h
    ailo/ecs/Hierarchy.cpp
    ailo/render/CommandBuffer.cpp
    ailo/render/DeletionQueue.cpp
    ailo/render/DeletionQueue.h
    ailo/render/StagePool.cpp
    ailo/render/StagePool.h
    ailo/render/CommandBuffer.h
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
    ailo/render/vulkan/DescriptorSet.cpp
    ailo/render/vulkan/DescriptorSet.h
    ailo/common/LRUCache.h
    ailo/render/RenderPassCache.cpp
    ailo/render/RenderPassCache.h
    ailo/render/SamplerCache.cpp
    ailo/render/SamplerCache.h
    ailo/render/PipelineCache.cpp
    ailo/render/PipelineCache.h
    ailo/render/Program.cpp
    ailo/render/Program.h
    ailo/render/vulkan/Resources.h
        ailo/ecs/SceneLighting.cpp
        ailo/ecs/SceneLighting.h
        ailo/ecs/Lights.h
        ailo/ecs/Camera.cpp
        ailo/ecs/Camera.h
        ailo/render/Renderable.cpp
        ailo/render/Renderable.h
        ailo/render/Skin.h
        ailo/render/Animation.h
        ailo/render/Skeleton.cpp
        ailo/render/Skeleton.h
        ailo/ecs/AnimatorComponent.h
        ailo/ecs/AnimationPlugin.cpp
        ailo/ecs/AnimationPlugin.h
        ailo/ecs/Spawn.cpp
        ailo/ecs/Spawn.h
        ailo/platform/Platform.cpp
        ailo/platform/Platform.h
        ailo/di/di.h
        ailo/app/App.cpp
        ailo/app/App.h
        ailo/app/System.h
        ailo/app/World.h
        ailo/assets/AssetPlugin.h
        ailo/platform/PlatformPlugin.cpp
        ailo/platform/PlatformPlugin.h
        ailo/render/RenderPlugin.cpp
        ailo/render/RenderPlugin.h
        ailo/sandbox/SandboxPlugin.cpp
        ailo/sandbox/SandboxPlugin.h
        ailo/ui/ImGuiPlugin.cpp
        ailo/ui/ImGuiPlugin.h
        ailo/ui/OrbitCameraPlugin.cpp
        ailo/ui/OrbitCameraPlugin.h
        ailo/physics/PhysicsBodies.cpp
        ailo/physics/PhysicsBodies.h
        ailo/physics/PhysicsComponents.h
        ailo/physics/PhysicsLayers.h
        ailo/physics/PhysicsMath.h
        ailo/physics/PhysicsPlugin.cpp
        ailo/physics/PhysicsPlugin.h
        ailo/physics/PhysicsWorld.cpp
        ailo/physics/PhysicsWorld.h
        ailo/assets/AssetServer.h
        ailo/assets/AssetServer.cpp
        ailo/render/DefaultAssets.h
        ailo/render/DefaultAssetFactory.cpp
        ailo/render/DefaultAssetFactory.h
)

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
        Jolt
        ailo_material
)

# Include directories
target_include_directories(ailo PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/ailo
)

# GLM configuration for Vulkan
target_compile_definitions(ailo PRIVATE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_compile_definitions(ailo PRIVATE GLFW_INCLUDE_VULKAN)

target_compile_definitions(ailo PRIVATE VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS)

# VK_KHR_portability_subset (MoltenVK) is declared in vulkan_beta.h
target_compile_definitions(ailo PRIVATE VK_ENABLE_BETA_EXTENSIONS)

# Copy linked DLLs next to the executable
if(WIN32)
    add_custom_command(TARGET ailo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:ailo> $<TARGET_FILE_DIR:ailo>
        COMMAND_EXPAND_LISTS
    )
endif()

#target_compile_definitions(ailo PRIVATE NDEBUG)

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
            COMMAND ${GLSLC} -I${CMAKE_SOURCE_DIR}/shaders ${DEFINE_FLAGS} -MD -MF ${SPIRV}.d ${CMAKE_SOURCE_DIR}/${SHADER} -o ${SPIRV}
            DEPENDS ${CMAKE_SOURCE_DIR}/${SHADER}
            DEPFILE ${SPIRV}.d
            COMMENT "Compiling shader ${OUTPUT_NAME}.${FILE_EXT}.spv with defines: ${SHADER_ARG_DEFINES}"
    )

    set_source_files_properties(${SPIRV} PROPERTIES GENERATED TRUE)
    target_sources(${TARGET} PRIVATE ${SPIRV})
endfunction()

function(add_material TARGET MATERIAL)
    get_filename_component(MATERIAL_NAME ${MATERIAL} NAME_WE)
    set(OUTPUT ${CMAKE_BINARY_DIR}/materials/${MATERIAL_NAME}.matpack)

    add_custom_command(
            OUTPUT ${OUTPUT}
            COMMAND $<TARGET_FILE:matcomp> -I ${CMAKE_SOURCE_DIR}/shaders --depfile ${OUTPUT}.d -o ${OUTPUT} ${CMAKE_SOURCE_DIR}/${MATERIAL}
            DEPENDS matcomp ${CMAKE_SOURCE_DIR}/${MATERIAL}
            DEPFILE ${OUTPUT}.d
            COMMENT "Compiling material ${MATERIAL_NAME}.matpack"
    )

    set_source_files_properties(${OUTPUT} PROPERTIES GENERATED TRUE)
    target_sources(${TARGET} PRIVATE ${OUTPUT})
endfunction()

# Generate DFG LUT texture using resgen tool
set(DFG_LUT_OUTPUT ${CMAKE_BINARY_DIR}/assets/textures/dfg_lut.hdr)
add_custom_command(
        OUTPUT ${DFG_LUT_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/assets/textures/"
        COMMAND $<TARGET_FILE:resgen> ibl-dfg ${DFG_LUT_OUTPUT}
        DEPENDS resgen
        COMMENT "Generating DFG LUT texture"
)
target_sources(ailo PRIVATE ${DFG_LUT_OUTPUT})

# Radiance map generation function
function(add_radiance_map TARGET INPUT_HDR)
    set(oneValueArgs NAME SIZE SAMPLES)
    cmake_parse_arguments(RADIANCE "" "${oneValueArgs}" "" ${ARGN})

    get_filename_component(FILE_NAME ${INPUT_HDR} NAME_WE)

    if(RADIANCE_NAME)
        set(OUTPUT_NAME ${RADIANCE_NAME})
    else()
        set(OUTPUT_NAME ${FILE_NAME})
    endif()

    set(OPTIONAL_ARGS "")
    if(RADIANCE_SIZE)
        list(APPEND OPTIONAL_ARGS ${RADIANCE_SIZE})
        if(RADIANCE_SAMPLES)
            list(APPEND OPTIONAL_ARGS ${RADIANCE_SAMPLES})
        endif()
    endif()

    set(OUTPUT_DIR ${CMAKE_BINARY_DIR}/assets/textures/${OUTPUT_NAME})
    set(OUTPUT_BASE ${OUTPUT_DIR}/${OUTPUT_NAME}.hdr)

    set(STAMP ${OUTPUT_DIR}/${OUTPUT_NAME}.stamp)

    add_custom_command(
            OUTPUT ${STAMP}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
            COMMAND $<TARGET_FILE:resgen> ibl-prefilter ${CMAKE_SOURCE_DIR}/${INPUT_HDR} ${OUTPUT_BASE} ${OPTIONAL_ARGS}
            COMMAND ${CMAKE_COMMAND} -E touch ${STAMP}
            DEPENDS resgen ${CMAKE_SOURCE_DIR}/${INPUT_HDR}
            COMMENT "Generating radiance map from ${INPUT_HDR}"
            USES_TERMINAL
    )

    target_sources(${TARGET} PRIVATE ${STAMP})

    # Only resgen reads the source HDR; the engine loads the prefiltered output.
    set_property(GLOBAL APPEND PROPERTY AILO_ASSET_BUILD_INPUTS ${INPUT_HDR})
endfunction()

add_radiance_map(ailo assets/textures/rogland_clear_night_4k.hdr)

# Copy source assets into the build folder, next to generated ones, so the executable can run from there.
# One rule per file: only new or changed files are copied. New files are picked up on the next build.
file(GLOB_RECURSE AILO_ASSET_FILES CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/assets/*)
get_property(AILO_ASSET_BUILD_INPUTS GLOBAL PROPERTY AILO_ASSET_BUILD_INPUTS)
if(AILO_ASSET_BUILD_INPUTS)
    list(REMOVE_ITEM AILO_ASSET_FILES ${AILO_ASSET_BUILD_INPUTS})
endif()

set(AILO_ASSET_OUTPUTS "")
foreach(ASSET IN LISTS AILO_ASSET_FILES)
    set(ASSET_OUTPUT "${CMAKE_BINARY_DIR}/${ASSET}")
    get_filename_component(ASSET_OUTPUT_DIR "${ASSET_OUTPUT}" DIRECTORY)
    add_custom_command(
            OUTPUT "${ASSET_OUTPUT}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${ASSET_OUTPUT_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/${ASSET}" "${ASSET_OUTPUT}"
            DEPENDS "${CMAKE_SOURCE_DIR}/${ASSET}"
            COMMENT "Copying ${ASSET}"
    )
    list(APPEND AILO_ASSET_OUTPUTS "${ASSET_OUTPUT}")
endforeach()

add_custom_target(ailo_assets ALL DEPENDS ${AILO_ASSET_OUTPUTS})
add_dependencies(ailo ailo_assets)

# Add shaders
add_shader(ailo shaders/shader.vert)
add_shader(ailo shaders/shader.frag)
add_shader(ailo shaders/imgui.vert)
add_shader(ailo shaders/imgui.frag)
add_shader(ailo shaders/hdr.vert)
add_shader(ailo shaders/hdr.frag)

add_material(ailo assets/materials/lit.mat)
add_material(ailo assets/materials/toon.mat)
add_material(ailo assets/materials/skybox.mat)


# Add executable
add_executable(ditest ailo/di/di_tests.cpp)

add_executable(apptest
        ailo/app/app_tests.cpp
        ailo/app/App.cpp
        ailo/ecs/Scene.cpp
)
target_link_libraries(apptest EnTT::EnTT)
target_include_directories(apptest PRIVATE ${CMAKE_SOURCE_DIR}/ailo)

add_executable(assetservertest
        ailo/assets/asset_server_tests.cpp
        ailo/assets/AssetServer.cpp
)
target_link_libraries(assetservertest EnTT::EnTT)
target_include_directories(assetservertest PRIVATE ${CMAKE_SOURCE_DIR}/ailo)

add_executable(texturetest ailo/render/texture/texture_tests.cpp)
target_link_libraries(texturetest ailo_texture stb_image)

add_executable(transformtest
        ailo/ecs/transform_tests.cpp
        ailo/ecs/Transform.cpp
        ailo/ecs/Hierarchy.cpp
)
target_link_libraries(transformtest EnTT::EnTT glm::glm)
target_include_directories(transformtest PRIVATE ${CMAKE_SOURCE_DIR}/ailo)
target_compile_definitions(transformtest PRIVATE GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)

add_executable(physicstest
        ailo/physics/physics_tests.cpp
        ailo/physics/PhysicsBodies.cpp
        ailo/physics/PhysicsPlugin.cpp
        ailo/physics/PhysicsWorld.cpp
        ailo/app/App.cpp
        ailo/ecs/Scene.cpp
        ailo/ecs/Transform.cpp
        ailo/ecs/Hierarchy.cpp
)
target_link_libraries(physicstest Jolt EnTT::EnTT glm::glm)
target_include_directories(physicstest PRIVATE ${CMAKE_SOURCE_DIR}/ailo)
target_compile_definitions(physicstest PRIVATE GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)
