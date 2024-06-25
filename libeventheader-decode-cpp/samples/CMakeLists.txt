add_executable(eventheader-decode-file-sample
    decode-file-sample.cpp)
target_link_libraries(eventheader-decode-file-sample
    eventheader-decode)

if(NOT WIN32)

    find_library(TRACEFS_LIBRARIES tracefs)
    find_library(TRACEEVENT_LIBRARIES traceevent)

    if(TRACEFS_LIBRARIES AND TRACEEVENT_LIBRARIES)

        add_executable(eventheader-decode-live-sample
            decode-live-sample.cpp)
        target_link_libraries(eventheader-decode-live-sample
            eventheader-decode
            "${TRACEFS_LIBRARIES}" "${TRACEEVENT_LIBRARIES}")

    endif()
endif()
