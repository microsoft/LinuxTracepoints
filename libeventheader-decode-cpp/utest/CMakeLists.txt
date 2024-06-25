add_executable(eventheader-decode-dat-utest
    decode-dat-utest.cpp)
target_link_libraries(eventheader-decode-dat-utest
    eventheader-decode)
target_compile_features(eventheader-decode-dat-utest
	PRIVATE cxx_std_17)

# Only works on 64-bit little-endian.
# When running the test from within Visual Studio, the working directory is wrong.
add_test(NAME decode-dat-utest
    COMMAND eventheader-decode-dat-utest ${CMAKE_CURRENT_BINARY_DIR})

configure_file(
    "../../TestOutput/EventHeaderInterceptorLE64.dat"
    "EventHeaderInterceptorLE64.dat"
    COPYONLY)
configure_file(
    "../../TestOutput/EventHeaderInterceptorLE64.dat.linux.json"
    "EventHeaderInterceptorLE64.dat.linux.json.expected"
    COPYONLY)
configure_file(
    "../../TestOutput/EventHeaderInterceptorLE64.dat.windows.json"
    "EventHeaderInterceptorLE64.dat.windows.json.expected"
    COPYONLY)

add_executable(eventheader-decode-perf-utest
    decode-perf-utest.cpp)
target_link_libraries(eventheader-decode-perf-utest
    eventheader-decode
    tracepoint-decode)
target_compile_features(eventheader-decode-perf-utest
	PRIVATE cxx_std_17)

add_test(NAME decode-perf-utest-perf
    COMMAND eventheader-decode-perf-utest "${CMAKE_CURRENT_BINARY_DIR}/perf.data")

add_test(NAME decode-perf-utest-pipe
    COMMAND eventheader-decode-perf-utest "${CMAKE_CURRENT_BINARY_DIR}/pipe.data")

configure_file(
    "../../TestOutput/perf.data"
    "perf.data"
    COPYONLY)

configure_file(
    "../../TestOutput/perf.data.linux.json"
    "perf.data.linux.json.expected"
    COPYONLY)

configure_file(
    "../../TestOutput/perf.data.windows.json"
    "perf.data.windows.json.expected"
    COPYONLY)

configure_file(
    "../../TestOutput/pipe.data"
    "pipe.data"
    COPYONLY)

configure_file(
    "../../TestOutput/pipe.data.linux.json"
    "pipe.data.linux.json.expected"
    COPYONLY)

configure_file(
    "../../TestOutput/pipe.data.windows.json"
    "pipe.data.windows.json.expected"
    COPYONLY)
