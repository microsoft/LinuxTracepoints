add_executable(control-lookup
    control-lookup.cpp)
target_link_libraries(control-lookup
    PUBLIC tracepoint-control)
target_compile_features(control-lookup
    PRIVATE cxx_std_17)

add_executable(control-session
    control-session.cpp)
target_link_libraries(control-session
    PUBLIC tracepoint-control tracepoint-decode)
target_compile_features(control-session
    PRIVATE cxx_std_17)
