cmake_minimum_required(VERSION 3.10.0)
project(Muyo)

# Root of this project, correct whether it is the top-level build or a subdirectory.
# CMAKE_SOURCE_DIR / CMAKE_BINARY_DIR resolve to the *consumer's* root when this project is
# embedded, which silently points every path built from them at the wrong tree (#68).
set(MUYO_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(MUYO_BINARY_ROOT "${CMAKE_CURRENT_BINARY_DIR}")

# True when this project is the root of the build. Everything guarded by it - the demo app, the PSO
# compiler, the test suite, the docs check - is for developing this project rather than consuming it,
# and the guard is what keeps the Catch2 fetch, a network dependency, out of an embedder's configure.
# PROJECT_IS_TOP_LEVEL is the idiomatic spelling but needs CMake 3.21; this file declares 3.10, where
# it would silently evaluate to false and the tests would quietly never be built.
set(MUYO_TOP_LEVEL FALSE)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(MUYO_TOP_LEVEL TRUE)
endif()
# Compile time features
option (FEATURE_RAY_TRACING "Compile with ray tracing feature" off)
option (FEATURE_SYNCHRONIZATION2 "Compile with ray tracing feature" off)
option (FEATURE_USE_SDL "Use SDL as window system" off)
if (${FEATURE_RAY_TRACING})
    add_compile_definitions(FEATURE_RAY_TRACING)
endif()

if (${FEATURE_SYNCHRONIZATION2})
    add_compile_definitions(FEATURE_SYNCHRONIZATION2)
endif()

# MUYO_BINARY_ROOT rather than CMAKE_BINARY_DIR: when embedded, the latter is the consumer's build
# root, so Muyo's artefacts - and the generated shaders the tests read - would be scattered into it
# (#68). At top level the two are the same directory, so nothing changes for this build.
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${MUYO_BINARY_ROOT})
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${MUYO_BINARY_ROOT}")
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${MUYO_BINARY_ROOT}")
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${MUYO_BINARY_ROOT}")
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${MUYO_BINARY_ROOT}")
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${MUYO_BINARY_ROOT}")
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${MUYO_BINARY_ROOT}")
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)

## GLFW
# Look for precompiled glfw library
find_package(glfw3 3.2 QUIET)
# Otherwise, build from source
if (NOT ${glfw3_FOUND})
# Build glfw from source
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
execute_process(COMMAND bash -c "echo $WAYLAND_DISPLAY"
                OUTPUT_VARIABLE WAYLAND_SESSION
                OUTPUT_STRIP_TRAILING_WHITESPACE)

if(WAYLAND_SESSION)
    message(STATUS "Wayland session detected")
    set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 OFF CACHE BOOL "" FORCE)
endif()
add_subdirectory(${MUYO_ROOT}/thirdparty/glfw)
endif()
add_compile_definitions(USE_GLFW)
set(WINDOW_LIB glfw)

## GLM
find_package(glm QUIET)
if (NOT ${glm_FOUND})
    set(GLM_INCLUDE_DIRS ${MUYO_ROOT}/thirdparty/glm)
endif()

## imgui
set(IMGUI_INCLUDE_DIRS ${MUYO_ROOT}/thirdparty/imgui)
add_library(imgui
    thirdparty/imgui/imgui.cpp
    thirdparty/imgui/imgui_demo.cpp
    thirdparty/imgui/imgui_draw.cpp
    thirdparty/imgui/imgui_widgets.cpp
    thirdparty/imgui/imgui_tables.cpp
    )
# Use a project-local imgui config (ImTextureID_Invalid override) instead of editing the submodule.
target_include_directories(imgui PRIVATE ${MUYO_ROOT}/src)
target_compile_definitions(imgui PRIVATE IMGUI_USER_CONFIG="ImGuiUserConfig.h")

set(SPIRV-Reflect_INCLUDE_DIRS ${MUYO_ROOT}/thirdparty/SPIRV-Reflect)
add_library(SPIRV-Reflect ${MUYO_ROOT}/thirdparty/SPIRV-Reflect/spirv_reflect.cpp)

