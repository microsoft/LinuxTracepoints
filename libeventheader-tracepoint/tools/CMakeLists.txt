# Deprecated - prefer tracepoint-register from libtracepoint.
add_executable(eventheader-register
    eventheader-register.cpp)
target_link_libraries(eventheader-register
    PUBLIC eventheader-tracepoint tracepoint)
install(TARGETS eventheader-register)
