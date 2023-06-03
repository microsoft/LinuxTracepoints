// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#ifndef _included_PerfDataFile_h
#define _included_PerfDataFile_h

#include "PerfByteReader.h"
#include <stdint.h>
#include <stdio.h> // FILE
#include <map>
#include <memory>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <sal.h>
#endif
#ifndef _In_
#define _In_
#endif
#ifndef _In_z_
#define _In_z_
#endif
#ifndef _In_reads_bytes_
#define _In_reads_bytes_(cb)
#endif
#ifndef _Out_
#define _Out_
#endif
#ifndef _Outptr_result_maybenull_
#define _Outptr_result_maybenull_
#endif
#ifndef _Out_writes_bytes_all_
#define _Out_writes_bytes_all_(size)
#endif
#ifndef _Field_z_
#define _Field_z_
#endif
#ifndef _Field_size_bytes_
#define _Field_size_bytes_(size)
#endif
#ifndef _Success_
#define _Success_(condition)
#endif

// Forward declarations from PerfEventAbi.h or linux/uapi/linux/perf_event.h:
struct perf_event_attr;
struct perf_event_header;

namespace tracepoint_decode
{
    // Forward declarations from PerfEventInfo.h:
    struct PerfSampleEventInfo;
    struct PerfNonSampleEventInfo;

    // Forward declaration from PerfEventMetadata.h:
    class PerfEventMetadata;

    // uint8 header index.
    // From: perf.data-file-format.txt, perf/util/header.h.
    enum PerfHeaderIndex : uint8_t {
        PERF_HEADER_RESERVED = 0,       // always cleared
        PERF_HEADER_FIRST_FEATURE = 1,
        PERF_HEADER_TRACING_DATA = 1,
        PERF_HEADER_BUILD_ID,
        PERF_HEADER_HOSTNAME,
        PERF_HEADER_OSRELEASE,
        PERF_HEADER_VERSION,
        PERF_HEADER_ARCH,
        PERF_HEADER_NRCPUS,
        PERF_HEADER_CPUDESC,
        PERF_HEADER_CPUID,
        PERF_HEADER_TOTAL_MEM,
        PERF_HEADER_CMDLINE,
        PERF_HEADER_EVENT_DESC,
        PERF_HEADER_CPU_TOPOLOGY,
        PERF_HEADER_NUMA_TOPOLOGY,
        PERF_HEADER_BRANCH_STACK,
        PERF_HEADER_PMU_MAPPINGS,
        PERF_HEADER_GROUP_DESC,
        PERF_HEADER_AUXTRACE,
        PERF_HEADER_STAT,
        PERF_HEADER_CACHE,
        PERF_HEADER_SAMPLE_TIME,
        PERF_HEADER_MEM_TOPOLOGY,
        PERF_HEADER_CLOCKID,
        PERF_HEADER_DIR_FORMAT,
        PERF_HEADER_BPF_PROG_INFO,
        PERF_HEADER_BPF_BTF,
        PERF_HEADER_COMPRESSED,
        PERF_HEADER_CPU_PMU_CAPS,
        PERF_HEADER_CLOCK_DATA,
        PERF_HEADER_HYBRID_TOPOLOGY,
        PERF_HEADER_PMU_CAPS,
        PERF_HEADER_LAST_FEATURE,
    };

    struct PerfEventDesc
    {
        perf_event_attr const* attr;    // NULL for unknown id.
        _Field_z_ char const* name;     // "" if no name available.
    };

    class PerfDataFile
    {
        struct perf_file_section;
        struct perf_pipe_header;
        struct perf_file_header;

