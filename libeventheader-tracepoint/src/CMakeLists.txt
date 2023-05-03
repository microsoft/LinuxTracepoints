# eventheader-tracepoint = libeventheader-tracepoint, EVENTHEADER_HEADERS
add_library(eventheader-tracepoint
    eventheader-tracepoint.c)
target_link_libraries(eventheader-tracepoint
    PUBLIC eventheader-headers tracepoint-headers)
install(TARGETS eventheader-tracepoint
    EXPORT eventheader-tracepointTargets)
install(EXPORT eventheader-tracepointTargets
    FILE "eventheader-tracepointTargets.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/eventheader-tracepoint")
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/eventheader-tracepointConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/eventheader-tracepointConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/eventheader-tracepoint"
    NO_SET_AND_CHECK_MACRO
    NO_CHECK_REQUIRED_COMPONENTS_MACRO)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/eventheader-tracepointConfig.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/eventheader-tracepoint")
