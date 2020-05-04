cmake_minimum_required(VERSION 3.0.0)
project(TinyRenderer)

# Set system env
#if (NOT DEFINED VK_SDK_PATH)
#    message(FATAL_ERROR "VK_SDK_PATH is not defined")
#endif()

set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

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

## VULKAN
if(APPLE)
    set (Vulkan_INCLUDE_DIR $ENV{VULKAN_SDK}/include)
    set (Vulkan_LIBRARIES $ENV{VULKAN_SDK}/lib/libvulkan.1.dylib)
    message (STATUS "Found vulkan " ${Vulkan_INCLUDE_DIR})
else()
    find_package(Vulkan REQUIRED)
endif()

## Vulkan Memory Allocator
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/VulkanMemoryAllocator)
add_library(vma ${CMAKE_SOURCE_DIR}/thirdparty/VulkanMemoryAllocator/vk_mem_alloc.cpp)

## stb images
include_directories(${CMAKE_SOURCE_DIR}/thirdparty/stb)
add_library(stb ${CMAKE_SOURCE_DIR}/thirdparty/stb/stb.cpp)

## tiny obj
add_library(tinyobj ${CMAKE_SOURCE_DIR}/thirdparty/tinyobjloader/tiny_obj_loader.cc)


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
    )

add_library(imgui
    thirdparty/imgui/imgui.cpp
    thirdparty/imgui/imgui_demo.cpp
    thirdparty/imgui/imgui_draw.cpp
    thirdparty/imgui/imgui_widgets.cpp
    thirdparty/imgui/imgui_impl_vulkan.cpp
    )
    
#Enable CPPCHECK
# set(CMAKE_CXX_CPPCHECK "cppcheck")

add_executable( helloVulkan
    src/helloVulkan.cpp
    src/PipelineStateBuilder.cpp
    #src/UIOverlay.cpp 
    src/Texture.cpp
    src/VkRenderDevice.cpp src/Util.cpp
    src/VkMemoryAllocator.cpp
    src/RenderPass.cpp
    src/RenderPassGBuffer.cpp
    src/PipelineManager.cpp
    src/Swapchain.cpp
    src/GLFWSwapchain.cpp
    src/RenderTargetResource.cpp
    src/RenderResourceManager.cpp
    src/Debug.cpp
    )

target_link_libraries(helloVulkan glfw imgui ${Vulkan_LIBRARIES} stb vma tinyobj)

# Build shaders
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${CMAKE_SOURCE_DIR}/shaders/*.frag"
    "${CMAKE_SOURCE_DIR}/shaders/*.vert"
    )

message(STATUS "Shaders: ${GLSL_SOURCE_FILES}")
foreach (GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${FILE_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/"
        COMMAND ${GLSL_VALIDATOR} -V -H ${GLSL} -o ${SPIRV}
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

#add_custom_command(TARGET helloVulkan POST_BUILD
#    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:helloVulkan>/shaders/"
#    COMMAND ${CMAKE_COMMAND} -E copy_directory
#    "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders"
#    "$<TARGET_FILE_DIR:helloVulkan>/shaders"
#    )
#
