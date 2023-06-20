cmake_minimum_required(VERSION 3.10)
project(eventheader-decode-cpp
    VERSION 1.1.0
    DESCRIPTION "EventHeader tracepoint decoding for C/C++"
    HOMEPAGE_URL "https://github.com/microsoft/LinuxTracepoints"
    LANGUAGES CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(BUILD_SAMPLES ON CACHE BOOL "Build sample code")
set(BUILD_TOOLS ON CACHE BOOL "Build tool code")

if(NOT TARGET tracepoint-decode)
    find_package(tracepoint-decode 1.1 REQUIRED)
endif()

if(NOT TARGET eventheader-headers)
    find_package(eventheader-headers 1.1 REQUIRED)
endif()

if(WIN32)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wformat
        -Wformat-security
        -Werror=format-security
        -Wstack-protector
        -Werror=stack-protector)
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-D_FORTIFY_SOURCE=2)
    endif()
endif()

add_subdirectory(src)

if(BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

if(BUILD_TOOLS)
    add_subdirectory(tools)
endif()
