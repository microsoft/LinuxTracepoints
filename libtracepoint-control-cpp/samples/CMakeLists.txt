add_executable(tracepoint-metadata-lookup-sample
    metadata-lookup-sample.cpp)
target_link_libraries(tracepoint-metadata-lookup-sample
    PUBLIC tracepoint-control)
target_compile_features(tracepoint-metadata-lookup-sample
    PRIVATE cxx_std_17)

add_executable(tracepoint-control-session-sample
    control-session-sample.cpp)
target_link_libraries(tracepoint-control-session-sample
    PUBLIC tracepoint-control tracepoint-decode)
target_compile_features(tracepoint-control-session-sample
    PRIVATE cxx_std_17)

add_executable(tracepoint-save-session-sample
    save-session-sample.cpp)
target_link_libraries(tracepoint-save-session-sample
    PUBLIC tracepoint-control tracepoint-decode)
target_compile_features(tracepoint-save-session-sample
    PRIVATE cxx_std_17)

if(TARGET eventheader-decode)
    set(eventheader-decode_FOUND TRUE)
else()
    find_package(eventheader-decode)
endif()

if(eventheader-decode_FOUND)

    add_executable(tracepoint-circular-snap-sample
        circular-snap-sample.cpp)
    target_link_libraries(tracepoint-circular-snap-sample
        PUBLIC tracepoint-control tracepoint-decode eventheader-decode)
    target_compile_features(tracepoint-circular-snap-sample
        PRIVATE cxx_std_17)

endif()
