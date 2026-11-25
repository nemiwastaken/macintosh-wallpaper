cmake_minimum_required(VERSION 3.20)
project(macintosh-wallpaper C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(OpenGL REQUIRED)
find_package(PNG REQUIRED)
find_package(glfw3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)

set(SOURCES
    src/main.cpp
    src/image.cpp
    src/font.cpp
    src/theme.cpp
    src/program.cpp
    src/scene.cpp
    src/renderer.cpp
    src/overlay.cpp
)

# Optional Wayland wallpaper backend (wlr-layer-shell + EGL).
find_program(WAYLAND_SCANNER wayland-scanner)
pkg_check_modules(WAYLAND IMPORTED_TARGET wayland-client wayland-egl egl)
if(WAYLAND_SCANNER AND WAYLAND_FOUND)
    set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/protocols)
    set(PROTO_GEN ${CMAKE_CURRENT_BINARY_DIR}/protocols)
    file(MAKE_DIRECTORY ${PROTO_GEN})
    add_custom_command(
        OUTPUT ${PROTO_GEN}/wlr-layer-shell-unstable-v1-client-protocol.h
               ${PROTO_GEN}/wlr-layer-shell-unstable-v1-protocol.c
               ${PROTO_GEN}/xdg-shell-client-protocol.h
               ${PROTO_GEN}/xdg-shell-protocol.c
        COMMAND ${WAYLAND_SCANNER} client-header
                ${PROTO_DIR}/wlr-layer-shell-unstable-v1.xml
                ${PROTO_GEN}/wlr-layer-shell-unstable-v1-client-protocol.h
        COMMAND ${WAYLAND_SCANNER} private-code
                ${PROTO_DIR}/wlr-layer-shell-unstable-v1.xml
                ${PROTO_GEN}/wlr-layer-shell-unstable-v1-protocol.c
        COMMAND ${WAYLAND_SCANNER} client-header
                ${PROTO_DIR}/xdg-shell.xml ${PROTO_GEN}/xdg-shell-client-protocol.h
        COMMAND ${WAYLAND_SCANNER} private-code
                ${PROTO_DIR}/xdg-shell.xml ${PROTO_GEN}/xdg-shell-protocol.c
        DEPENDS ${PROTO_DIR}/wlr-layer-shell-unstable-v1.xml ${PROTO_DIR}/xdg-shell.xml
        COMMENT "Generating wlr-layer-shell and xdg-shell protocol code")
    list(APPEND SOURCES src/layer_shell.cpp
         ${PROTO_GEN}/wlr-layer-shell-unstable-v1-protocol.c
         ${PROTO_GEN}/xdg-shell-protocol.c)
    set(LAYER_SHELL TRUE)
else()
    message(STATUS "Wayland layer-shell backend disabled (missing wayland-scanner or wayland-client)")
endif()

add_executable(macintosh-wallpaper ${SOURCES})

target_include_directories(macintosh-wallpaper PRIVATE src)
if(LAYER_SHELL)
    target_include_directories(macintosh-wallpaper PRIVATE ${PROTO_GEN})
    target_compile_definitions(macintosh-wallpaper PRIVATE HAVE_LAYER_SHELL=1)
endif()
target_link_libraries(macintosh-wallpaper PRIVATE
    OpenGL::GL PNG::PNG glfw PkgConfig::EPOXY)
if(LAYER_SHELL)
    target_link_libraries(macintosh-wallpaper PRIVATE PkgConfig::WAYLAND)
endif()

target_compile_definitions(macintosh-wallpaper PRIVATE
    RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/resources"
    SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders")

install(TARGETS macintosh-wallpaper RUNTIME DESTINATION bin)
install(DIRECTORY shaders DESTINATION share/macintosh-wallpaper)
install(FILES packaging/macintosh-wallpaper.desktop DESTINATION share/applications OPTIONAL)
