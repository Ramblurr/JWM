cmake_minimum_required(VERSION 3.10)
project(jwm-project LANGUAGES CXX)

####################################################################
# Platform specific config
# Add subdirectories here to enable clion project index and support

if(WIN32)
    add_subdirectory(windows)
    message(STATUS "Configure windows platform module")
endif()

if(APPLE)
    add_subdirectory(macos)
    message(STATUS "Configure macos platform module")
endif()


if (UNIX AND NOT APPLE)
    add_subdirectory(linux-x11)
    add_subdirectory(linux-wayland)
    message(STATUS "Configure linux-x11 platform module")
    message(STATUS "Configure linux-wayland platform module")
endif()
