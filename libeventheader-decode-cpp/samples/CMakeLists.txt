add_executable(eventheader-decode-file-sample
    decode-file-sample.cpp)
target_link_libraries(eventheader-decode-file-sample
    eventheader-decode)

if(NOT WIN32)

    find_package(PkgConfig)

    if (PKG_CONFIG_FOUND)

        pkg_check_modules(TRACEEVENT REQUIRED libtraceevent)
        pkg_check_modules(TRACEFS REQUIRED libtracefs)

    else()

        # If pkgconfig isn't available, try to find things manually (Why?)
        #
        # On Ubuntu, at least, the required headers are already in the default search path.
        # Everybody should already have pkgconfig anyway though, so we'll just continue in this case
        # assuming the headers can be found by the compiler.
        find_library(TRACEFS_LIBRARIES tracefs)
        find_library(TRACEEVENT_LIBRARIES traceevent)

    endif()

    if (TRACEFS_LIBRARIES AND TRACEEVENT_LIBRARIES)

        add_executable(eventheader-decode-live-sample
            decode-live-sample.cpp)

        if (TRACEFS_INCLUDE_DIRS AND TRACEEVENT_INCLUDE_DIRS)

            target_include_directories(eventheader-decode-live-sample
                PUBLIC "${TRACEFS_INCLUDE_DIRS}" "${TRACEEVENT_INCLUDE_DIRS}")

        endif()

        target_link_libraries(eventheader-decode-live-sample
            eventheader-decode
            "${TRACEFS_LIBRARIES}" "${TRACEEVENT_LIBRARIES}")

    endif()
endif()
