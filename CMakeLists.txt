cmake_minimum_required(VERSION 3.22)
project(ailo)

set(CMAKE_CXX_STANDARD 23)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g")

# Find Vulkan package
find_package(Vulkan REQUIRED)

# Find glslc shader compiler
find_program(GLSLC glslc HINTS
    $ENV{VULKAN_SDK}/bin
    $ENV{VULKAN_SDK}/Bin
    ${Vulkan_GLSLC_EXECUTABLE}
)

if(NOT GLSLC)
    message(FATAL_ERROR "glslc shader compiler not found!")
endif()

# Compiles SHADER to <build>/shaders/<name>.<stage>.spv and appends that path to the list LIST_VAR.
function(add_shader LIST_VAR SHADER)
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

    list(APPEND ${LIST_VAR} ${SPIRV})
    set(${LIST_VAR} ${${LIST_VAR}} PARENT_SCOPE)
endfunction()

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

# Asset builds (build/README.md) run Ninja on manifests written by the scripts in build/, which run the tools named
# in config.json from AILO_ASSET_TOOLS_DIR. Set before the engine, whose tests embed assets.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(CMAKE_GENERATOR MATCHES "Ninja")
    set(AILO_NINJA ${CMAKE_MAKE_PROGRAM})
else()
    find_program(AILO_NINJA ninja REQUIRED)
endif()
set(AILO_ASSET_TOOLS texc matcomp modelc lightc)
set(AILO_ASSET_TOOLS_DIR ${CMAKE_BINARY_DIR}/asset_tools)

# Builds FILES like the asset build (build/gen.py) and compiles them into TARGET as a C++ package written by bin2cpp
# (tools/bin2cpp/README.md): "embedded/<PACKAGE>.h" declares a std::span<const uint8_t> per file in
# ailo::embedded::<PACKAGE>, named after the built file's path relative to ROOT ("materials/toon.mat" -> kMaterialsToon).
# FILES are relative to ROOT (an assets folder, default: the current source folder). PACKAGE names the generated files,
# so it must be unique in the build.
function(embed_assets TARGET)
    cmake_parse_arguments(EMB "" "PACKAGE;ROOT" "FILES" ${ARGN})
    if(NOT EMB_PACKAGE OR NOT EMB_FILES)
        message(FATAL_ERROR "embed_assets(${TARGET}) needs PACKAGE and FILES")
    endif()
    if(NOT EMB_ROOT)
        set(EMB_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
    cmake_path(ABSOLUTE_PATH EMB_ROOT BASE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} NORMALIZE)

    set(MANIFEST package_${EMB_PACKAGE}.ninja)
    set(OUT_DIR ${CMAKE_BINARY_DIR}/generated/embedded)
    set(HEADER ${OUT_DIR}/${EMB_PACKAGE}.h)
    set(SOURCE ${OUT_DIR}/${EMB_PACKAGE}.cpp)
    # Written only when it changes, so the manifest is regenerated only when FILES do.
    set(FILE_LIST ${CMAKE_BINARY_DIR}/package_build/${EMB_PACKAGE}/files.txt)
    list(JOIN EMB_FILES "\n" FILE_LIST_CONTENT)
    file(GENERATE OUTPUT ${FILE_LIST} CONTENT "${FILE_LIST_CONTENT}")

    add_custom_command(
            OUTPUT ${CMAKE_BINARY_DIR}/${MANIFEST}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/build/gen_package.py
                    --root ${EMB_ROOT}
                    --files ${FILE_LIST}
                    --build-dir ${CMAKE_BINARY_DIR}
                    --bin2cpp $<TARGET_FILE:bin2cpp>
                    --config ${CMAKE_SOURCE_DIR}/build/config.json
                    --tools-dir ${AILO_ASSET_TOOLS_DIR}
                    --package ${EMB_PACKAGE}
                    --namespace ailo::embedded::${EMB_PACKAGE}
                    --output ${OUT_DIR}
            DEPENDS ${CMAKE_SOURCE_DIR}/build/gen_package.py ${CMAKE_SOURCE_DIR}/build/gen.py
                    ${CMAKE_SOURCE_DIR}/build/config.json ${FILE_LIST}
            COMMENT "Generating ${MANIFEST}"
    )

    # Runs every build, like the asset build: only Ninja knows whether an asset changed. bin2cpp rewrites the header and
    # source only when their content changes, so an up-to-date package recompiles nothing.
    add_custom_target(embed_${EMB_PACKAGE}
            COMMAND ${AILO_NINJA} -f ${MANIFEST}
            COMMAND ${AILO_NINJA} -f ${MANIFEST} -t cleandead
            BYPRODUCTS ${HEADER} ${SOURCE}
            DEPENDS ${CMAKE_BINARY_DIR}/${MANIFEST}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Building embedded assets ${EMB_PACKAGE}"
            USES_TERMINAL
    )
    add_dependencies(embed_${EMB_PACKAGE} bin2cpp ${AILO_ASSET_TOOLS})

    target_sources(${TARGET} PRIVATE ${HEADER} ${SOURCE})
    target_include_directories(${TARGET} PRIVATE ${CMAKE_BINARY_DIR}/generated)
    add_dependencies(${TARGET} embed_${EMB_PACKAGE})
endfunction()

# The engine
add_subdirectory(ailo)

add_subdirectory(tools/json)
add_subdirectory(tools/matcomp)
add_subdirectory(tools/texc)
add_subdirectory(tools/modelc)
add_subdirectory(tools/lightc)
add_subdirectory(tools/bin2cpp)

# The generator expression keeps multi-config generators from adding a per-config subfolder.
set_target_properties(${AILO_ASSET_TOOLS} PROPERTIES RUNTIME_OUTPUT_DIRECTORY $<1:${AILO_ASSET_TOOLS_DIR}>)

# The demo game
add_subdirectory(sandbox)
