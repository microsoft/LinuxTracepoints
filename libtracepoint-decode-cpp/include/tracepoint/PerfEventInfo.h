// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#ifndef _included_PerfEventInfo_h
#define _included_PerfEventInfo_h

#include <stdint.h>

#ifdef _WIN32
#include <sal.h>
#endif
#ifndef _Field_z_
#define _Field_z_
#endif
#ifndef _Field_size_bytes_
#define _Field_size_bytes_(size)
#endif

// Forward declaration from PerfEventAbi.h or linux/uapi/linux/perf_event.h:
struct perf_event_attr;

namespace tracepoint_decode
{
    // Forward declaration from PerfEventMetadata.h:
    class PerfEventMetadata;

    struct PerfSampleEventInfo
    {
        uint64_t id;                            // Always valid if GetSampleEventInfo succeeded.
        perf_event_attr const* attr;            // Always valid if GetSampleEventInfo succeeded.
        _Field_z_ char const* name;             // e.g. "system:tracepoint", or "" if no name available.
        uint64_t sample_type;                   // Bit set if corresponding info present in event.
        uint32_t pid, tid;                      // Valid if sample_type & PERF_SAMPLE_TID.
        uint64_t time;                          // Valid if sample_type & PERF_SAMPLE_TIME.
        uint64_t stream_id;                     // Valid if sample_type & PERF_SAMPLE_STREAM_ID.
        uint32_t cpu, cpu_reserved;             // Valid if sample_type & PERF_SAMPLE_CPU.
        uint64_t ip;                            // Valid if sample_type & PERF_SAMPLE_IP.
        uint64_t addr;                          // Valid if sample_type & PERF_SAMPLE_ADDR.
        uint64_t period;                        // Valid if sample_type & PERF_SAMPLE_PERIOD.
        uint64_t const* read_values;            // Valid if sample_type & PERF_SAMPLE_READ. Points into event.
        uint64_t const* callchain;              // Valid if sample_type & PERF_SAMPLE_CALLCHAIN. Points into event.
        PerfEventMetadata const* raw_meta;      // Valid if sample_type & PERF_SAMPLE_RAW. NULL if event unknown.
        _Field_size_bytes_(raw_data_size) void const* raw_data; // Valid if sample_type & PERF_SAMPLE_RAW. Points into event.
        uintptr_t raw_data_size;                   // Valid if sample_type & PERF_SAMPLE_RAW. Size of raw_data.
    };

    struct PerfNonSampleEventInfo
    {
        uint64_t id;                            // Always valid if GetNonSampleEventInfo succeeded.
        perf_event_attr const* attr;            // Always valid if GetNonSampleEventInfo succeeded.
        _Field_z_ char const* name;             // e.g. "system:tracepoint", or "" if no name available.
        uint64_t sample_type;                   // Bit set if corresponding info present in event.
        uint32_t pid, tid;                      // Valid if sample_type & PERF_SAMPLE_TID.
        uint64_t time;                          // Valid if sample_type & PERF_SAMPLE_TIME.
        uint64_t stream_id;                     // Valid if sample_type & PERF_SAMPLE_STREAM_ID.
        uint32_t cpu, cpu_reserved;             // Valid if sample_type & PERF_SAMPLE_CPU.
    };
}
// namespace tracepoint_decode

#endif // _included_PerfEventInfo_h
