cmake_minimum_required(VERSION 3.10.0)
# The language level is a directory default declared before project(), so every later-created target -
# including the third-party subdirectories - inherits C++20, and CMAKE_CXX_FLAGS does not have to
# carry it. CMAKE_CXX_EXTENSIONS OFF maps to the strict flag, as before (#68).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
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
# ThreadSanitizer is a configuration of its own, not another flag for Sanitize: TSan and ASan cannot be
# linked into one binary, and the two find different classes of defect. The flags are generator
# expressions rather than CMAKE_CXX_FLAGS_THREAD so they behave the same on a single-config generator
# regardless of how the configuration happened to be named. -DNDEBUG is deliberately absent, as in
# Debug: a check that a release build removes is not the thing being verified.
set(THREAD_FLAGS "")
if (UNIX)
    set(THREAD_FLAGS
      "$<$<CONFIG:Thread>:-fsanitize=thread>"
      "$<$<CONFIG:Thread>:-fno-omit-frame-pointer>"
      # An unknown CMAKE_BUILD_TYPE gets no per-configuration defaults at all (AGENTS.md section 4),
      # so the optimisation level and debug info are stated here rather than inherited.
      "$<$<CONFIG:Thread>:-O1>"
      "$<$<CONFIG:Thread>:-g>"
    )
    # Compile options, not a replacement for CMAKE_CXX_FLAGS: assigning that variable discards
    # whatever a toolchain file or `-DCMAKE_CXX_FLAGS` provided, which is why
    # `scripts/sanity.sh warnings` was not actually applying its own flags (#36).
    # `-pthread` also goes on the link line, where it used to reach via CMAKE_CXX_FLAGS.
    # Targets created before this point (the third-party libraries above) no longer inherit these
    # warnings or -pthread: none of them creates threads, and scoping warnings to this project's
    # targets is the direction #36 asks for. Their language level is still C++20 from
    # CMAKE_CXX_STANDARD above, so meshoptimizer moves from the default gnu++17 to C++20.
    # `-Wextra` is on. `-Wmissing-field-initializers` is off, and not because it is noisy: it fires on
    # exactly the initialisation style AGENTS.md section 3 mandates. `{VK_STRUCTURE_TYPE_...}` and partial
    # designated initialisers omit fields on purpose - positional initialisation of a Vulkan struct is
    # banned because the layouts do not match the specification order - so the check reports a style that
    # is required here. Measured on the 2026-10-20 tree: 3824 of 3942 `-Wextra` warnings were this check;
    # excluding it left 118, of which 40 are owned by #169/#186 and the rest were fixed or are vendored.
    add_compile_options(-Wall -Wextra -Wno-missing-field-initializers -Wno-missing-braces -pthread)
    add_link_options(-pthread)
    # -g is left to the per-configuration defaults: Debug carries it via CMAKE_CXX_FLAGS_DEBUG, and
    # the old shared string pushed it into Release too. The dead CMAKE_CXX_DEBUG_FLAGS /
    # CMAKE_CXX_RELEASE_FLAGS assignments are gone - the real variables are CMAKE_CXX_FLAGS_<CONFIG>,
    # so neither ever did anything.
    set(SANITIZE_FLAGS
      "$<${SANITIZE_CONDITION}:-fsanitize=address,undefined>"
      # Undefined behaviour must *fail* the run, not print and continue. With `-fsanitize=undefined`
      # alone the runtime default is to recover: a signed overflow or an out-of-range enum load prints a
      # `runtime error:` line to stderr and the process still exits 0, so the suite reports success with
      # UB inside it. Measured before this change on the tree: the same binary printed
      # "runtime error: signed integer overflow" and exited 0. Every "no UBSan errors" claim in this
      # repository was therefore a claim about *reading stderr*, not about an exit status.
      # Not enabled: `-fsanitize=pointer-compare,pointer-subtract` with
      # `ASAN_OPTIONS=detect_invalid_pointer_pairs=2`. Measured on this tree it aborts inside
      # `_deps/vma-src/include/vk_mem_alloc.h:5231` (VmaPoolAllocator's intrusive list compares pointers
      # that are not in the same object). VMA is a vendored header compiled into our translation units,
      # so the check cannot be scoped away from it, and it would abort every run. Class of check is
      # sound; this tree's dependency is not compatible with it.
      "$<${SANITIZE_CONDITION}:-fno-sanitize-recover=undefined>"
      "$<${SANITIZE_CONDITION}:-fno-omit-frame-pointer>"
      # Sanitizer reports need line information, so -g is kept for Sanitize even though Release no
      # longer gets it from the shared flags.
      "$<${SANITIZE_CONDITION}:-g>"
    )
