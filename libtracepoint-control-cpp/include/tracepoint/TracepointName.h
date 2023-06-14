// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*
TracepointName is a SystemName and EventName.
*/

#pragma once
#ifndef _included_TracepointName_h
#define _included_TracepointName_h 1

#include <string_view>

namespace tracepoint_control
{
    /*
    A TracepointName is a string identifier for a tracepoint on a system.
    It contains two parts: SystemName and EventName.

    Construct a TracepointName by one of the following:
    - TracepointName("SystemName", "EventName")
    - TracepointName("SystemName:EventName")
    - TracepointName("SystemName/EventName")
    - TracepointName("EventName") // Uses SystemName = "user_events"
    */
    struct TracepointName
    {
        /*
        SystemName is the name of a subdirectory of
        "/sys/kernel/tracing/events" such as "user_events" or "ftrace".
        */
        std::string_view SystemName;

        /*
        EventName is the name of a subdirectory of
        "/sys/kernel/tracing/events/SystemName" such as "MyEvent" or "function".
        */
        std::string_view EventName;

        /*
        Create a TracepointName from systemName and eventName, e.g.
        TracepointName("user_events", "MyEvent_L1K1").

        - systemName is the name of a subdirectory of
          "/sys/kernel/tracing/events" such as "user_events" or "ftrace".
        - eventName is the name of a subdirectory of
          "/sys/kernel/tracing/events/systemName", e.g. "MyEvent" or
          "function".
        */
        constexpr
        TracepointName(std::string_view systemName, std::string_view eventName) noexcept
            : SystemName(systemName)
            , EventName(eventName)
        {
            return;
        }

        /*
        Create a TracepointName from a combined "systemName:eventName" or
        "systemName/eventName" string. If the string does not contain ':' or '/',
        the SystemName is assumed to be "user_events".
        */
        explicit constexpr
        TracepointName(std::string_view systemAndEventName) noexcept
            : SystemName()
            , EventName()
        {
            auto const splitPos = systemAndEventName.find_first_of(":/", 0, 2);
            if (splitPos == systemAndEventName.npos)
            {
                SystemName = std::string_view("user_events", 11);
                EventName = systemAndEventName;
            }
            else
            {
                SystemName = systemAndEventName.substr(0, splitPos);
                EventName = systemAndEventName.substr(splitPos + 1);
            }
        }

        /*
        Require SystemName and EventName to always be specified.
        */
        TracepointName() = delete;
    };
}
// namespace tracepoint_control

#endif // _included_TracepointName_h
