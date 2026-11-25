# 3.11 is required by `SameMinorVersion` in the package version file (#73); the rest of this file
# already used newer commands in practice. The 0.x compatibility decision is recorded in
# docs/RenderGraph-Library-Design.md, "Versioning the library".
cmake_minimum_required(VERSION 3.11)
# The language level is a directory default declared before project(), so every later-created target -
# including the third-party subdirectories - inherits C++20, and CMAKE_CXX_FLAGS does not have to
# carry it. CMAKE_CXX_EXTENSIONS OFF maps to the strict flag, as before (#68).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
project(Muyo VERSION 0.1.0)

# Install destinations and the package helpers are used by the `muyo_rg` package rules, which the
# target definitions below point at through their INSTALL_INTERFACE include directories (#73).
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Root of this project, correct whether it is the top-level build or a subdirectory.
# CMAKE_SOURCE_DIR / CMAKE_BINARY_DIR resolve to the *consumer's* root when this project is
# embedded, which silently points every path built from them at the wrong tree (#68).
set(MUYO_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(MUYO_BINARY_ROOT "${CMAKE_CURRENT_BINARY_DIR}")

# ── Library version ──────────────────────────────────────────────────────────
# The version is declared once, here, and the public header is generated from it rather than written
# by hand, so the two cannot disagree. The policy behind the number - what a major/minor/patch mean,
# the deprecation window, and why 1.0 waits for the public surface (#53) - is in
# docs/RenderGraph-Library-Design.md, "Versioning the library" (#87).
#
# `muyo_rg/version.h` is the generated, macro-level form (`#if MUYO_RG_VERSION >= ...`); the
# checked-in `src/RenderGraph/MuyoVersion.h` re-exports the values as documented C++ constants and is
# the header the Doxygen coverage check sees. Both are public.
set(MUYO_RG_GENERATED_DIR "${MUYO_BINARY_ROOT}/generated")
configure_file(
    "${MUYO_ROOT}/cmake/MuyoVersion.h.in"
    "${MUYO_RG_GENERATED_DIR}/muyo_rg/version.h"
    @ONLY
)

# True when this project is the root of the build. Everything guarded by it - the test binaries and the
# docs check - is for developing this project rather than consuming it, and the guard is what keeps the
# Catch2 fetch, a network dependency, out of an embedder's configure.
# PROJECT_IS_TOP_LEVEL is the idiomatic spelling but needs CMake 3.21; this file declares 3.11, where
# it would silently evaluate to false and the tests would quietly never be built.
set(MUYO_TOP_LEVEL FALSE)
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(MUYO_TOP_LEVEL TRUE)
endif()
# An embedder that takes this project as a subdirectory keeps its own install and export decisions, so
# the package rules are opt-in there and on by default at the top level (#73).
option(MUYO_RG_INSTALL "Generate the muyo_rg install rules and CMake package" ${MUYO_TOP_LEVEL})
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
# root, so Muyo's artefacts would be scattered into it (#68). At top level the two are the same
# directory, so nothing changes for this build.
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${MUYO_BINARY_ROOT})
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${MUYO_BINARY_ROOT}")
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${MUYO_BINARY_ROOT}")
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${MUYO_BINARY_ROOT}")
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${MUYO_BINARY_ROOT}")
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${MUYO_BINARY_ROOT}")
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${MUYO_BINARY_ROOT}")
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)

## SPIRV-Reflect
# The reflection half of `muyo_device`: `ShaderReflectionFetcher.h` is part of the device's public
# interface and includes `spirv_reflect.h`, so this vendored target stays and is exported with the
# package (see the install rules below). Everything else the engine used to declare - GLFW, GLM, Dear
# ImGui, ImGuizmo, imnodes, tinyobjloader, tinygltf, stb and meshoptimizer - moved to the renderer with
# the pipeline it served (#318).
set(SPIRV-Reflect_INCLUDE_DIRS ${MUYO_ROOT}/thirdparty/SPIRV-Reflect)
add_library(SPIRV-Reflect ${MUYO_ROOT}/thirdparty/SPIRV-Reflect/spirv_reflect.cpp)

