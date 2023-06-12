// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*
Helpers for locating the "/sys/.../tracing" directory and loading "format"
files from it.

The TracepointCache class uses these functions to locate and load format
information.
*/

#pragma once
#ifndef _included_TracepointPath_h
#define _included_TracepointPath_h 1

#include <string_view>
#include <vector>

#ifndef _In_z_
#define _In_z_
#endif
#ifndef _Ret_z_
#define _Ret_z_
#endif
#ifndef _Success_
#define _Success_(condition)
#endif

namespace tracepoint_control
{
    /*
    Returns the path to the "/sys/.../tracing" directory, usually either
    "/sys/kernel/tracing" or "/sys/kernel/debug/tracing".

    Returns "" if no tracing directory could be found (e.g. tracefs not mounted).

    Implementation: The first time this is called, it parses "/proc/mounts" to
    find the tracefs or debugfs mount point. Subsequent calls return the cached
    result. This function is thread-safe.
    */
    _Ret_z_ char const*
    GetTracingDirectory() noexcept;

    /*
    Given full path to a file, appends the file's contents to the specified
    dest string.

    Returns 0 for success, errno for error.
    */
    _Success_(return == 0) int
    AppendTracingFile(
        std::vector<char>& dest,
        _In_z_ char const* fileName) noexcept;

    /*
    Given systemName and eventName, appends the corresponding event's format
    data to the specified dest string (i.e. appends the contents of format file
    "$(tracingDirectory)/events/$(systemName)/$(eventName)/format").

    For example, AppendTracingFormatFile("user_events", "MyEventName", format) would
    append the contents of "/sys/.../tracing/events/user_events/MyEventName/format"
    (using the "/sys/.../tracing" directory as returned by GetTracingDirectory()).

    Returns 0 for success, errno for error.
    */
    _Success_(return == 0) int
    AppendTracingFormatFile(
        std::vector<char>& dest,
        std::string_view systemName,
        std::string_view eventName) noexcept;
}
// namespace tracepoint_control

#endif // _included_TracepointPath_h
