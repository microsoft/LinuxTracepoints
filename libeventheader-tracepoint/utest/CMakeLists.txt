add_executable(eventheader-dat-utest
    dat-utest.cpp
    ../samples/tracepoint-file.cpp
    TestCommon.cpp
    TestDynamic.cpp
    TestProviderC.c
    TestProviderCpp.cpp)
target_link_libraries(eventheader-dat-utest
    PUBLIC eventheader-tracepoint)
target_compile_features(eventheader-dat-utest
    PRIVATE cxx_std_17 c_std_11)

configure_file(
    "../../TestOutput/EventHeaderInterceptorLE64.dat"
    "EventHeaderInterceptorLE64.dat.expected"
    COPYONLY)

# Only works on 64-bit little-endian.
# When running the test from within Visual Studio, the working directory is wrong.
add_test(NAME eventheader-dat-utest
    COMMAND eventheader-dat-utest ${CMAKE_CURRENT_BINARY_DIR})

add_executable(eventheader-tracepoint-generate
    tracepoint-generate.cpp
    TestCommon.cpp
    TestDynamic.cpp
    TestProviderC.c
    TestProviderCpp.cpp)
target_link_libraries(eventheader-tracepoint-generate
    PUBLIC eventheader-tracepoint tracepoint)
target_compile_features(eventheader-tracepoint-generate
    PRIVATE cxx_std_17 c_std_11)