## ImGuizmo
set(IMGUIZMO_INCLUDE_DIRS ${MUYO_ROOT}/thirdparty/ImGuizmo/src)
add_library(imguizmo ${IMGUIZMO_INCLUDE_DIRS}/ImGuizmo.cpp)
#add_library(ImGuizmo STATIC
#    ${IMGUIZMO_INCLUDE_DIRS}/ImGuizmo.cpp
#    # Include these if you are also using the curve/gradient/graph editors
#    ${IMGUIZMO_INCLUDE_DIRS}/ImCurveEdit.cpp
#    ${IMGUIZMO_INCLUDE_DIRS}/ImGradient.cpp
#    ${IMGUIZMO_INCLUDE_DIRS}/ImSequencer.cpp
#    ${IMGUIZMO_INCLUDE_DIRS}/GraphEditor.cpp
#)

## imnodes
set(IMNODES_INCLUDE_DIRS ${MUYO_ROOT}/thirdparty/imnodes)
add_library(imnodes thirdparty/imnodes/imnodes.cpp)

## VULKAN
if (NOT ENV{VULKAN_SDK} AND EXISTS ENV{VULKAN_SDK})
    list(APPEND CMAKE_PREFIX_PATH $ENV{VULKAN_SDK})
endif()

# Cap vulkan version to avoid validation layer error
find_package(Vulkan REQUIRED)


## tiny obj
include_directories(${MUYO_ROOT}/thirdparty/tinyobjloader)
add_library(tinyobj ${MUYO_ROOT}/thirdparty/tinyobjloader/tiny_obj_loader.cc)

## tiny gltf
include_directories(${MUYO_ROOT}/thirdparty/tinygltf)
add_library(tinygltf ${MUYO_ROOT}/thirdparty/tinygltf/tiny_gltf.cc)

## stb images
include_directories(${MUYO_ROOT}/thirdparty/stb)
add_library(stb ${MUYO_ROOT}/thirdparty/stb/stb.cpp)

## meshoptimizer
add_subdirectory(${MUYO_ROOT}/thirdparty/meshoptimizer)
include_directories(${MUYO_ROOT}/thirdparty/meshoptimizer/src)

if(NOT EXISTS ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE})
    message(FATAL_ERROR "Can't find glslangValidator at ${GLSL_VALIDATOR}")
endif()

## Find slang, this should later be included in official cmake script
if (NOT EXISTS ${Vulkan_SLANG_EXECUTABLE})
  set(_Vulkan_hint_executable_search_paths "$ENV{VULKAN_SDK}/bin")
  find_program(Vulkan_SLANG_EXECUTABLE
    NAMES slangc
    HINTS
      ${_Vulkan_hint_executable_search_paths}
    )
  unset(_Vulkan_hint_executable_search_paths)
endif()


if (NOT EXISTS ${Vulkan_SLANG_EXECUTABLE})
    message(FATAL_ERROR "Can't find slang executable")
endif()

set(SANITIZE_CONDITION "$<CONFIG:Sanitize>")
if (UNIX)
    # The hardcoded -std=c++20 stays, because CMAKE_CXX_FLAGS applies to every target in this
    # directory at generate time and moving it would change what the third-party targets compile as.
    # The language level is additionally declared as a target interface requirement, which is what
    # crosses a project boundary - a consumer previously compiled this project's headers as C++17 and
    # failed on std::source_location (#68). CMAKE_CXX_EXTENSIONS OFF keeps the feature mapping to the
    # same strict flag, so nothing about this build changes.
    set (CMAKE_CXX_EXTENSIONS OFF)
    set (CMAKE_CXX_FLAGS "-std=c++20 -Wall -pthread -Wno-missing-braces -g")
    set (CMAKE_CXX_DEBUG_FLAGS ${CMAKE_CXX_FLAGS} "")
    set (CMAKE_CXX_RELEASE_FLAGS ${CMAKE_CXX_FLAGS} "-O3")
    set(SANITIZE_FLAGS
      "$<${SANITIZE_CONDITION}:-fsanitize=address,undefined>"
      "$<${SANITIZE_CONDITION}:-fno-omit-frame-pointer>"
    )
elseif(MSVC)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest /W3")
    set(SANITIZE_FLAGS "$<${SANITIZE_CONDITION}:/fsanitize=address,undefined,thread,memory>")
    add_compile_options("$<${SANITIZE_CONDITION}:/Zi>")
