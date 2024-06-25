add_executable(tracepoint-api-utest
    api-utest.cpp)
target_compile_features(tracepoint-api-utest
    PRIVATE cxx_std_11)
target_link_libraries(tracepoint-api-utest
    PUBLIC tracepoint)
add_test(NAME tracepoint-api-utest
    COMMAND tracepoint-api-utest)

add_executable(tracepoint-provider-utest
    provider-utest-c.c
    provider-utest-cpp.cpp)
target_compile_features(tracepoint-provider-utest
    PRIVATE cxx_std_11)
target_link_libraries(tracepoint-provider-utest
    PUBLIC tracepoint)
add_test(NAME tracepoint-provider-utest
    COMMAND tracepoint-provider-utest)
