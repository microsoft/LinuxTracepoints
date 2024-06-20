add_executable(perf-collect
    perf-collect.cpp)
target_link_libraries(perf-collect
    tracepoint-control tracepoint-decode)
target_compile_features(perf-collect
    PRIVATE cxx_std_17)
install(TARGETS perf-collect)