        uint64_t m_filePos;
        uint64_t m_fileLen;
        uint64_t m_dataBeginFilePos;
        uint64_t m_dataEndFilePos;
        FILE* m_file;
        std::vector<uint8_t> m_eventData;
        std::vector<char> m_headers[32]; // Stored file-endian.
        std::vector<std::unique_ptr<perf_event_attr>> m_attrsList; // Stored host-endian.
        std::map<uint64_t, PerfEventDesc> m_eventDescById; // Points into m_attrsList and/or m_headers.
        PerfByteReader m_byteReader;
        int8_t m_sampleIdOffset; // -1 = unset, -2 = no id.
        int8_t m_nonSampleIdOffset; // -1 = unset, -2 = no id.
        int8_t m_commonTypeOffset; // -1 = unset, -2 = not available.
        uint8_t m_commonTypeSize;
        bool m_parsedHeaderEventDesc;

        // HEADER_TRACING_DATA
        bool m_parsedTracingData;
        uint8_t m_tracingDataLongSize;
        uint32_t m_tracingDataPageSize;
        std::string_view m_headerPage; // Points into m_headers.
        std::string_view m_headerEvent; // Points into m_headers.
        std::vector<std::string_view> m_ftraces; // Points into m_headers.
        std::map<uint32_t, PerfEventMetadata> m_metadataById; // Points into m_headers.
        std::string_view m_kallsyms; // Points into m_headers.
        std::string_view m_printk; // Points into m_headers.
        std::string_view m_cmdline; // Points into m_headers.

    public:

        PerfDataFile(PerfDataFile const&) = delete;
        void operator=(PerfDataFile const&) = delete;
        ~PerfDataFile() noexcept;
        PerfDataFile() noexcept;

        // Returns true if the currently-opened file is big-endian.
        bool
        FileBigEndian() const noexcept;

        // Returns PerfByteReader(FileBigEndian()).
        PerfByteReader
        ByteReader() const noexcept;

        // Returns the position within the input file of the event that will be
        // read by the next call to ReadEvent().
        // Returns UINT64_MAX after end-of-file or file error.
        uint64_t
        FilePos() const noexcept;

        // Returns the position within the input file of the first event.
        uint64_t
        DataBeginFilePos() const noexcept;

        // If the input file was recorded in pipe mode, returns UINT64_MAX.
        // Otherwise, returns the position within the input file immediately after
        // the last event.
        uint64_t
        DataEndFilePos() const noexcept;

        // Returns the number of attribute records available from Attr().
        uintptr_t
        AttrCount() const noexcept;

        // Combined data from perf_file_header::attrs and PERF_RECORD_HEADER_ATTR.
        // Requires attrIndex < AttrCount().
        perf_event_attr const&
        Attr(uintptr_t attrIndex) const noexcept;

        // Combined data from perf_file_header::attrs, PERF_RECORD_HEADER_ATTR,
        // and HEADER_EVENT_DESC. Returns {NULL,""} for unknown id.
        PerfEventDesc
        EventDescById(uint64_t id) const noexcept;

        // Returns the raw data from the specified header (file-endian, use ByteReader()
        // to do byte-swapping as appropriate).
        // Returns empty if the requested header was not loaded from the file.
        std::string_view
        Header(PerfHeaderIndex headerIndex) const noexcept;

        // Closes the input file, if any.
        void
        Close() noexcept;

        // Closes the current input file (if any), then opens the specified
        // perf.data file using fopen and reads the file header.
        // If not a pipe-mode file, loads metadata. If a pipe-mode file, metadata
        // will be loaded as the metadata events are encountered by ReadEvent.
        // On successful return, the file will be positioned before the first event.
        _Success_(return == 0) int
        Open(_In_z_ char const* filePath) noexcept;

        // Closes the current input file (if any), then switches stdin to binary
        // mode (Windows-only), then reads the file header from stdin. If stdin is
        // not a pipe-mode file, returns an error. Metadata will be loaded as the
        // metadata events are encountered by ReadEvent.
        // On successful return, the file will be positioned before the first event.
        _Success_(return == 0) int
        OpenStdin() noexcept;