elseif(MSVC)
    # Left as-is: the Windows path is not covered by this repository's CI, so this PR does not change
    # an untested toolchain. It appends rather than assigning, so it was never the fragile form.
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest /W3")
    set(SANITIZE_FLAGS "$<${SANITIZE_CONDITION}:/fsanitize=address,undefined,thread,memory>")
    add_compile_options("$<${SANITIZE_CONDITION}:/Zi>")
endif()

# Deliberately *after* the third-party targets above (imgui, SPIRV-Reflect, tinygltf, stb, ...): only the
# project's own targets inherit these, so UB inside a vendored library cannot abort our run and a vendored
# warning cannot be mistaken for ours. The Sanitize build is a statement about this repository's code.
add_compile_options(${SANITIZE_FLAGS})
add_link_options(${SANITIZE_FLAGS})
# Same placement and the same reasoning: a TSan report is a statement about this repository's code, so
# the vendored targets above stay uninstrumented.
add_compile_options(${THREAD_FLAGS})
add_link_options(${THREAD_FLAGS})
if(CMAKE_BUILD_TYPE STREQUAL "Thread" AND NOT UNIX)
    # Without this the configuration would build an ordinary binary on a platform where no TSan flag is
    # wired, and the sanitizer run would be green because it never instrumented anything.
    message(FATAL_ERROR
        "CMAKE_BUILD_TYPE=Thread has no -fsanitize=thread flags on this platform; "
        "ThreadSanitizer is wired for UNIX only (MSVC does not provide it).")
endif()

# Headers that shared between shader and cpp 
set (SHARED_HEADER_DIR "${MUYO_ROOT}/shaders/shared")

include_directories(
    ${SHARED_HEADER_DIR}
    )

# Vendored and SDK headers are SYSTEM: `-Wextra` checks this repository's code, not its dependencies', and a
# warning from `vk_mem_alloc.h` in every translation unit says nothing about us. Header names are unique
# between these roots and ours (checked: no basename occurs in both), so `-isystem` cannot change what an
# include resolves to - only which translation unit a diagnostic is attributed to. #53/#68 reshape this
# include surface; this split is what the `-Wextra` baseline needs until then.
include_directories(SYSTEM
    ${Vulkan_INCLUDE_DIR}
    ${GLFW3_INCLUDE_DIR}
    ${GLM_INCLUDE_DIRS}
    ${IMGUI_INCLUDE_DIRS}
    ${IMGUIZMO_INCLUDE_DIRS}
    ${IMNODES_INCLUDE_DIRS}
    ${SPIRV-Reflect_INCLUDE_DIRS}
    # RenderDoc's API is a single header, and `src/Debug/RenderDoc.h` lives in this target. It compiled only because
    # `tests` added this path for the vendor-specific test helper; the capture backend makes it a real dependency of
    # `muyo`, so the path belongs here rather than in one consumer (#167).
    ${MUYO_ROOT}/thirdparty/RenderDoc
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
    "src/RenderGraph/DescriptorSetCache.cpp"
    "src/RenderGraph/RenderGraphExecutor.cpp"
    "src/RenderGraph/RenderGraphTrace.cpp"
)

#fetch VMA from Github
include(FetchContent)
FetchContent_Declare(
  VMA
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
  # Pinned to the commit this project's aliasing path was verified against (#94). It sits 15 commits past
  # the v3.4.0 tag, so neither the tag nor master would name the code the block-sharing guarantee rests on -
  # and aliasing semantics, the CAN_ALIAS flag in particular, is exactly what a VMA update could change.
  # Pinning to the commit rather than the tag keeps the verified code; fetching a raw SHA from GitHub was
  # checked to work before relying on it. Same reasoning as the pinned Vulkan SDK tarball in CI (#91).
  GIT_TAG bfb2ca99e1e7e9684dc06546b552ee3eff4b10aa
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
    )

