cmake_minimum_required(VERSION 3.10)
project(eventheader-tracepoint)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_subdirectory(include)

if(NOT WIN32)

    if(NOT TARGET tracepoint-headers)
        find_package(tracepoint-headers)
    endif()

    if(NOT TARGET tracepoint)
        find_package(tracepoint)
    endif()

    add_subdirectory(src)
    add_subdirectory(samples)

endif()