        // Returns the event header (host-endian) followed by the raw data from the
        // file (file-endian, use ByteReader() to do byte-swapping as appropriate).
        //
        // On success, sets *ppEventHeader to the event and returns 0.
        // The returned pointer is valid until the next call to ReadEvent.
        // 
        // On end-of-file, sets *ppEventHeader to NULL and returns 0.
        // 
        // On error, sets *ppEventHeader to NULL and returns errno.
        //
        // For PERF_RECORD_HEADER_TRACING_DATA and PERF_RECORD_AUXTRACE, the extra
        // data will be placed immediately after the event.
        _Success_(return == 0) int
        ReadEvent(_Outptr_result_maybenull_ perf_event_header const** ppEventHeader) noexcept;

        // Tries to get event information from the event's prefix. The prefix is
        // usually present only for sample events. If the event prefix is not
        // present, this function may return an error or it may succeed but return
        // incorrect information. In general, only use this on events where
        // pEventHeader->type == PERF_RECORD_SAMPLE.
        _Success_(return == 0) int
        GetSampleEventInfo(
            _In_ perf_event_header const* pEventHeader,
            _Out_ PerfSampleEventInfo* pInfo) const noexcept;

        // Tries to get event information from the event's suffix. The event suffix
        // is usually present only for non-sample kernel-generated events.
        // If the event suffix is not present, this function may return an error or
        // it may succeed but return incorrect information. In general:
        // - Only use this on events where pEventHeader->type != PERF_RECORD_SAMPLE
        //   and pEventHeader->type < PERF_RECORD_USER_TYPE_START.
        // - Only use this on events that come after the PERF_RECORD_FINISHED_INIT
        //   event.
        _Success_(return == 0) int
        GetNonSampleEventInfo(
            _In_ perf_event_header const* pEventHeader,
            _Out_ PerfNonSampleEventInfo* pInfo) const noexcept;

    private:

        _Success_(return == 0) int
        LoadAttrs(perf_file_section const& attrs, uint64_t cbAttrAndIdSection64) noexcept;

        _Success_(return == 0) int
        LoadHeaders(perf_file_section const& data, uint64_t flags) noexcept;

        void
        ParseTracingData() noexcept;

        void
        ParseHeaderEventDesc() noexcept;

        _Success_(return == 0) int
        GetSampleEventId(_In_ perf_event_header const* pEventHeader, _Out_ uint64_t* pId) const noexcept;

        _Success_(return == 0) int
        GetNonSampleEventId(_In_ perf_event_header const* pEventHeader, _Out_ uint64_t* pId) const noexcept;

        _Success_(return == 0) int
        AddAttr(
            std::unique_ptr<perf_event_attr> pAttrPtr,
            uint32_t cbAttrCopied,
            _In_z_ char const* pName,
            _In_reads_bytes_(cbIdsFileEndian) void const* pbIdsFileEndian,
            uintptr_t cbIdsFileEndian) noexcept(false);

        template<class SizeType>
        _Success_(return == 0) int
        ReadPostEventData(uint16_t eventSizeFromHeader) noexcept;

        bool
        EnsureEventDataSize(uint32_t minSize) noexcept;

        // Note: leaves filePos at EOF.
        _Success_(return == 0) int
        UpdateFileLen() noexcept;

        bool
        SectionValid(perf_file_section const& section) const noexcept;

        // Returns 0 (success), EIO (fread error), or EPIPE (eof).
        _Success_(return == 0) int
        FileRead(_Out_writes_bytes_all_(cb) void* p, uintptr_t cb) noexcept;

        _Success_(return == 0) int
        FileSeek(uint64_t filePos) noexcept;

        // Returns 0 (success), EIO (fread error), EPIPE (eof), or others.
        _Success_(return == 0) int
        FileSeekAndRead(uint64_t filePos, _Out_writes_bytes_all_(cb) void* p, uintptr_t cb) noexcept;
    };
}
// namespace tracepoint_decode

#endif // _included_PerfDataFile_h