# The dependency half of the same interface, marked SYSTEM so a consumer that turns on extra warnings is
# not handed ours-vs-vendored ambiguity by this project's interface.
target_include_directories(muyo SYSTEM
    PUBLIC
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
target_include_directories(muyo_rg SYSTEM PUBLIC $<BUILD_INTERFACE:${vma_SOURCE_DIR}/include>)

# SPIRV-Reflect is PUBLIC because it is genuinely part of the interface: RenderGraphBuilder.h includes
# ShaderReflectionFetcher.h, which includes spirv_reflect.h, so a consumer compiles against its headers
# - and `muyo` compiles ShaderReflectionFetcher.cpp, which calls spvReflect*. Only the in-tree `tests`
# target named it, so an embedder linking muyo_rg got "undefined reference to spvReflectCreateShaderModule"
# from an object the linker pulled in on demand (filed by muyo-llm as Finding F1).
target_link_libraries(muyo imgui imguizmo imnodes SPIRV-Reflect ${Vulkan_LIBRARIES} ${WINDOW_LIB} VulkanMemoryAllocator stb tinygltf tinyobj meshoptimizer)

# muyo_rg does not stand alone today: it reaches into the engine for resource managers, the device
# singleton and the shader reflection service. Declared rather than implied, so the coupling is visible
# where a consumer meets it - and so #53 has one line to change when those become injected services.
target_link_libraries(muyo_rg PUBLIC muyo)

file(GLOB main_SRC
    "src/app/helloVulkan.cpp"
    )

file(GLOB rgMain_SRC
    "src/app/helloVulkanRG.cpp"
    )

file(GLOB PSOCompiler_SRC
    "src/app/PSOCompiler.cpp"
    )

if (MUYO_TOP_LEVEL)
add_executable(helloVulkan ${main_SRC})
target_link_libraries(helloVulkan PUBLIC muyo)

# The render graph's first consumer: the same frame through the graph. It links both `muyo` (the
# engine's resources, device and managers) and `muyo_rg`.
add_executable(helloVulkanRG ${rgMain_SRC})
target_link_libraries(helloVulkanRG PUBLIC muyo muyo_rg)
target_compile_definitions(helloVulkanRG PRIVATE IMGUI_USER_CONFIG="ImGuiUserConfig.h")


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

# Build slang shaders. `.heap.slang` sources are the direct-descriptor-heap variants: they compile with
# the SPV_EXT_descriptor_heap capability and emit `<name>.heap.spv`, while the ordinary `.slang` rule stays
# unchanged for the legacy RenderPassManager. The glob excludes the heap suffix so a source is built by
# exactly one rule (docs/DescriptorHeap-Bindless-Design.md §9).
file (GLOB_RECURSE SLANG_SOURCE_FILES "${MUYO_ROOT}/shaders/*.slang")
list (FILTER SLANG_SOURCE_FILES EXCLUDE REGEX "\\.heap\\.slang$")
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

# Descriptor-heap variants. `-capability spvDescriptorHeapEXT` emits SPV_EXT_descriptor_heap +
# SPV_KHR_untyped_pointers; the resource/sampler heap strides stay at Slang's default 0 so the module
# carries OpConstantSizeOfEXT/ArrayStrideIdEXT rather than a device-specific stride
# (docs/DescriptorHeap-Bindless-Design.md §5). The output name keeps the `.heap` marker so the graph can
# load `foo.heap.spv` while the legacy renderer loads `foo.spv`.
file (GLOB_RECURSE HEAP_SLANG_SOURCE_FILES "${MUYO_ROOT}/shaders/*.heap.slang")
message(STATUS "Descriptor-heap Slang Shaders: ${HEAP_SLANG_SOURCE_FILES}")
# Prefer the SDK's own validator: the distribution's spirv-val can be older than the extension it is
# asked to validate (measured here: /usr/bin/spirv-val rejects capability 5128, the SDK's 2026.4 accepts
# it). NO_DEFAULT_PATH makes the build use the pinned SDK or nothing, rather than silently picking the
# wrong one from PATH.
find_program(SPIRV_VAL_EXECUTABLE
    NAMES spirv-val
    HINTS "$ENV{VULKAN_SDK}/bin"
    NO_DEFAULT_PATH)
foreach (HEAP_SLANG_SOURCE_FILE ${HEAP_SLANG_SOURCE_FILES})
    get_filename_component(FILE_NAME ${HEAP_SLANG_SOURCE_FILE} NAME)
    string(REGEX REPLACE "\\.slang$" ".spv" SPIRV_NAME "${FILE_NAME}")
    set(SPIRV "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${SPIRV_NAME}")
    set(HEAP_VALIDATE_COMMAND)
    if (SPIRV_VAL_EXECUTABLE)
        set(HEAP_VALIDATE_COMMAND COMMAND ${SPIRV_VAL_EXECUTABLE} --target-env vulkan1.4 ${SPIRV})
    else()
        # Silence here would make an unvalidated .heap.spv look like a validated one, the same shape as
        # the silent shader-load skips this repository has already paid for. Say it in configure output.
        message(STATUS "descriptor-heap .spv validation: disabled (no spirv-val in $ENV{VULKAN_SDK}/bin)")
    endif()
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/"
        COMMAND ${Vulkan_SLANG_EXECUTABLE} ${HEAP_SLANG_SOURCE_FILE} -DSHADER_CODE -DSLANG
                -capability spvDescriptorHeapEXT -capability spirv_1_4
                -profile glsl_450 -target spirv -o ${SPIRV} -entry main
        ${HEAP_VALIDATE_COMMAND}
        DEPENDS ${HEAP_SLANG_SOURCE_FILE} ${GLSL_HEADER_FILES}
        )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()





