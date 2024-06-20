add_executable(perf-decode
    perf-decode.cpp)
target_link_libraries(perf-decode
    eventheader-decode
    tracepoint-decode)
target_compile_features(perf-decode
    PRIVATE cxx_std_17)
install(TARGETS perf-decode)