endif()

add_compile_options(${SANITIZE_FLAGS})
add_link_options(${SANITIZE_FLAGS})

# Headers that shared between shader and cpp 
set (SHARED_HEADER_DIR "${MUYO_ROOT}/shaders/shared")

include_directories(
    ${Vulkan_INCLUDE_DIR}
    ${GLFW3_INCLUDE_DIR}
    ${GLM_INCLUDE_DIRS}
    ${IMGUI_INCLUDE_DIRS}
    ${SHARED_HEADER_DIR}
    ${IMGUIZMO_INCLUDE_DIRS}
    ${IMNODES_INCLUDE_DIRS}
    ${SPIRV-Reflect_INCLUDE_DIRS}
    )

    
#Enable CPPCHECK
# set(CMAKE_CXX_CPPCHECK "cppcheck")

include_directories(
    "src/RenderDevice"
    "src/RenderPasses"
    "src/RenderResources"
    "src/Scene"
    "src/Debug"
    "src"
    )

file(GLOB muyo_SRC
    "src/*.h"
    "src/*.cpp"
    "src/Debug/*.h"
    "src/Debug/*.cpp"
    "src/RenderDevice/*.h"
    "src/RenderDevice/*.cpp"
    "src/RenderPasses/*.h"
    "src/RenderPasses/*.cpp"
    "src/RenderResources/*.h"
    "src/RenderResources/*.cpp"
    "src/Scene/*.h"
    "src/Scene/*.cpp"
    )

add_library(muyo STATIC ${muyo_SRC})

add_library(muyo_rg
    "src/RenderGraph/RenderGraphBuilder.cpp"
    "src/RenderGraph/CompiledRenderGraph.cpp"
    "src/RenderGraph/RenderGraphExecutor.cpp"
)

#fetch VMA from Github
include(FetchContent)
FetchContent_Declare(
  VMA
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  GIT_TAG master
)
FetchContent_MakeAvailable(VMA)
target_compile_features(muyo PUBLIC cxx_std_20)
target_compile_features(muyo_rg PUBLIC cxx_std_20)

# PUBLIC interface, so a consumer of these targets can compile against them. The include_directories()
# calls above are directory-scoped and do not propagate across a project boundary, which is why
# linking muyo_rg previously exported no includes at all (#68). The INSTALL_INTERFACE half of these
# belongs with the install rules, where the installed layout is decided (#73).
target_include_directories(muyo
    PUBLIC
        $<BUILD_INTERFACE:${MUYO_ROOT}/src>
        # The engine's own headers include each other by bare name ("Geometry.h"), so every
        # directory the top-level include_directories() lists has to be part of the interface too.
        # That these are needed at all is #53's finding arriving from the packaging side: the
        # surface is not a surface, it is the whole tree.
        $<BUILD_INTERFACE:${MUYO_ROOT}/src/RenderDevice>
        $<BUILD_INTERFACE:${MUYO_ROOT}/src/RenderPasses>
        $<BUILD_INTERFACE:${MUYO_ROOT}/src/RenderResources>
        $<BUILD_INTERFACE:${MUYO_ROOT}/src/Scene>
        $<BUILD_INTERFACE:${MUYO_ROOT}/src/Debug>
        $<BUILD_INTERFACE:${SHARED_HEADER_DIR}>
        $<BUILD_INTERFACE:${GLM_INCLUDE_DIRS}>
        $<BUILD_INTERFACE:${IMGUI_INCLUDE_DIRS}>
        $<BUILD_INTERFACE:${IMGUIZMO_INCLUDE_DIRS}>
        $<BUILD_INTERFACE:${IMNODES_INCLUDE_DIRS}>
        $<BUILD_INTERFACE:${SPIRV-Reflect_INCLUDE_DIRS}>
        $<BUILD_INTERFACE:${MUYO_ROOT}/thirdparty/tinyobjloader>
        $<BUILD_INTERFACE:${MUYO_ROOT}/thirdparty/tinygltf>
        $<BUILD_INTERFACE:${MUYO_ROOT}/thirdparty/stb>
        $<BUILD_INTERFACE:${MUYO_ROOT}/thirdparty/meshoptimizer/src>
        $<BUILD_INTERFACE:${vma_SOURCE_DIR}/include>
    )

