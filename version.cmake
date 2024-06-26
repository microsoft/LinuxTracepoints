include_guard()
if(NOT DEFINED LINUXTRACEPOINTS_VERSION)
    set(LINUXTRACEPOINTS_VERSION   1.4.0)
    set(EVENTHEADER_HEADERS_MINVER 1.4) # Referenced by libeventheader-decode-cpp
    set(TRACEPOINT_MINVER          1.4) # Referenced by libeventheader-tracepoint
    set(TRACEPOINT_DECODE_MINVER   1.4) # Referenced by libeventheader-decode-cpp, libtracepoint-control-cpp
    set(TRACEPOINT_HEADERS_MINVER  1.4) # Referenced by libeventheader-tracepoint
endif()
