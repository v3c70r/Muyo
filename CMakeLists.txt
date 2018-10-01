cmake_minimum_required(VERSION 3.10)

# Set system env
if (NOT DEFINED VK_SDK_PATH)
    message(FATAL_ERROR "VK_SDK_PATH is not defined")
endif()

find_package(glfw3 3.3 REQUIRED)

message(STATUS "Set VK_SDK_PATH at ${VK_SDK_PATH}")
set (VK_INCLUDE_PATH ${VK_SDK_PATH}/include)

set (VK_LAYER_PATH    ${VK_SDK_PATH}/etc/vulkan/explicit_layer.d)
set (VK_LIBRARY_PATH  ${VK_SDK_PATH}/lib/)
set (VK_ICD_FILENAMES ${VK_SDK_PATH}/etc/vulkan/icd.d/MoltenVK_icd.json)


set (CMAKE_CXX_FLAGS "-std=c++11 -Wall -pthread -Wno-missing-braces")
set (CMAKE_CXX_DEBUG_FLAGS ${CMAKE_CXX_FLAGS} "-g")
set (CMAKE_CXX_DEBUG_FLAGS ${CMAKE_CXX_FLAGS} "-g -O2")

link_directories(${VK_LIBRARY_PATH})

include_directories(${VK_INCLUDE_PATH}
    ${GLFW3_INCLUDE_DIR}
    )

add_executable( helloVulkan
    helloVulkan.cpp thirdparty/tiny_obj_loader.cc context.cpp
    )
target_link_libraries(helloVulkan glfw vulkan)

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