# RenderGraphResourceDesc.h uses VmaMemoryUsage in a public struct, so VMA is part of muyo_rg's
# interface whether or not that is desirable - a consumer cannot compile the public surface without it.
# #53 removes VMA from the surface; until then it has to be reachable.
target_include_directories(muyo_rg PUBLIC $<BUILD_INTERFACE:${vma_SOURCE_DIR}/include>)

target_link_libraries(muyo imgui imguizmo imnodes ${Vulkan_LIBRARIES} ${WINDOW_LIB} VulkanMemoryAllocator stb tinygltf tinyobj meshoptimizer)

# muyo_rg does not stand alone today: it reaches into the engine for resource managers, the device
# singleton and the shader reflection service. Declared rather than implied, so the coupling is visible
# where a consumer meets it - and so #53 has one line to change when those become injected services.
target_link_libraries(muyo_rg PUBLIC muyo)

file(GLOB main_SRC
    "src/app/helloVulkan.cpp"
    )

file(GLOB PSOCompiler_SRC
    "src/app/PSOCompiler.cpp"
    )

if (MUYO_TOP_LEVEL)
add_executable(helloVulkan ${main_SRC})
target_link_libraries(helloVulkan PUBLIC muyo)


add_executable(PSOCompiler ${PSOCompiler_SRC})
target_link_libraries(PSOCompiler PUBLIC muyo)
endif()


