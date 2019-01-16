cmake_minimum_required(VERSION 3.0.0)

# Set system env
if (NOT DEFINED VK_SDK_PATH)
    message(FATAL_ERROR "VK_SDK_PATH is not defined")
endif()

# Look for dependencies

# Look for precompiled glfw library
find_package(glfw3 3.2)

# Otherwise, build from source
if (NOT ${glfw3_FOUND})
    # Build glfw from source
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/glfw)
endif()

find_package(glm)
if (NOT ${glm_FOUND})
    set(GLM_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/thirdparty/glm)
endif()

message(STATUS "Set VK_SDK_PATH at ${VK_SDK_PATH}")
set (VK_INCLUDE_PATH ${VK_SDK_PATH}/include)

set (VK_LAYER_PATH    ${VK_SDK_PATH}/etc/vulkan/explicit_layer.d)
set (VK_LIBRARY_PATH  ${VK_SDK_PATH}/lib/)
set (VK_BIN_PATH  ${VK_SDK_PATH}/bin/)
set (VK_ICD_FILENAMES ${VK_SDK_PATH}/etc/vulkan/icd.d/MoltenVK_icd.json)
file(WRITE  ${PROJECT_BINARY_DIR}/env.sh "export VK_LAYER_PATH=${VK_LAYER_PATH}\n")
file(APPEND ${PROJECT_BINARY_DIR}/env.sh "export VK_LIBRARY_PATH=${VK_LIBRARY_PATH}\n")
file(APPEND ${PROJECT_BINARY_DIR}/env.sh "export VK_ICD_FILENAMES=${VK_ICD_FILENAMES}\n")


if (UNIX)
    set (CMAKE_CXX_FLAGS "-std=c++14 -Wall -pthread -Wo-missing-braces")
    set (CMAKE_CXX_DEBUG_FLAGS ${CMAKE_CXX_FLAGS} "-g")
    set (CMAKE_CXX_RELEASE_FLAGS ${CMAKE_CXX_FLAGS} "-O3")
elseif(MSVC)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest /W4")
endif()

link_directories(${VK_LIBRARY_PATH};${VK_BIN_PATH})


include_directories(
    ${VK_INCLUDE_PATH}
    ${GLFW3_INCLUDE_DIR}
    ${GLM_INCLUDE_DIRS}
    )

add_library( imgui SHARED
    thirdparty/imgui/imgui.cpp
    thirdparty/imgui/imgui_demo.cpp
    thirdparty/imgui/imgui_draw.cpp
    thirdparty/imgui/imgui_widgets.cpp
    thirdparty/imgui/imgui_impl_vulkan.cpp
    )
    
add_executable( helloVulkan
    src/helloVulkan.cpp thirdparty/tiny_obj_loader.cc src/Context.cpp
    )
target_link_libraries(helloVulkan glfw imgui vulkan-1)

# Build shaders
set (GLSL_VALIDATOR ${VK_SDK_PATH}/bin/glslangValidator)
file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${CMAKE_SOURCE_DIR}/shaders/*.frag"
    "${CMAKE_SOURCE_DIR}/shaders/*.vert"
    )

message(STATUS "Shaders: ${GLSL_SOURCE_FILES}")
foreach (GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${PROJECT_BINARY_DIR}/shaders/${FILE_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/shaders/"
        COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
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
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/assets ${PROJECT_BINARY_DIR}/assets
    )
#add_custom_command(TARGET helloVulkan POST_BUILD
#    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:helloVulkan>/shaders/"
#    COMMAND ${CMAKE_COMMAND} -E copy_directory
#    "${PROJECT_BINARY_DIR}/shaders"
#    "$<TARGET_FILE_DIR:helloVulkan>/shaders"
#    )
#
