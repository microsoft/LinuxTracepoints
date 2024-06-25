add_executable(tracepoint-api-sample
    api-sample.c)
target_link_libraries(tracepoint-api-sample
    PUBLIC tracepoint)

add_executable(tracepoint-provider-sample
    provider-sample.c)
target_link_libraries(tracepoint-provider-sample
    PUBLIC tracepoint)
