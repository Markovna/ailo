# Embeds FILES into TARGET (tools/resgen/README.md). Each file is declared in "resources/<PACKAGE>.h" as
# std::span<const uint8_t> ailo::resources::embedded::k<Name>, named after the file ("tonemap.vert.spv" -> kTonemapVert).
function(embed_resources TARGET)
    cmake_parse_arguments(RES "" "PACKAGE" "FILES" ${ARGN})

    set(OUT_DIR ${CMAKE_BINARY_DIR}/generated/resources)
    set(HEADER ${OUT_DIR}/${RES_PACKAGE}.h)
    set(SOURCE ${OUT_DIR}/${RES_PACKAGE}.cpp)

    add_custom_command(
            OUTPUT ${HEADER} ${SOURCE}
            COMMAND $<TARGET_FILE:resgen> --package ${RES_PACKAGE} -o ${OUT_DIR} ${RES_FILES}
            DEPENDS resgen ${RES_FILES}
            COMMENT "Embedding resources ${RES_PACKAGE}"
    )

    target_sources(${TARGET} PRIVATE ${HEADER} ${SOURCE})
    target_include_directories(${TARGET} PRIVATE ${CMAKE_BINARY_DIR}/generated)
endfunction()

# Package formats, shared by the engine and the offline asset compilers (tools/).
add_library(ailo_material STATIC
    render/material/MaterialEnums.cpp
    render/material/MaterialEnums.h
    render/material/MaterialPackage.cpp
    render/material/MaterialPackage.h
    render/material/Variant.h
)
target_include_directories(ailo_material PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(ailo_texture STATIC
    render/texture/TexturePackage.cpp
    render/texture/TexturePackage.h
    render/texture/TexturePacker.cpp
    render/texture/TexturePacker.h
)
target_include_directories(ailo_texture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ailo_texture PRIVATE stb_image)

add_library(ailo_model STATIC
    render/model/ModelPackage.cpp
    render/model/ModelPackage.h
)
target_include_directories(ailo_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ailo_model PUBLIC glm::glm ailo_texture)

# IBL baked by lightc.
add_library(ailo_lighting STATIC
    render/lighting/LightingPackage.cpp
    render/lighting/LightingPackage.h
)
target_include_directories(ailo_lighting PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ailo_lighting PUBLIC glm::glm ailo_texture)

# The engine. Games link it and get its include dirs, defines and public dependencies.
add_library(ailo STATIC
    render/RenderAPI.cpp
    render/RenderAPI.h
    render/ImGuiRenderer.cpp
    render/ImGuiRenderer.h
    input/InputSystem.cpp
    input/InputSystem.h
    input/InputTypes.h
    render/vulkan/VulkanUtils.cpp
    render/vulkan/VulkanUtils.h
    render/RenderPrimitive.cpp
    render/RenderPrimitive.h
    render/Renderer.cpp
    render/Renderer.h
    ecs/Scene.cpp
    ecs/Scene.h
    utils/Utils.h
    OS.cpp
    OS.h
    render/Material.cpp
    render/Material.h
    render/MaterialInstance.cpp
    render/MaterialInstance.h
    render/Skybox.h
    render/Texture.cpp
    render/Texture.h
    render/Mesh.h
    render/Model.cpp
    render/Model.h
    render/IndirectLight.cpp
    render/IndirectLight.h
    ecs/Transform.h
    ecs/Transform.cpp
    ecs/Hierarchy.h
    ecs/Hierarchy.cpp
    render/CommandBuffer.cpp
    render/DeletionQueue.cpp
    render/DeletionQueue.h
    render/StagePool.cpp
    render/StagePool.h
    render/GpuTimer.cpp
    render/GpuTimer.h
    render/CommandBuffer.h
    render/SwapChain.cpp
    render/SwapChain.h
    render/FrameBufferCache.cpp
    render/FrameBufferCache.h
    render/VulkanConstants.h
    render/VulkanDevice.cpp
    render/VulkanDevice.h
    render/Constants.h
    render/ResourceContainer.h
    render/vulkan/Texture.cpp
    render/vulkan/Texture.h
    render/vulkan/DescriptorSet.cpp
    render/vulkan/DescriptorSet.h
    common/LRUCache.h
    render/RenderPassCache.cpp
    render/RenderPassCache.h
    render/SamplerCache.cpp
    render/SamplerCache.h
    render/PipelineCache.cpp
    render/PipelineCache.h
    render/Program.cpp
    render/Program.h
    render/vulkan/Resources.h
    ecs/SceneLighting.cpp
    ecs/SceneLighting.h
    ecs/Lights.h
    ecs/Camera.cpp
    ecs/Camera.h
    render/Renderable.cpp
    render/Renderable.h
    render/Skin.h
    render/Animation.h
    render/Skeleton.cpp
    render/Skeleton.h
    ecs/AnimatorComponent.h
    ecs/AnimationPlugin.cpp
    ecs/AnimationPlugin.h
    ecs/Spawn.cpp
    ecs/Spawn.h
    platform/Platform.cpp
    platform/Platform.h
    di/di.h
    app/App.cpp
    app/App.h
    app/System.h
    app/World.h
    assets/AssetPlugin.h
    platform/PlatformPlugin.cpp
    platform/PlatformPlugin.h
    render/RenderPlugin.cpp
    render/RenderPlugin.h
    ui/ImGuiPlugin.cpp
    ui/ImGuiPlugin.h
    ui/OrbitCameraPlugin.cpp
    ui/OrbitCameraPlugin.h
    physics/PhysicsBodies.cpp
    physics/PhysicsBodies.h
    physics/PhysicsComponents.h
    physics/PhysicsLayers.h
    physics/PhysicsMath.h
    physics/PhysicsPlugin.cpp
    physics/PhysicsPlugin.h
    physics/PhysicsWorld.cpp
    physics/PhysicsWorld.h
    assets/AssetReader.h
    assets/AssetReader.cpp
    assets/AssetServer.h
    assets/AssetServer.cpp
    render/DefaultAssets.h
    render/DefaultAssetFactory.cpp
    render/DefaultAssetFactory.h
    render/ShadowMapManager.cpp
    render/ShadowMapManager.h
    common/AABB.h
)

# PUBLIC: libraries whose headers the engine's headers include.
target_link_libraries(ailo
    PUBLIC
        Vulkan::Vulkan
        glfw
        glm::glm
        GPUOpen::VulkanMemoryAllocator
        EnTT::EnTT
        imgui
        Jolt
        ailo_material
        ailo_lighting
    PRIVATE
        stb_image
        ailo_model
        ailo_texture
        ailo_fileio
)

target_include_directories(ailo PUBLIC
    ${Vulkan_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# PUBLIC so that games see the same GLM/Vulkan configuration as the engine (GLM_FORCE_DEPTH_ZERO_TO_ONE changes the
# projection matrices).
target_compile_definitions(ailo PUBLIC
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)

target_compile_definitions(ailo PUBLIC GLFW_INCLUDE_VULKAN)

target_compile_definitions(ailo PUBLIC VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS)

# VK_KHR_portability_subset (MoltenVK) is declared in vulkan_beta.h
target_compile_definitions(ailo PUBLIC VK_ENABLE_BETA_EXTENSIONS)

#target_compile_definitions(ailo PRIVATE NDEBUG)

# The DFG LUT is a built-in texture (textures::kDfgLut), embedded into the engine.
set(DFG_LUT_OUTPUT ${CMAKE_BINARY_DIR}/generated/dfg_lut.texpack)
add_custom_command(
        OUTPUT ${DFG_LUT_OUTPUT}
        COMMAND $<TARGET_FILE:lightc> --dfg -o ${DFG_LUT_OUTPUT}
        DEPENDS lightc
        COMMENT "Generating DFG LUT texture"
)
embed_resources(ailo PACKAGE builtin_textures FILES ${DFG_LUT_OUTPUT})

# Built-in shaders, embedded into the engine
add_shader(BUILTIN_SHADERS shaders/imgui.vert)
add_shader(BUILTIN_SHADERS shaders/imgui.frag)
add_shader(BUILTIN_SHADERS shaders/tonemap.vert)
add_shader(BUILTIN_SHADERS shaders/tonemap.frag)
embed_resources(ailo PACKAGE builtin_shaders FILES ${BUILTIN_SHADERS})

# Tests
add_executable(ditest di/di_tests.cpp)

add_executable(apptest
        app/app_tests.cpp
        app/App.cpp
        ecs/Scene.cpp
)
target_link_libraries(apptest EnTT::EnTT)
target_include_directories(apptest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(assetservertest
        assets/asset_server_tests.cpp
        assets/AssetServer.cpp
)
target_link_libraries(assetservertest EnTT::EnTT)
target_include_directories(assetservertest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(texturetest render/texture/texture_tests.cpp)
target_link_libraries(texturetest ailo_texture stb_image)

add_executable(modeltest render/model/model_tests.cpp)
target_link_libraries(modeltest ailo_model)

# Also checks lightc's SH projection, whose output the package format defines.
add_executable(lightingtest
        render/lighting/lighting_tests.cpp
        ${CMAKE_SOURCE_DIR}/tools/lightc/src/SphericalHarmonics.cpp
)
target_link_libraries(lightingtest ailo_lighting)
target_include_directories(lightingtest PRIVATE ${CMAKE_SOURCE_DIR}/tools/lightc/src)

add_executable(transformtest
        ecs/transform_tests.cpp
        ecs/Transform.cpp
        ecs/Hierarchy.cpp
)
target_link_libraries(transformtest EnTT::EnTT glm::glm)
target_include_directories(transformtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(transformtest PRIVATE GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)

add_executable(physicstest
        physics/physics_tests.cpp
        physics/PhysicsBodies.cpp
        physics/PhysicsPlugin.cpp
        physics/PhysicsWorld.cpp
        app/App.cpp
        ecs/Scene.cpp
        ecs/Transform.cpp
        ecs/Hierarchy.cpp
)
target_link_libraries(physicstest Jolt EnTT::EnTT glm::glm)
target_include_directories(physicstest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(physicstest PRIVATE GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)
