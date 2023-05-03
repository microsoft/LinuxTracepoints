cmake_minimum_required(VERSION 3.10)
project(eventheader-decode-cpp)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(NOT TARGET eventheader-headers)
    find_package(eventheader-headers)
endif()

add_subdirectory(src)
add_subdirectory(samples)
