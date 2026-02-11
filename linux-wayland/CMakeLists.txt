cmake_minimum_required(VERSION 3.9)

project(jwm LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT JWM_ARCH)
    if ("${CMAKE_SYSTEM_PROCESSOR}" STREQUAL "arm64")
        set(JWM_ARCH "arm64")
    else()
        set(JWM_ARCH "x64")
    endif()
endif()

file(GLOB SOURCES_CXX ${CMAKE_CURRENT_LIST_DIR}/../shared/cc/*.cc ${CMAKE_CURRENT_LIST_DIR}/cc/*.cc ${CMAKE_CURRENT_LIST_DIR}/cc/*.c)
file(GLOB SOURCES_CXX_IMPL ${CMAKE_CURRENT_LIST_DIR}/../shared/cc/impl/*.cc)
set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
set(XDG_OUTPUT_PROTOCOL_XML ${CMAKE_CURRENT_LIST_DIR}/protocols/xdg-output-unstable-v1.xml)
set(XDG_OUTPUT_PROTOCOL_HEADER ${GENERATED_DIR}/xdg-output-unstable-v1-client-protocol.hh)
set(GENERATED_XDG_OUTPUT_PROTOCOL ${GENERATED_DIR}/xdg-output-unstable-v1-protocol.c)
set(XDG_SHELL_PROTOCOL_XML ${CMAKE_CURRENT_LIST_DIR}/protocols/xdg-shell.xml)
set(XDG_SHELL_PROTOCOL_HEADER ${GENERATED_DIR}/xdg-shell-client-protocol.hh)
set(GENERATED_XDG_SHELL_PROTOCOL ${GENERATED_DIR}/xdg-shell-protocol.c)

add_custom_command(
    OUTPUT ${XDG_OUTPUT_PROTOCOL_HEADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${WAYLAND_SCANNER} client-header ${XDG_OUTPUT_PROTOCOL_XML} ${XDG_OUTPUT_PROTOCOL_HEADER}
    DEPENDS ${XDG_OUTPUT_PROTOCOL_XML}
    VERBATIM
)

add_custom_command(
    OUTPUT ${GENERATED_XDG_OUTPUT_PROTOCOL}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${WAYLAND_SCANNER} public-code ${XDG_OUTPUT_PROTOCOL_XML} ${GENERATED_XDG_OUTPUT_PROTOCOL}
    DEPENDS ${XDG_OUTPUT_PROTOCOL_XML}
    VERBATIM
)

add_custom_command(
    OUTPUT ${XDG_SHELL_PROTOCOL_HEADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${WAYLAND_SCANNER} client-header ${XDG_SHELL_PROTOCOL_XML} ${XDG_SHELL_PROTOCOL_HEADER}
    DEPENDS ${XDG_SHELL_PROTOCOL_XML}
    VERBATIM
)

add_custom_command(
    OUTPUT ${GENERATED_XDG_SHELL_PROTOCOL}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GENERATED_DIR}
    COMMAND ${WAYLAND_SCANNER} public-code ${XDG_SHELL_PROTOCOL_XML} ${GENERATED_XDG_SHELL_PROTOCOL}
    DEPENDS ${XDG_SHELL_PROTOCOL_XML}
    VERBATIM
)

add_custom_target(wayland-protocols-codegen
    DEPENDS
      ${XDG_OUTPUT_PROTOCOL_HEADER}
      ${GENERATED_XDG_OUTPUT_PROTOCOL}
      ${XDG_SHELL_PROTOCOL_HEADER}
      ${GENERATED_XDG_SHELL_PROTOCOL}
)

add_library(jwm SHARED ${SOURCES_OBJC} ${SOURCES_CXX} ${SOURCES_CXX_IMPL} ${GENERATED_XDG_OUTPUT_PROTOCOL} ${GENERATED_XDG_SHELL_PROTOCOL})
add_dependencies(jwm wayland-protocols-codegen)
find_library(WAYLAND_CLIENT_LIB wayland-client HINTS ENV LD_LIBRARY_PATH REQUIRED)

set(JAVA_HOME $ENV{JAVA_HOME})
if (NOT JAVA_HOME)
    file(GLOB JAVA_HOMES "/usr/lib/jvm/java-*")
    if (JAVA_HOMES)
        list(GET JAVA_HOMES 0 JAVA_HOME)
        message(STATUS "Java home found automatically at ${JAVA_HOME}. Set JAVA_HOME environment variable to override.")
    else()
        message(FATAL_ERROR "Java home not found! Please set JAVA_HOME environment variable.")
    endif()
endif()

target_include_directories(jwm PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../shared/cc ${JAVA_HOME}/include ${JAVA_HOME}/include/linux ${GENERATED_DIR})
set_target_properties(jwm PROPERTIES OUTPUT_NAME "jwm_wayland_${JWM_ARCH}")
target_link_libraries(jwm PRIVATE ${WAYLAND_CLIENT_LIB})
