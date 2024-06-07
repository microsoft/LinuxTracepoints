add_executable(tracepoint-utest
    tracepoint-utest.cpp)
target_compile_features(tracepoint-utest
    PRIVATE cxx_std_11)
target_link_libraries(tracepoint-utest
    PUBLIC tracepoint)

add_executable(tpp-utest
    tpp-utest-c.c
    tpp-utest-cpp.cpp)
target_compile_features(tpp-utest
    PRIVATE cxx_std_11)
target_link_libraries(tpp-utest
    PUBLIC tracepoint)