## VULKAN
if (NOT ENV{VULKAN_SDK} AND EXISTS ENV{VULKAN_SDK})
    list(APPEND CMAKE_PREFIX_PATH $ENV{VULKAN_SDK})
endif()

# Cap vulkan version to avoid validation layer error
find_package(Vulkan REQUIRED)

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
    # Targets created before this point (the vendored SPIRV-Reflect library above) do not inherit these
    # warnings or -pthread: it creates no threads, and scoping warnings to this project's targets is the
    # direction #36 asks for. Its language level is still C++20 from CMAKE_CXX_STANDARD above.
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
    # Windows CI builds and runs the tests with this toolchain; keep the existing MSVC flags intact.
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest /W3")
    set(SANITIZE_FLAGS "$<${SANITIZE_CONDITION}:/fsanitize=address,undefined,thread,memory>")
    add_compile_options("$<${SANITIZE_CONDITION}:/Zi>")
endif()

# Deliberately *after* the vendored target above (SPIRV-Reflect): only the project's own targets inherit
# these, so UB inside a vendored library cannot abort our run and a vendored warning cannot be mistaken
# for ours. The Sanitize build is a statement about this repository's code.
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

# ── Deprecation enforcement ──────────────────────────────────────────────────
# The versioning policy leans on `[[deprecated("use X")]]` reaching the consumer's own build
# (docs/RenderGraph-Library-Design.md, "Versioning the library"). That only works if the library
# itself is forced to stop using the old spelling, so this project's targets turn the warning into an
# error. It is applied per target instead of with add_compile_options because FetchContent adds VMA
# and Catch2 after this point: a vendored target's deprecation warning is not ours to fail (#87).
function(muyo_enable_deprecation_errors target)
    if (UNIX)
        target_compile_options(${target} PRIVATE -Werror=deprecated-declarations)
    elseif (MSVC)
        # MSVC calls the same diagnostic C4996; enforce it in the Windows CI build as well.
        target_compile_options(${target} PRIVATE /we4996)
    endif()
endfunction()

# Vendored and SDK headers are SYSTEM: `-Wextra` checks this repository's code, not its dependencies', and a
# warning from `vk_mem_alloc.h` in every translation unit says nothing about us. Header names are unique
# between these roots and ours (checked: no basename occurs in both), so `-isystem` cannot change what an
# include resolves to - only which translation unit a diagnostic is attributed to. #53/#68 reshape this
# include surface; this split is what the `-Wextra` baseline needs until then.
include_directories(SYSTEM
    ${Vulkan_INCLUDE_DIR}
    ${SPIRV-Reflect_INCLUDE_DIRS}
    # RenderDoc's API is a single header, and `src/Debug/RenderDoc.h` is part of `muyo_debug`. It compiled
    # only because `tests` added this path for the vendor-specific test helper; the capture backend makes
    # it a real dependency of `muyo_debug`, so the path belongs here rather than in one consumer (#167).
    ${MUYO_ROOT}/thirdparty/RenderDoc
    )

    
#Enable CPPCHECK
# set(CMAKE_CXX_CPPCHECK "cppcheck")

# The device layer is its own target: instance, device, queues, memory, sync primitives, command pools and
# the debug-messenger hook. The graph links it directly rather than receiving Vulkan device code through
# the engine (#58). The glob stays for now because the surface is still being split; #53 owns turning it
# into a stated list.
file(GLOB muyo_device_SRC
    "src/RenderDevice/*.h"
    "src/RenderDevice/*.cpp"
    )

