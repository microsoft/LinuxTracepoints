cmake_minimum_required(VERSION 3.10)
project(tracepoint)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_subdirectory(include)

if(NOT WIN32)
    add_subdirectory(src)
    add_subdirectory(samples)
    add_subdirectory(utest)
endif()
