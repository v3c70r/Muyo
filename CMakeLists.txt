cmake_minimum_required(VERSION 3.0.0)
project(Muyo)
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

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)

#window lib
if (${FEATURE_USE_SDL})
  find_package(SDL2 REQUIRED)
  add_compile_definitions(USE_SDL)
  set(WINDOW_LIB SDL2::SDL2main SDL2::SDL2)
else()
  ## GLFW
  # Look for precompiled glfw library
  find_package(glfw3 3.2 QUIET)
  # Otherwise, build from source
  if (NOT ${glfw3_FOUND})
    # Build glfw from source
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    #set(GLFW_USE_WAYLAND ON CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/glfw)
  endif()
  add_compile_definitions(USE_GLFW)
  set(WINDOW_LIB glfw)
endif()

## GLM
find_package(glm QUIET)
if (NOT ${glm_FOUND})
    set(GLM_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/glm)
endif()

## imgui
set(IMGUI_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/imgui)
add_library(imgui
    thirdparty/imgui/imgui.cpp
    thirdparty/imgui/imgui_demo.cpp
    thirdparty/imgui/imgui_draw.cpp
    thirdparty/imgui/imgui_widgets.cpp
    thirdparty/imgui/imgui_tables.cpp
    )

## ImGuizmo
set(IMGUIZMO_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/ImGuizmo)
add_library(imguizmo thirdparty/ImGuizmo/ImGuizmo.cpp)

## VULKAN
if (NOT ENV{VULKAN_SDK} AND EXISTS ENV{VULKAN_SDK})
    list(APPEND CMAKE_PREFIX_PATH $ENV{VULKAN_SDK})
endif()

# Cap vulkan version to avoid validation layer error
find_package(Vulkan REQUIRED)


## Vulkan Memory Allocator
add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/VulkanMemoryAllocator)
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/VulkanMemoryAllocator/include)

## tiny obj
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/tinyobjloader)
add_library(tinyobj ${CMAKE_SOURCE_DIR}/thirdparty/tinyobjloader/tiny_obj_loader.cc)

## tiny gltf
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/tinygltf)
add_library(tinygltf ${CMAKE_SOURCE_DIR}/thirdparty/tinygltf/tiny_gltf.cc)

## stb images
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/stb)
add_library(stb ${CMAKE_SOURCE_DIR}/thirdparty/stb/stb.cpp)

## meshoptimizer
add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/meshoptimizer)
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/meshoptimizer/src)

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

if (UNIX)
    set (CMAKE_CXX_FLAGS "-std=c++20 -Wall -pthread -Wno-missing-braces -g")
    set (CMAKE_CXX_DEBUG_FLAGS ${CMAKE_CXX_FLAGS} "")
    set (CMAKE_CXX_RELEASE_FLAGS ${CMAKE_CXX_FLAGS} "-O3")
elseif(MSVC)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest /W3")
endif()

# Headers that shared between shader and cpp 
set (SHARED_HEADER_DIR "${CMAKE_SOURCE_DIR}/shaders/shared")

include_directories(
    ${Vulkan_INCLUDE_DIR}
    ${GLFW3_INCLUDE_DIR}
    ${GLM_INCLUDE_DIRS}
    ${IMGUI_INCLUDE_DIRS}
    ${SHARED_HEADER_DIR}
    ${IMGUIZMO_INCLUDE_DIRS}
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
    "src/tests/*.h"
    "src/tests/*.cpp"
    )

add_library(muyo STATIC ${muyo_SRC})
target_link_libraries(muyo imgui imguizmo ${Vulkan_LIBRARIES} ${WINDOW_LIB} VulkanMemoryAllocator stb tinygltf tinyobj meshoptimizer)

file(GLOB main_SRC
    "src/app/helloVulkan.cpp"
    )

file(GLOB PSOCompiler_SRC
    "src/app/PSOCompiler.cpp"
    )

add_executable(helloVulkan ${main_SRC})
target_link_libraries(helloVulkan PUBLIC muyo)


add_executable(PSOCompiler ${PSOCompiler_SRC})
target_link_libraries(PSOCompiler PUBLIC muyo)


# Build glsl shaders
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${CMAKE_SOURCE_DIR}/shaders/*.frag"
    "${CMAKE_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_SOURCE_DIR}/shaders/*.rgen"
    "${CMAKE_SOURCE_DIR}/shaders/*.rmiss"
    "${CMAKE_SOURCE_DIR}/shaders/*.rchit"
    "${CMAKE_SOURCE_DIR}/shaders/*.comp"
    )

file(GLOB_RECURSE GLSL_HEADER_FILES
    "${CMAKE_SOURCE_DIR}/shaders/*.h"
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
file (GLOB_RECURSE SLANG_SOURCE_FILES "${CMAKE_SOURCE_DIR}/shaders/*.slang")
message(STATUS "Slang Shaders: ${SLANG_SOURCE_FILES}")
foreach (SLANG_SOURCE_FILE ${SLANG_SOURCE_FILES})
    get_filename_component(FILE_NAME ${SLANG_SOURCE_FILE} NAME)
    set(SPIRV "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${FILE_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/"
        #hello-world.slang -profile glsl_450 -target spirv -o hello-world.spv -entry computeMain
        COMMAND ${Vulkan_SLANG_EXECUTABLE} ${SLANG_SOURCE_FILE} -emit-spirv-directly -g2 -DSHADER_CODE -profile glsl_450 -target spirv -o ${SPIRV} -entry main
        DEPENDS ${SLANG_SOURCE_FILE}# ${GLSL_HEADER_FILES}) # Header change will trigger a full shader rebuild, which might not be necessary
        )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()





add_custom_target(
    Shaders
    DEPENDS ${SPIRV_BINARY_FILES}
    )

add_dependencies(helloVulkan Shaders)

# Copy over asset
add_custom_command(
    TARGET helloVulkan
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/assets
    )
message(STATUS "runtime dir" ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# Tests
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