add_custom_target(
    Shaders
    DEPENDS ${SPIRV_BINARY_FILES}
    )

if (MUYO_TOP_LEVEL)
add_dependencies(helloVulkan Shaders)
add_dependencies(helloVulkanRG Shaders)

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
    src/tests/CaptureToolTest.cpp
    src/tests/DebugCallbackTests.cpp
    src/tests/DescriptorHeapProbeTests.cpp
    src/tests/DescriptorHeapServiceTests.cpp
    src/tests/DeviceCapabilityTests.cpp
    src/tests/FrameSyncTest.cpp
    src/tests/RenderGraphBuilderTest.cpp
    src/tests/RenderGraphHeapTests.cpp
    src/tests/PSODescTest.cpp
    src/tests/RenderGraphLaneLayoutTest.cpp
    src/tests/RenderGraphTraceTest.cpp
    src/tests/SceneImporterTests.cpp
    src/tests/ShaderReflectionTests.cpp
    src/tests/TransientMemoryPlannerTest.cpp
)
message(STATUS "Catch2 source dir: ${catch2_SOURCE_DIR}")
target_include_directories(tests PRIVATE ${catch2_SOURCE_DIR}/src thirdparty/RenderDoc)
# `muyo` and `muyo_rg` already provide every source this binary needs, and `muyo` links
# `SPIRV-Reflect`, so the duplicated source list and the explicit `SPIRV-Reflect` are gone: they
# were compiling a second copy of the fetcher and the graph, and the redundant link line was
# covering for it rather than the libraries (#76).
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain muyo muyo_rg)
include(Catch)

# ThreadSanitizer cannot start on a kernel that randomises mappings beyond its shadow layout: every
# invocation, including the one `catch_discover_tests` performs at *build* time, dies with
# `FATAL: ThreadSanitizer: unexpected memory mapping ...`. Disabling address-space randomisation for
# that process is the root-free workaround (`setarch x86_64 -R`), and the emulator property is where it
# belongs because ctest and the build-time discovery both read it (#173). A single-config generator is
# assumed - the repository builds with Unix Makefiles - so the configuration is named by
# CMAKE_BUILD_TYPE; on a multi-config generator there is no one build type to test and this would be a
# silent no-op, so it is refused rather than left looking wired.
if(CMAKE_BUILD_TYPE STREQUAL "Thread")
    if(CMAKE_CONFIGURATION_TYPES)
        message(FATAL_ERROR
            "CMAKE_BUILD_TYPE=Thread is only supported with a single-config generator: "
            "CROSSCOMPILING_EMULATOR is a target property and cannot vary by configuration here.")
    endif()
    set_target_properties(tests PROPERTIES
        CROSSCOMPILING_EMULATOR "setarch;${CMAKE_SYSTEM_PROCESSOR};-R")
    message(STATUS "ThreadSanitizer: test discovery and ctest run under `setarch ${CMAKE_SYSTEM_PROCESSOR} -R`")
endif()

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