# Build glsl shaders
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${MUYO_ROOT}/shaders/*.frag"
    "${MUYO_ROOT}/shaders/*.vert"
    "${MUYO_ROOT}/shaders/*.rgen"
    "${MUYO_ROOT}/shaders/*.rmiss"
    "${MUYO_ROOT}/shaders/*.rchit"
    "${MUYO_ROOT}/shaders/*.comp"
    )

file(GLOB_RECURSE GLSL_HEADER_FILES
    "${MUYO_ROOT}/shaders/*.h"
    "${SHARED_HEADER_DIR}/*.h"
    )


message(STATUS "GLSL shaders: ${GLSL_SOURCE_FILES}")
foreach (GLSL_SOURCE_FILE ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL_SOURCE_FILE} NAME)
    set(SPIRV "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${FILE_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/"
        COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} --target-env vulkan1.3 -DSHADER_CODE -g -V ${GLSL_SOURCE_FILE} -o ${SPIRV}
        DEPENDS ${GLSL_SOURCE_FILE} ${GLSL_HEADER_FILES}) # Header change will trigger a full shader rebuild, which might not be necessary
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()

# Build slang shaders
file (GLOB_RECURSE SLANG_SOURCE_FILES "${MUYO_ROOT}/shaders/*.slang")
message(STATUS "Slang Shaders: ${SLANG_SOURCE_FILES}")
foreach (SLANG_SOURCE_FILE ${SLANG_SOURCE_FILES})
    get_filename_component(FILE_NAME ${SLANG_SOURCE_FILE} NAME)
    set(SPIRV "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${FILE_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/"
        #hello-world.slang -profile glsl_450 -target spirv -o hello-world.spv -entry computeMain
        COMMAND ${Vulkan_SLANG_EXECUTABLE} ${SLANG_SOURCE_FILE} -DSHADER_CODE -DSLANG -capability spirv_1_4 -profile glsl_450 -target spirv -o ${SPIRV} -entry main
        DEPENDS ${SLANG_SOURCE_FILE}# ${GLSL_HEADER_FILES}) # Header change will trigger a full shader rebuild, which might not be necessary
        )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()





add_custom_target(
    Shaders
    DEPENDS ${SPIRV_BINARY_FILES}
    )

if (MUYO_TOP_LEVEL)
add_dependencies(helloVulkan Shaders)

# Copy over asset
add_custom_command(
    TARGET helloVulkan
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${MUYO_ROOT}/assets ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets
    )
endif()
message(STATUS "runtime dir" ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# Everything from here to the end of the docs block is a development concern: it declares targets a
# consumer has no reason to build and, in the case of Catch2, fetches one over the network. Kept out
# of an embedder's configure entirely (#68 review A1). The Shaders custom target stays unguarded -
# it is a build dependency with no network cost, and a consumer may want this project's shaders.
if (MUYO_TOP_LEVEL)

# Tests

enable_testing()
include(CTest)

Include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.0 # or a later release
)

FetchContent_MakeAvailable(Catch2)
add_executable(tests 
    src/ShaderReflectionFetcher.cpp
    src/RenderGraph/RenderGraphBuilder.cpp
    src/RenderGraph/CompiledRenderGraph.cpp
    src/RenderGraph/RenderGraphExecutor.cpp
    src/tests/DebugCallbackTests.cpp
    src/tests/DeviceCapabilityTests.cpp
    src/tests/RenderGraphBuilderTest.cpp
    src/tests/ShaderReflectionTests.cpp
)
message(STATUS "Catch2 source dir: ${catch2_SOURCE_DIR}")
target_include_directories(tests PRIVATE ${catch2_SOURCE_DIR}/src thirdparty/RenderDoc)
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain SPIRV-Reflect muyo muyo_rg)
include(Catch)
catch_discover_tests(tests)

# Keep ImTextureID_Invalid consistent (see src/ImGuiUserConfig.h) across all imgui consumers.
target_compile_definitions(muyo PRIVATE IMGUI_USER_CONFIG="ImGuiUserConfig.h")
if (MUYO_TOP_LEVEL)
target_compile_definitions(helloVulkan PRIVATE IMGUI_USER_CONFIG="ImGuiUserConfig.h")
target_compile_definitions(PSOCompiler PRIVATE IMGUI_USER_CONFIG="ImGuiUserConfig.h")
target_compile_definitions(tests PRIVATE IMGUI_USER_CONFIG="ImGuiUserConfig.h")
endif()

# ── RenderGraph API documentation ────────────────────────────────────────────
# Doxygen + Python are optional. When both are present, `render_graph_docs`
# regenerates docs/RenderGraph-api.md and fails if any public RenderGraph entity
# is undocumented; CTest runs the same check via the RenderGraphApiDocs test.
# See docs/README.md.
find_program(DOXYGEN_EXECUTABLE
    NAMES doxygen
    HINTS ${MUYO_ROOT}/.tools/doxygen/bin ${MUYO_ROOT}/.tools/doxygen/usr/bin)
find_package(Python3 COMPONENTS Interpreter QUIET)

if(DOXYGEN_EXECUTABLE AND Python3_Interpreter_FOUND)
    set(RENDER_GRAPH_DOCS_SCRIPT ${MUYO_ROOT}/scripts/render_graph_docs.py)
    set(RENDER_GRAPH_DOCS_DIR ${CMAKE_BINARY_DIR}/docs/render_graph)
    add_custom_target(render_graph_docs
        COMMAND ${CMAKE_COMMAND} -E env DOXYGEN=${DOXYGEN_EXECUTABLE}
                ${Python3_EXECUTABLE} ${RENDER_GRAPH_DOCS_SCRIPT} --output-dir ${RENDER_GRAPH_DOCS_DIR}
        WORKING_DIRECTORY ${MUYO_ROOT}
        COMMENT "Generating RenderGraph API documentation (fails on undocumented API)"
        VERBATIM)
    add_test(NAME RenderGraphApiDocs
        COMMAND ${CMAKE_COMMAND} -E env DOXYGEN=${DOXYGEN_EXECUTABLE}
                ${Python3_EXECUTABLE} ${RENDER_GRAPH_DOCS_SCRIPT} --check --output-dir ${RENDER_GRAPH_DOCS_DIR})
else()
    message(STATUS "RenderGraph docs disabled (doxygen/Python3 not found); see docs/README.md")
endif()

endif() # MUYO_TOP_LEVEL: tests, demo, PSO compiler, docs check

# Disable tests for now until proper method has been found to load the gltf
#add_executable(testSceneImporter
#    src/Geometry.cpp
#    src/SceneImporter.cpp
#    src/Scene.cpp
#
#    src/tests/testSceneImporter.cpp
#)

#target_link_libraries(testSceneImporter tinygltf stb)

#add_custom_command(TARGET helloVulkan POST_BUILD
#    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:helloVulkan>/shaders/"
#    COMMAND ${CMAKE_COMMAND} -E copy_directory
#    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
#    "$<TARGET_FILE_DIR:helloVulkan>/shaders"
#    )
#
