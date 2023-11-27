cmake_minimum_required(VERSION 3.10)
project(eventheader-decode-dotnet
    VERSION 1.3.0
    DESCRIPTION "EventHeader tracepoint decoding for .NET"
    HOMEPAGE_URL "https://github.com/microsoft/LinuxTracepoints"
    LANGUAGES CSharp)
include(GNUInstallDirs)
set(BUILD_SAMPLES ON CACHE BOOL "Build sample code")

add_subdirectory(src)

if(BUILD_SAMPLES)
    add_subdirectory(samples)
endif()
