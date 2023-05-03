cmake_minimum_required(VERSION 3.10)
project(tracepoint-headers)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# tracepoint-headers = TRACEPOINT_HEADERS
add_library(tracepoint-headers INTERFACE)
target_include_directories(tracepoint-headers
    INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
set(TRACEPOINT_HEADERS
    "${PROJECT_SOURCE_DIR}/tracepoint/tracepoint.h"
    "${PROJECT_SOURCE_DIR}/tracepoint/tracepoint-impl.h"
    "${PROJECT_SOURCE_DIR}/tracepoint/tracepoint-state.h")
set_target_properties(tracepoint-headers PROPERTIES
    PUBLIC_HEADER "${TRACEPOINT_HEADERS}")
install(TARGETS tracepoint-headers
    EXPORT tracepoint-headersTargets
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tracepoint)
install(EXPORT tracepoint-headersTargets
    FILE "tracepoint-headersTargets.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tracepoint-headers")
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tracepoint-headersConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/tracepoint-headersConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tracepoint-headers"
    NO_SET_AND_CHECK_MACRO
    NO_CHECK_REQUIRED_COMPONENTS_MACRO)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/tracepoint-headersConfig.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tracepoint-headers")
