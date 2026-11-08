# Out-of-tree consumer probe for the installed `muyo_rg` CMake package (#298).
#
# The `package` job in .github/workflows/ci.yml configures this project with only
# -DCMAKE_PREFIX_PATH=<installed prefix>: no source-tree include directory and no source-tree target
# are on the command line. That is the point - if the exported interface is incomplete, this project
# fails to configure or compile, which is what a consumer would hit.
cmake_minimum_required(VERSION 3.16)
project(muyo_rg_package_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The only `muyo_rg` input: the installed package. `find_package` fails with "muyo_rg could not be
# found because dependency Vulkan could not be found" when the SDK is hidden, which the job checks.
find_package(muyo_rg 0.1 REQUIRED)

add_executable(package_probe main.cpp)
target_link_libraries(package_probe PRIVATE Muyo::muyo_rg)
