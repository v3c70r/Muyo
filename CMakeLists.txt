cmake_minimum_required(VERSION 3.0.0)
project(Muyo)
# Set system env
#if (NOT DEFINED VK_SDK_PATH)
#    message(FATAL_ERROR "VK_SDK_PATH is not defined")
#endif()

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set ( CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")
set ( CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
set ( CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")
set ( CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")
set ( CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}")
set ( CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}")

### Handle dependencies

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
# build vulkanMemoryAllocator from source

## GLM
find_package(glm QUIET)
if (NOT ${glm_FOUND})
    set(GLM_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/glm)
endif()

set(IMGUI_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/imgui)

## VULKAN
if (NOT ENV{VULKAN_SDK} AND EXISTS ENV{VULKAN_SDK})
    list(APPEND CMAKE_PREFIX_PATH $ENV{VULKAN_SDK})
endif()

find_package(Vulkan REQUIRED)

## Vulkan Memory Allocator
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/VulkanMemoryAllocator)
add_library(vma ${CMAKE_SOURCE_DIR}/thirdparty/VulkanMemoryAllocator/vk_mem_alloc.cpp)

## tiny obj
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/tinyobjloader)
add_library(tinyobj ${CMAKE_SOURCE_DIR}/thirdparty/tinyobjloader/tiny_obj_loader.cc)

## tiny gltf
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/tinygltf)
add_library(tinygltf ${CMAKE_SOURCE_DIR}/thirdparty/tinygltf/tiny_gltf.cc)

## stb images
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/stb)
add_library(stb ${CMAKE_SOURCE_DIR}/thirdparty/stb/stb.cpp)

find_file(GLSL_VALIDATOR 
    NAMES 
    glslangValidator glslangValidator.exe 
    PATHS 
    "${Vulkan_INCLUDE_DIR}/../Bin"
    "${Vulkan_INCLUDE_DIR}/../bin"
    "$ENV{VULKAN_SDK}/bin"
    "$ENV{VULKAN_SDK}/Bin"
    )
if(NOT EXISTS ${GLSL_VALIDATOR})
    message(FATAL_ERROR "Can't find glslangValidator at ${GLSL_VALIDATOR}")
endif()

if (UNIX)
    set (CMAKE_CXX_FLAGS "-std=c++17 -Wall -pthread -Wno-missing-braces -g")
    set (CMAKE_CXX_DEBUG_FLAGS ${CMAKE_CXX_FLAGS} "")
    set (CMAKE_CXX_RELEASE_FLAGS ${CMAKE_CXX_FLAGS} "-O3")
elseif(MSVC)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest /W4")
endif()


#link_directories(${VK_LIBRARY_PATH};${VK_BIN_PATH})


include_directories(
    ${Vulkan_INCLUDE_DIR}
    ${GLFW3_INCLUDE_DIR}
    ${GLM_INCLUDE_DIRS}
    ${IMGUI_INCLUDE_DIRS}
    )

add_library(imgui
    thirdparty/imgui/imgui.cpp
    thirdparty/imgui/imgui_demo.cpp
    thirdparty/imgui/imgui_draw.cpp
    thirdparty/imgui/imgui_widgets.cpp
    )
    
#Enable CPPCHECK
# set(CMAKE_CXX_CPPCHECK "cppcheck")

add_executable( helloVulkan
    src/helloVulkan.cpp

    src/PipelineStateBuilder.cpp
    src/Texture.cpp
    src/VkRenderDevice.cpp
    src/VkMemoryAllocator.cpp
    src/VertexBuffer.cpp
    src/RenderPass.cpp
    src/RenderPassGBuffer.cpp
    src/Swapchain.cpp
    src/RenderTargetResource.cpp
    src/RenderResourceManager.cpp
    src/RenderPassUI.cpp
    src/RenderPassSkybox.cpp
    src/DescriptorManager.cpp
    src/SamplerManager.cpp
    src/Debug.cpp
    src/Geometry.cpp
    src/Material.cpp
    src/RenderLayerIBL.cpp
    src/SceneImporter.cpp
    src/Scene.cpp
    src/RenderPassManager.cpp
    src/RenderPassTransparent.cpp
    src/DebugUI.cpp
    src/SceneManager.cpp
    src/RayTracingUtils.cpp
    src/ImageStorageResource.cpp
    )

target_link_libraries(helloVulkan glfw imgui ${Vulkan_LIBRARIES} vma stb tinygltf tinyobj)

# Build shaders
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${CMAKE_SOURCE_DIR}/shaders/*.frag"
    "${CMAKE_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_SOURCE_DIR}/shaders/*.rgen"
    "${CMAKE_SOURCE_DIR}/shaders/*.rmiss"
    "${CMAKE_SOURCE_DIR}/shaders/*.rchit"
    )

message(STATUS "Shaders: ${GLSL_SOURCE_FILES}")
foreach (GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${FILE_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/"
        COMMAND ${GLSL_VALIDATOR} --target-env vulkan1.2 -g -V ${GLSL} -o ${SPIRV}
        DEPENDS ${GLSL})
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
