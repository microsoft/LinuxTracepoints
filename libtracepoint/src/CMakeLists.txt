# tracepoint = libtracepoint, TRACEPOINT_HEADERS

add_library(tracepoint
    tracepoint.c)
target_link_libraries(tracepoint
    PUBLIC tracepoint-headers)
install(TARGETS tracepoint
    EXPORT tracepointTargets)
install(EXPORT tracepointTargets
    FILE "tracepointTargets.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tracepoint")
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tracepointConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/tracepointConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tracepoint"
    NO_SET_AND_CHECK_MACRO
    NO_CHECK_REQUIRED_COMPONENTS_MACRO)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/tracepointConfig.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tracepoint")
