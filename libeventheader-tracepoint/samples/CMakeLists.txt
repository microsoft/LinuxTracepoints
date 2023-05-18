set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(dynamic-sample
    dynamic-sample.cpp)
target_link_libraries(dynamic-sample
    PUBLIC eventheader-tracepoint tracepoint)

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
