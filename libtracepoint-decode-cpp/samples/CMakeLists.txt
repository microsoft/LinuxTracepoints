add_executable(tracepoint-perf-file-rewrite-sample
    perf-file-rewrite-sample.cpp)
target_link_libraries(tracepoint-perf-file-rewrite-sample
    tracepoint-decode)
target_compile_features(tracepoint-perf-file-rewrite-sample
    PRIVATE cxx_std_17)
