// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*
TracingCache: class that loads, parses, and caches the metadata (format)
information for tracepoints.

The TracingSession class uses TracingCache to manage format information for
its tracepoints.
*/

#pragma once
#ifndef _included_TracingCache_h
#define _included_TracingCache_h 1

#include <tracepoint/PerfEventMetadata.h>
#include <unordered_map>
#include <string_view>
#include <vector>

#if _WIN32
#include <sal.h>
#else // _WIN32
#ifndef _Success_
#define _Success_(condition)
#endif
#ifndef _Out_
#define _Out_
#endif
#endif // _WIN32

namespace tracepoint_control
{
    /*
    Loads, parses, and caches the metadata (format) information for tracepoints.
    */
    class TracingCache
    {
    public:

        TracingCache(TracingCache const&) = delete;
        void operator=(TracingCache const&) = delete;
        ~TracingCache();

        /*
        May throw std::bad_alloc.
        */
        TracingCache() noexcept(false);

        /*
        If no events are present in cache, returns -1.
        Otherwise, returns the offset of the common_type field (usually 0).
        */
        int8_t
        CommonTypeOffset() const noexcept;

        /*
        If no events are present in cache, returns 0.
        Otherwise, returns the size of the common_type field (1, 2, or 4, usually 2).
        */
        uint8_t
        CommonTypeSize() const noexcept;

        /*
        If metadata for an event with the specified ID is cached, return it.
        Otherwise, return NULL.
        */
        tracepoint_decode::PerfEventMetadata const*
        FindById(uint32_t id) const noexcept;

        /*
        If metadata for an event with the specified system and name is cached,
        return it. Otherwise, return NULL.
        */
        tracepoint_decode::PerfEventMetadata const*
        FindByName(std::string_view systemName, std::string_view eventName) const noexcept;

        /*
        If metadata for an event with the specified data is cached,
        return it. Otherwise, return NULL.

        Implementation:

        - Assume that rawData is host-endian.
        - Use CommonTypeOffset() and CommonTypeSize() to extract the common_type
          field value.
        - Use FindById() to find the matching metadata.
        */
        tracepoint_decode::PerfEventMetadata const*
        FindByRawData(std::string_view rawData) const noexcept;

        /*
        Parse the formatFileContents to get the metadata. If metadata for an
        event with the same name or ID is already cached, return EEXIST.
        Otherwise, add the metadata to the cache.
        */
        _Success_(return == 0) int
        AddFromFormat(
            std::string_view systemName,
            std::string_view formatFileContents,
            bool longSize64 = sizeof(long) == 8) noexcept;

        /*
        Load and parse the "/sys/.../tracing/events/systemName/eventName/format" file.
        If metadata for an event with the same name or ID is cached, return EEXIST.
        Otherwise, add the metadata to the cache.
        */
        _Success_(return == 0) int
        AddFromSystem(
            std::string_view systemName,
            std::string_view eventName) noexcept;

        /*
        If metadata for an event with the specified name is cached, return it.
        Otherwise, return AddFromSystem(systemName, eventName).
        */
        _Success_(return == 0) int
        FindOrAddFromSystem(
            std::string_view systemName,
            std::string_view eventName,
            _Out_ tracepoint_decode::PerfEventMetadata const** ppMetadata) noexcept;

    private:

        /*
        systemAndFormat = "SystemName\nFormatFileContents".
        */
        _Success_(return == 0) int
        Add(std::vector<char>&& systemAndFormat,
            size_t systemNameSize,
            bool longSize64) noexcept;

    private:

        struct CacheVal
        {
            std::vector<char> SystemAndFormat; // = "SystemName\nFormatFileContents"
            tracepoint_decode::PerfEventMetadata Metadata; // Points into SystemAndFormat

            CacheVal(CacheVal const&) = delete;
            void operator=(CacheVal const&) = delete;
            ~CacheVal();

            CacheVal(
                std::vector<char>&& systemAndFormat,
                tracepoint_decode::PerfEventMetadata&& metadata) noexcept;
        };

        struct EventName
        {
            std::string_view System; // Points into SystemAndFormat
            std::string_view Event; // Points into SystemAndFormat
            EventName(std::string_view system, std::string_view event) noexcept;
        };

        struct EventNameHashOps
        {
            size_t operator()(EventName const&) const noexcept; // Hash
            size_t operator()(EventName const&, EventName const&) const noexcept; // Equal
        };

        std::unordered_map<uint32_t, CacheVal> m_byId;
        std::unordered_map<EventName, CacheVal const&, EventNameHashOps, EventNameHashOps> m_byName;
        int8_t m_commonTypeOffset; // -1 = unset
        uint8_t m_commonTypeSize; // 0 = unset
    };
}
// namespace tracepoint_control

#endif // _included_TracingCache_h
