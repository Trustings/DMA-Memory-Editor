cmake_minimum_required(VERSION 3.16)
project(DMA-Memory-Editor LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

set(SOURCES
    main.cpp
    core/core.cpp
    core/config.cpp
    memory/memory.cpp
    memory/list/list.cpp
    memory/region/region.cpp
    memory/search/search.cpp
    gui/menu.cpp
    gui/render/render.cpp
    # Dear ImGui, from the upstream layout. The project used to also carry a
    # second, byte-identical copy of the two backend files at the top level of
    # gui/imgui and build those instead.
    gui/imgui/imgui.cpp
    gui/imgui/imgui_demo.cpp
    gui/imgui/imgui_draw.cpp
    gui/imgui/imgui_tables.cpp
    gui/imgui/imgui_widgets.cpp
    gui/imgui/backends/imgui_impl_glfw.cpp
    gui/imgui/backends/imgui_impl_vulkan.cpp
)

if(UNIX AND NOT APPLE)
    list(APPEND SOURCES debugger/debugger.cpp)
endif()

add_executable(DMA-Memory-Editor ${SOURCES})

target_include_directories(DMA-Memory-Editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/memory
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/list
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/region
    ${CMAKE_CURRENT_SOURCE_DIR}/memory/search
    ${CMAKE_CURRENT_SOURCE_DIR}/debugger
    ${CMAKE_CURRENT_SOURCE_DIR}/gui
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/imgui/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/gui/render
)

# _FORTIFY_SOURCE only does anything with optimization on, and defining it at
# -O0 produces a warning on every translation unit.
target_compile_definitions(DMA-Memory-Editor PRIVATE
    # Required by the vendored MemProcFS headers: vmmdll.h gates its Linux
    # type definitions on LINUX. memory.hpp also defines it defensively so a
    # non-CMake build still works.
    $<$<PLATFORM_ID:Linux>:LINUX>
    GLFW_INCLUDE_NONE
    _FILE_OFFSET_BITS=64
    $<$<NOT:$<CONFIG:Debug>>:_FORTIFY_SOURCE=2>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(DMA-Memory-Editor PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -Wno-missing-field-initializers
    )
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

find_package(Threads REQUIRED)

# Prefer a system Vulkan SDK; fall back to the headers vendored in include/.
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    message(STATUS "Using system Vulkan: ${Vulkan_LIBRARIES}")
    target_link_libraries(DMA-Memory-Editor PRIVATE Vulkan::Vulkan)
else()
    message(STATUS "System Vulkan not found; using bundled headers and -lvulkan")
    target_link_libraries(DMA-Memory-Editor PRIVATE vulkan)
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GLFW QUIET glfw3)
endif()
if(GLFW_FOUND)
    target_include_directories(DMA-Memory-Editor PRIVATE ${GLFW_INCLUDE_DIRS})
    target_link_libraries(DMA-Memory-Editor PRIVATE ${GLFW_LIBRARIES})
else()
    target_link_libraries(DMA-Memory-Editor PRIVATE glfw)
endif()

target_link_libraries(DMA-Memory-Editor PRIVATE
    Threads::Threads
    ${CMAKE_DL_LIBS}
    m
)

# ---------------------------------------------------------------------------
# MemProcFS / LeechCore
# ---------------------------------------------------------------------------
# These are prebuilt shared objects checked into lib/. They are copied next to
# the executable and found there at runtime through an $ORIGIN rpath.

set(VMM_LIBS vmm.so leechcore.so leechcore_device_qemu.so)

foreach(libname IN LISTS VMM_LIBS)
    if(NOT EXISTS ${LIB_DIR}/${libname})
        message(FATAL_ERROR
            "Missing ${LIB_DIR}/${libname}.\n"
            "Build MemProcFS/LeechCore yourself (recommended) or fetch the\n"
            "release archive as described in the README, and place the\n"
            "resulting .so files in lib/.")
    endif()
endforeach()

set(COPIED_LIBS "")
foreach(libname IN LISTS VMM_LIBS)
    add_custom_command(
        OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/${libname}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${LIB_DIR}/${libname}
                ${CMAKE_CURRENT_BINARY_DIR}/${libname}
        DEPENDS ${LIB_DIR}/${libname}
        COMMENT "Copying ${libname} to the build directory")
    list(APPEND COPIED_LIBS ${CMAKE_CURRENT_BINARY_DIR}/${libname})
endforeach()

add_custom_target(copied_libs ALL DEPENDS ${COPIED_LIBS})
add_dependencies(DMA-Memory-Editor copied_libs)

# These have to be linked by exact file name. They are named vmm.so and
# leechcore.so rather than libvmm.so/libleechcore.so, so neither a plain path
# nor an IMPORTED target works: CMake decides the file name is not a valid
# library name and rewrites it to "-L lib -lvmm", which then fails because no
# libvmm.so exists. GNU ld's -l:<exact-name> syntax is the way to say what we
# actually mean, and it records the plain file name as DT_NEEDED, which the
# $ORIGIN rpath below resolves against the copies next to the executable.
target_link_libraries(DMA-Memory-Editor PRIVATE
    -L${LIB_DIR}
    -l:vmm.so
    -l:leechcore.so
)

target_link_options(DMA-Memory-Editor PRIVATE
    -Wl,-z,noexecstack
    -Wl,--disable-new-dtags
)

# Use the install rpath at build time too, so CMake does not append the
# absolute build-machine path of lib/ to the binary's RPATH.
set_target_properties(DMA-Memory-Editor PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    BUILD_RPATH              "$ORIGIN"
    INSTALL_RPATH            "$ORIGIN"
)
