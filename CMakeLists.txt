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

# Only the offline model compiler (tools/modelc) links it.
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(third_party/assimp)
unset(BUILD_SHARED_LIBS)
# On MinGW assimp forces "-static-libstdc++ -Wl,-Bstatic -lstdc++ -lwinpthread" onto its consumers. The static
# libwinpthread.a in CLion's msvcrt toolchain references the UCRT-only __intrinsic_setjmpex, so linking modelc fails.
# Drop those flags and link the runtime like every other target.
if (MINGW)
    get_target_property(_assimp_link_libs assimp INTERFACE_LINK_LIBRARIES)
    list(FILTER _assimp_link_libs EXCLUDE REGEX "-static-lib|-Wl,-Bstatic|-lstdc\\+\\+|-lwinpthread")
    set_target_properties(assimp PROPERTIES INTERFACE_LINK_LIBRARIES "${_assimp_link_libs}")
endif ()

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

add_subdirectory(libs/fileio)

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

# Model package format, shared by the engine and the offline model compiler.
add_library(ailo_model STATIC
    ailo/render/model/ModelPackage.cpp
    ailo/render/model/ModelPackage.h
)
target_include_directories(ailo_model PUBLIC ${CMAKE_SOURCE_DIR}/ailo)
target_link_libraries(ailo_model PUBLIC glm::glm ailo_texture)

# Lighting package format (IBL baked by lightc), shared by the engine and lightc.
add_library(ailo_lighting STATIC
    ailo/render/lighting/LightingPackage.cpp
    ailo/render/lighting/LightingPackage.h
)
target_include_directories(ailo_lighting PUBLIC ${CMAKE_SOURCE_DIR}/ailo)
target_link_libraries(ailo_lighting PUBLIC glm::glm ailo_texture)

add_subdirectory(tools/json)
add_subdirectory(tools/matcomp)
add_subdirectory(tools/texc)
add_subdirectory(tools/modelc)
add_subdirectory(tools/lightc)

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
    ailo/render/IndirectLight.cpp
    ailo/render/IndirectLight.h
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
        ailo/render/ShadowMapManager.cpp
        ailo/render/ShadowMapManager.h
        ailo/common/AABB.h
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
        Jolt
        ailo_material
        ailo_model
        ailo_lighting
        ailo_texture
        ailo_fileio
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

# The DFG LUT depends on no asset, so lightc generates it here instead of in the asset build.
set(DFG_LUT_OUTPUT ${CMAKE_BINARY_DIR}/assets/textures/dfg_lut.texpack)
add_custom_command(
        OUTPUT ${DFG_LUT_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/assets/textures/"
        COMMAND $<TARGET_FILE:lightc> --dfg -o ${DFG_LUT_OUTPUT}
        DEPENDS lightc
        COMMENT "Generating DFG LUT texture"
)
target_sources(ailo PRIVATE ${DFG_LUT_OUTPUT})

# Offline asset build (build/README.md): gen.py writes build_assets.ninja, which compiles every asset in
# assets/ with the tool its extension maps to in config.json and copies everything else. CMake only generates the
# manifest the first time; after that it regenerates itself when assets are added or removed.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(CMAKE_GENERATOR MATCHES "Ninja")
    set(AILO_NINJA ${CMAKE_MAKE_PROGRAM})
else()
    find_program(AILO_NINJA ninja REQUIRED)
endif()

set(AILO_ASSET_MANIFEST ${CMAKE_BINARY_DIR}/build_assets.ninja)
add_custom_command(
        OUTPUT ${AILO_ASSET_MANIFEST}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/build/gen.py
                --source ${CMAKE_SOURCE_DIR}/assets
                --build-dir ${CMAKE_BINARY_DIR}
                --config ${CMAKE_SOURCE_DIR}/build/config.json
                --cmake ${CMAKE_COMMAND}
                --tool texc=$<TARGET_FILE:texc>
                --tool matcomp=$<TARGET_FILE:matcomp>
                --tool modelc=$<TARGET_FILE:modelc>
                --tool lightc=$<TARGET_FILE:lightc>
        DEPENDS ${CMAKE_SOURCE_DIR}/build/gen.py ${CMAKE_SOURCE_DIR}/build/config.json
        COMMENT "Generating build_assets.ninja"
)

# cleandead removes the outputs of assets that no longer exist. -f takes the manifest's path relative to the build
# folder: ninja regenerates it only if the path matches its build edge (build_assets.ninja) exactly.
add_custom_target(ailo_assets ALL
        COMMAND ${AILO_NINJA} -f build_assets.ninja
        COMMAND ${AILO_NINJA} -f build_assets.ninja -t cleandead
        DEPENDS ${AILO_ASSET_MANIFEST}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Building assets"
        USES_TERMINAL
)
add_dependencies(ailo_assets texc matcomp modelc lightc)
add_dependencies(ailo ailo_assets)

# Add shaders
add_shader(ailo shaders/imgui.vert)
add_shader(ailo shaders/imgui.frag)
add_shader(ailo shaders/tonemap.vert)
add_shader(ailo shaders/tonemap.frag)

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

add_executable(modeltest ailo/render/model/model_tests.cpp)
target_link_libraries(modeltest ailo_model)

# Also checks lightc's SH projection, whose output the package format defines.
add_executable(lightingtest
        ailo/render/lighting/lighting_tests.cpp
        tools/lightc/src/SphericalHarmonics.cpp
)
target_link_libraries(lightingtest ailo_lighting)
target_include_directories(lightingtest PRIVATE ${CMAKE_SOURCE_DIR}/tools/lightc/src)

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
