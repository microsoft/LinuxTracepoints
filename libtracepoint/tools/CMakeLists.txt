add_executable(tracepoint-register
    tracepoint-register.cpp)
target_link_libraries(tracepoint-register
    PUBLIC tracepoint)
target_compile_features(tracepoint-register
    PRIVATE cxx_std_17)
install(TARGETS tracepoint-register)
