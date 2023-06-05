add_executable(dynamic-sample
    dynamic-sample.cpp)
target_link_libraries(dynamic-sample
    PUBLIC eventheader-tracepoint tracepoint)
target_compile_features(dynamic-sample
    PRIVATE cxx_std_17)

add_executable(eventheader-sample
    sample.cpp)
target_link_libraries(eventheader-sample
    PUBLIC eventheader-tracepoint tracepoint)

add_executable(eventheader-interceptor-sample
    interceptor-sample.cpp
    TestProviderC.c
    TestProviderCpp.cpp
    tracepoint-file.cpp)
target_link_libraries(eventheader-interceptor-sample
    PUBLIC eventheader-tracepoint)
target_compile_features(eventheader-interceptor-sample
    PRIVATE cxx_std_17)
