add_executable(eventheader-dynamic-sample
    dynamic-sample.cpp)
target_link_libraries(eventheader-dynamic-sample
    PUBLIC eventheader-tracepoint tracepoint)
target_compile_features(eventheader-dynamic-sample
    PRIVATE cxx_std_17)

add_executable(eventheader-provider-sample
    provider-sample.cpp)
target_link_libraries(eventheader-provider-sample
    PUBLIC eventheader-tracepoint tracepoint)
target_compile_features(eventheader-provider-sample
    PRIVATE cxx_std_11)

add_executable(eventheader-interceptor-sample
    interceptor-sample.cpp
    InterceptToFile.cpp)
target_link_libraries(eventheader-interceptor-sample
    PUBLIC eventheader-tracepoint)
target_compile_features(eventheader-interceptor-sample
    PRIVATE cxx_std_17 c_std_11)