add_library(muyo_rg
    "src/RenderGraph/RenderGraphBuilder.cpp"
    "src/RenderGraph/CompiledRenderGraph.cpp"
    "src/RenderGraph/PipelineCache.cpp"
    "src/RenderGraph/ShaderCache.cpp"
    "src/RenderGraph/ShaderSourceProvider.cpp"
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

# `muyo_device` is the bottom of the stack (docs/RenderGraph-Library-Design.md §2): instance, device,
# queues, memory, sync primitives and command pools. It depends on Vulkan and VMA; a headless client links
# it without a window or a surface. Validation-layer and debug-messenger *policy* is deliberately not part
# of it; the device exposes the hook, the host decides (#58). STATIC for the same reason as the other
# targets here: a shared/exported artifact is the packaging decision (#73).
add_library(muyo_device STATIC ${muyo_device_SRC})
target_compile_features(muyo_device PUBLIC cxx_std_20)
target_compile_features(muyo_rg PUBLIC cxx_std_20)
muyo_enable_deprecation_errors(muyo_device)
muyo_enable_deprecation_errors(muyo_rg)

# SPIRV-Reflect is PUBLIC because `ShaderReflectionFetcher.h` moved here with the reflection code: a
# consumer of `muyo_device` that includes it compiles against `spirv_reflect.h`. The reflection and the
# VMA graph resources are device-level code (they talk to Vulkan, not to the renderer), which is what lets
# `muyo_rg` link the device alone (#272).
# `${Vulkan_LIBRARIES}` is a path on the build machine and would make the installed target
# non-relocatable, so only the build interface names it; the install interface names the imported
# `Vulkan::Vulkan` target, which `muyo_rgConfig.cmake` creates through `find_dependency(Vulkan)` (#73).
target_link_libraries(muyo_device PUBLIC
    $<BUILD_INTERFACE:${Vulkan_LIBRARIES}>
    $<INSTALL_INTERFACE:Vulkan::Vulkan>
    VulkanMemoryAllocator
    SPIRV-Reflect)

# `IsRayTracingSupported` is inline in the device's public header, so a consumer has to compile it with
# the same definition as the library's translation units. `add_compile_definitions` is directory-scoped
# and does not cross the target interface, so without this an RT build's consumer sees false while
# VkMemoryAllocator.cpp compiled the RT path.
if (FEATURE_RAY_TRACING)
    target_compile_definitions(muyo_device PUBLIC FEATURE_RAY_TRACING)
endif()

# The device's public headers include each other by bare name ("VkRenderDevice.h"), so `src/RenderDevice`
# is the whole include interface. `ScopedMarker` lives in `src/Debug/ScopedMarker.h` instead: it composes
# the host's capture-tool interface, which is host policy, and keeping it here made `muyo_device` depend
# on a header outside the device layer and an extra link edge (#275). A consumer of `muyo_device`
# therefore needs no `src` include and no debug-target link.
target_include_directories(muyo_device
    PUBLIC
        $<BUILD_INTERFACE:${MUYO_ROOT}/src/RenderDevice>
        # `VkGraphResourceAllocator.h` implements the graph-owned resource interface, whose header lives
        # in `src/RenderGraph`; a consumer that includes the allocator therefore needs that path. This is
        # an include edge, not a link edge: the interface is pure virtual and header-only, and
        # `muyo_device` links nothing from `muyo_rg` (#272).
        $<BUILD_INTERFACE:${MUYO_ROOT}/src>
        $<BUILD_INTERFACE:${MUYO_ROOT}/src/RenderGraph>
        # The installed layout mirrors the build one under `include/` (#73). `include/RenderDevice` is
        # the device's own bare-name root; `include` and `include/RenderGraph` are what
        # `VkGraphResourceAllocator.h` needs for `"RenderGraphResources.h"`.
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/RenderDevice>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/RenderGraph>
    )
# VMA is in VkMemoryAllocator.h's public interface, so a consumer compiles against its headers. So is
# SPIRV-Reflect: `ShaderReflectionFetcher.h` is a device public header and includes `spirv_reflect.h`
# itself. Both belong in the *build* interface as well, because a consumer that adds this project as a
# subdirectory does not inherit the directory-scoped `include_directories(... SYSTEM ...)` above - that is
# what made the renderer fail on `spirv_reflect.h` while Muyo's own tests compiled (#318).
target_include_directories(muyo_device SYSTEM PUBLIC
    $<BUILD_INTERFACE:${SPIRV-Reflect_INCLUDE_DIRS}>
    $<BUILD_INTERFACE:${vma_SOURCE_DIR}/include>)


# RenderGraphResourceDesc.h uses VmaMemoryUsage in a public struct, so VMA is part of muyo_rg's
# stated public interface whether or not that is desirable - a consumer cannot compile the surface
# without it. Removing the VMA vocabulary is #293; until then VMA has to be reachable, and
# docs/RenderGraph.md says so under "Public surface".
target_include_directories(muyo_rg SYSTEM PUBLIC $<BUILD_INTERFACE:${vma_SOURCE_DIR}/include>)

# The graph's own headers include each other by bare name, so `src/RenderGraph` is part of its interface,
# and `src` is what makes `RenderGraph/...` resolvable to a consumer that links the target. The generated
# directory is part of the interface for the same reason as the source ones: `MuyoVersion.h` includes
# `<muyo_rg/version.h>`, which only exists in the build tree. A consumer gets it by linking `muyo_rg`, not
# by adding the build directory itself.
#
# The install interface mirrors that layout under the prefix (#73): `include/RenderGraph` is the stated
# public surface, and `include` makes `RenderGraph/...` and the generated `muyo_rg/version.h` resolvable.
# There is deliberately no `Muyo/detail/RenderGraph` include directory: the builder's private state moved
# into an out-of-line pimpl (#297), so the public headers are self-contained and the internal graph
# headers are not installed at all.
target_include_directories(muyo_rg
    PUBLIC
        $<BUILD_INTERFACE:${MUYO_ROOT}/src/RenderGraph>
        $<BUILD_INTERFACE:${MUYO_ROOT}/src>
        $<BUILD_INTERFACE:${MUYO_RG_GENERATED_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/RenderGraph>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

# The graph depends on the device and on nothing else. Resource allocation is injected as
# `RenderGraph::IGraphResourceAllocator`, implemented by `muyo_device`'s `VkGraphResourceAllocator`;
# shader reflection lives in `muyo_device` too. The renderer link edge is gone (#272), and #53's header
# split/packaging is separate.
target_link_libraries(muyo_rg PUBLIC muyo_device)

# `muyo_debug` is the development-only support surface the remaining tests need: validation-layer policy,
# the capture-tool abstraction with its RenderDoc backend, and the graph trace lane layout. It is a
# separate small STATIC target rather than part of either library because the device layer deliberately
# keeps host policy out (#58), and the tests are its only consumer. Deliberately not installed or
# exported: nothing here is a consumer-facing guarantee. The renderer keeps its own copies of the
# host-policy half (#318).
add_library(muyo_debug STATIC
    "src/Debug/Validation.cpp"
    "src/Debug/CaptureTool.cpp"
    "src/Debug/RenderDocCaptureTool.cpp"
    "src/Debug/RenderGraphLaneLayout.cpp"
    )
target_compile_features(muyo_debug PUBLIC cxx_std_20)
# `src` is what makes `Debug/...` resolvable, and it is the include root the tests are written against.
# `src/Debug` is private: the kept debug sources include each other by bare name ("CaptureTool.h").
target_include_directories(muyo_debug
    PUBLIC
        $<BUILD_INTERFACE:${MUYO_ROOT}/src>
    PRIVATE
        $<BUILD_INTERFACE:${MUYO_ROOT}/src/Debug>
    )
# `src/Debug/RenderDoc.h` includes `renderdoc_app.h`, so the vendored header root is part of the
# target's interface.
target_include_directories(muyo_debug SYSTEM PUBLIC $<BUILD_INTERFACE:${MUYO_ROOT}/thirdparty/RenderDoc>)
target_link_libraries(muyo_debug PUBLIC muyo_device)
muyo_enable_deprecation_errors(muyo_debug)

if(MUYO_RG_INSTALL)
    # ── Install rules and the `muyo_rg` CMake package (#73) ──────────────────────
    #
    # A consumer takes the library from an installed prefix:
    #
    #     find_package(muyo_rg 0.1 REQUIRED)
    #     target_link_libraries(app PRIVATE Muyo::muyo_rg)
    #
    # What ships is a stated set rather than "everything under a directory": exactly the public graph
    # headers, which are the same names as `PUBLIC_HEADERS` in `scripts/render_graph_docs.py` (#53), the
    # docs check compares the two lists, and `--check` also fails when a public header includes an
    # internal one. The internal graph headers are not installed: the builder's private state is an
    # out-of-line pimpl (#297), so the public headers no longer need them, and there is no
    # `Muyo/detail/RenderGraph` directory in the package.
    set(MUYO_RG_PUBLIC_HEADERS
        CompiledRenderGraph.h
        MuyoVersion.h
        PipelineCache.h
        PSODesc.h
        RenderGraphBuilder.h
        RenderGraphExecuteInfo.h
        RenderGraphExecutionPlan.h
        RenderGraphNodeContext.h
        RenderGraphNodeResource.h
        RenderGraphResourceDesc.h
        RenderGraphResourceHandle.h
        RenderGraphResources.h
        RenderGraphShader.h
        RenderGraphTrace.h
        ShaderCache.h
        ShaderSourceProvider.h
        TransientMemoryPlan.h
        )
    set(MUYO_RG_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/muyo_rg")

    foreach(header IN LISTS MUYO_RG_PUBLIC_HEADERS)
        install(FILES "${MUYO_ROOT}/src/RenderGraph/${header}"
                DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/RenderGraph")
    endforeach()

    # `MuyoVersion.h` includes `<muyo_rg/version.h>`, which is generated into the build tree, so the
    # generated header is part of the installed surface too (#87).
    install(FILES "${MUYO_RG_GENERATED_DIR}/muyo_rg/version.h"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/muyo_rg")

    # The device layer ships with the graph (`docs/RenderGraph-Library-Design.md` section 2, "Both ship").
    # It has no stated public/internal split yet, so its target's headers are its interface; the graph's
    # public headers directly need DescriptorHeapService.h, PipelineStateBuilder.h, ShaderReflection.h,
    # ShaderReflectionFetcher.h and VkExtFuncsLoader.h, `RenderGraphBuilder`'s convenience constructor
    # names VkRenderDevice.h, and the rest complete the device class surface (VkMemoryAllocator /
    # VkGraphResourceAllocator).
    file(GLOB MUYO_DEVICE_HEADERS "${MUYO_ROOT}/src/RenderDevice/*.h")
    install(FILES ${MUYO_DEVICE_HEADERS} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/RenderDevice")

    install(TARGETS muyo_rg muyo_device EXPORT muyo_rgTargets
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

    # VMA and SPIRV-Reflect are part of `muyo_device`'s public interface (the graph's public headers name
    # `VmaMemoryUsage` and `SpvReflectShaderModule`), so the package ships both as exported targets rather
    # than making the consumer find a copy of either. Vulkan is the one dependency left to the consumer,
    # and `muyo_rgConfig.cmake` reports it at configure time.
    install(TARGETS SPIRV-Reflect EXPORT muyo_rgTargets
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    target_include_directories(SPIRV-Reflect PUBLIC
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Muyo/detail/thirdparty>)
    install(FILES "${MUYO_ROOT}/thirdparty/SPIRV-Reflect/spirv_reflect.h"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/Muyo/detail/thirdparty")
    # `spirv_reflect.h` includes its pinned SPIR-V headers as `"./include/spirv/unified1/spirv.h"`, relative
    # to itself. Installing the header without this tree made the out-of-tree probe fail at its first
    # include; the relative layout is part of the header's interface, so the tree ships beside it.
    install(DIRECTORY "${MUYO_ROOT}/thirdparty/SPIRV-Reflect/include/"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/Muyo/detail/thirdparty/include")

    install(TARGETS VulkanMemoryAllocator EXPORT muyo_rgTargets)
    target_include_directories(VulkanMemoryAllocator INTERFACE
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Muyo/detail/thirdparty>)
    install(FILES "${vma_SOURCE_DIR}/include/vk_mem_alloc.h"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/Muyo/detail/thirdparty")

    # The package files: the export set (`Muyo::muyo_rg` and its `Muyo::muyo_device` dependency), the
    # config that finds Vulkan and reports the version, and the version file. `SameMinorVersion` is the
    # 0.x answer recorded in `docs/RenderGraph-Library-Design.md`: while the major is 0 a minor release may
    # break, so `find_package(muyo_rg 0.1)` must not accept a 0.2 package; at 1.0 the policy's
    # `SameMajorVersion` applies.
    configure_package_config_file(
        "${MUYO_ROOT}/cmake/muyo_rgConfig.cmake.in"
        "${MUYO_BINARY_ROOT}/muyo_rgConfig.cmake"
        INSTALL_DESTINATION "${MUYO_RG_INSTALL_CMAKEDIR}")
    write_basic_package_version_file(
        "${MUYO_BINARY_ROOT}/muyo_rgConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMinorVersion)
    install(FILES
            "${MUYO_BINARY_ROOT}/muyo_rgConfig.cmake"
            "${MUYO_BINARY_ROOT}/muyo_rgConfigVersion.cmake"
            DESTINATION "${MUYO_RG_INSTALL_CMAKEDIR}")
    install(EXPORT muyo_rgTargets
            NAMESPACE Muyo::
            FILE muyo_rgTargets.cmake
            DESTINATION "${MUYO_RG_INSTALL_CMAKEDIR}")
endif()

message(STATUS "runtime dir" ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# Everything from here to the end of the docs block is a development concern: it declares targets a
# consumer has no reason to build and, in the case of Catch2, fetches one over the network. Kept out
# of an embedder's configure entirely (#68 review A1). Shaders and assets are not here either: they
# moved to `muyo-renderer` with the pipeline they served (#318), so configuring this project needs no
# `slangc` or `glslangValidator`.
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
    src/tests/ErrorReportingTests.cpp
    src/tests/PSODescTest.cpp
    src/tests/RenderGraphLaneLayoutTest.cpp
    src/tests/RenderGraphTraceTest.cpp
    src/tests/TransientMemoryPlannerTest.cpp
)
message(STATUS "Catch2 source dir: ${catch2_SOURCE_DIR}")
target_include_directories(tests PRIVATE ${catch2_SOURCE_DIR}/src thirdparty/RenderDoc)
# The two libraries and the debug support target provide every source this binary needs. `muyo_rg`
# links `muyo_device`, and `muyo_device` links `SPIRV-Reflect`, so no source is compiled twice and the
# link line names no vendored target directly (#76). The renderer's sources and tests left with the
# `muyo` target (#318), so this binary covers `muyo_rg` and `muyo_device` only.
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain muyo_rg muyo_device muyo_debug)
muyo_enable_deprecation_errors(tests)
include(Catch)

# The graph's own test binary: it links `muyo_rg` and `muyo_device` and nothing else. This is
# issue #272's "`muyo_rg` builds and its tests run without linking the engine" as an executable rather than
# a claim: a reintroduced engine dependency fails this link.
add_executable(rg_tests
    src/tests/RenderGraphStandaloneTests.cpp
    src/tests/MuyoVersionTest.cpp)
target_include_directories(rg_tests PRIVATE ${catch2_SOURCE_DIR}/src)
target_link_libraries(rg_tests PRIVATE Catch2::Catch2WithMain muyo_rg muyo_device)
# The expected version comes from `project(Muyo VERSION ...)` independently of the configured header,
# so a generated header that disagrees with the project version fails the test instead of drifting
# silently (#87). The quotes are escaped so the definition reaches the compiler as a string literal.
target_compile_definitions(rg_tests PRIVATE
    MUYO_RG_EXPECTED_MAJOR=${PROJECT_VERSION_MAJOR}
    MUYO_RG_EXPECTED_MINOR=${PROJECT_VERSION_MINOR}
    MUYO_RG_EXPECTED_PATCH=${PROJECT_VERSION_PATCH}
    MUYO_RG_EXPECTED_STRING=\"${PROJECT_VERSION}\")
muyo_enable_deprecation_errors(rg_tests)
catch_discover_tests(rg_tests)

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

endif() # MUYO_TOP_LEVEL: tests and docs check
