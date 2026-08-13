cmake_minimum_required(VERSION 3.10)
project(DMA-Memory-Editor LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_VERBOSE_MAKEFILE ON)  # Helps debugging

# Set directories
set(LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib)

    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/memory)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/debugger)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/memory/list)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/memory/region)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/memory/search)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/core)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/gui)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/gui/imgui)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/gui/render)

# Source files (keep as is)
set(SOURCES
    ./main.cpp
    ./memory/memory.cpp
    ./memory/list/list.cpp
    ./memory/region/region.cpp
    ./memory/search/search.cpp
    ./core/core.cpp
    ./debugger/debugger.cpp
    ./gui/menu.cpp
    ./gui/imgui/imgui.cpp
    ./gui/imgui/imgui_demo.cpp
    ./gui/imgui/imgui_draw.cpp
    ./gui/imgui/imgui_impl_glfw.cpp
    ./gui/imgui/imgui_impl_vulkan.cpp
    ./gui/imgui/imgui_tables.cpp
    ./gui/imgui/imgui_widgets.cpp
    ./gui/render/render.cpp
)

# Create executable
add_executable(DMA-Memory-Editor ${SOURCES})

# Compile definitions
target_compile_definitions(DMA-Memory-Editor PRIVATE
    LINUX
    _FILE_OFFSET_BITS=64
    _FORTIFY_SOURCE=2
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/vmm.so
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${LIB_DIR}/vmm.so
        ${CMAKE_CURRENT_BINARY_DIR}/vmm.so
    COMMENT "Copying vmm.so to build directory"
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/leechcore.so
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${LIB_DIR}/leechcore.so
        ${CMAKE_CURRENT_BINARY_DIR}/leechcore.so
    COMMENT
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/leechcore_device_qemu.so
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${LIB_DIR}/leechcore_device_qemu.so
        ${CMAKE_CURRENT_BINARY_DIR}/leechcore_device_qemu.so
    COMMENT
)

add_custom_target(copied_libs ALL
    DEPENDS
        ${CMAKE_CURRENT_BINARY_DIR}/vmm.so
        ${CMAKE_CURRENT_BINARY_DIR}/leechcore.so
        ${CMAKE_CURRENT_BINARY_DIR}/leechcore_device_qemu.so
)

add_dependencies(DMA-Memory-Editor copied_libs)

add_library(libZydis STATIC IMPORTED)
set_target_properties(libZydis PROPERTIES
    IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/lib/libZydis.a
)

target_link_libraries(DMA-Memory-Editor
    -L${CMAKE_CURRENT_BINARY_DIR}
    -l:vmm.so
    -l:leechcore.so
    -l:leechcore_device_qemu.so

    # System libraries
    pthread
    dl
    m
    glfw
    vulkan
    fuse
)

# ===== LINKER FLAGS =====
target_link_options(DMA-Memory-Editor PRIVATE
    -Wl,-z,noexecstack
    -Wl,-rpath,'$$ORIGIN'  # Look for libraries next to executable
)

# Set RPATH
set_target_properties(DMA-Memory-Editor PROPERTIES
    BUILD_RPATH "\$ORIGIN"
    INSTALL_RPATH "\$ORIGIN"
)

