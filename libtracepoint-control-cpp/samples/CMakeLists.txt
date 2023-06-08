add_executable(control-lookup
    control-lookup.cpp)
target_link_libraries(control-lookup
    PUBLIC tracepoint-control)
target_compile_features(control-lookup
    PRIVATE cxx_std_17)
