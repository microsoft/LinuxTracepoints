// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#ifndef _included_PerfDataFileWriter_h
#define _included_PerfDataFileWriter_h

#include "PerfDataFileDefs.h"
#include <stdint.h>
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
#ifndef _In_reads_
#define _In_reads_(count)
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

#ifndef _WIN32

// Forward declaration from sys/uio.h:
struct iovec;

#endif // !_WIN32

namespace tracepoint_decode
{
    /*
    PerfDataFileWriter class - Writes perf.data files.

    - Construct a writer: PerfDataFileWriter writer;
    - Open the file: writer.Create(filename);
      - This writes headers and positions the file pointer for event data.
    - Do the following (in any order):
      - Call WriteEventData to write event data to the file.
      - Call AddTracepointEventDesc() to provide event information for events
        with tracefs format information.
      - Call AddEventDesc() to provide event information for events that don't
        have tracefs format information.
      - Call SetHeader() to provide data for other headers in the file.
    - Close the file: writer.Close();
      - This writes the file footers, finalizes the headers, then closes the file.
    */
    class PerfDataFileWriter
    {
        struct perf_file_section;
        struct perf_file_header;
        struct EventDesc;
        struct TracepointInfo;

        uint64_t m_filePos;
        int m_file;
        std::vector<EventDesc> m_eventDescs;
        std::map<uint32_t, TracepointInfo> m_tracepointInfoByCommonType;
        std::vector<char> m_headers[PERF_HEADER_LAST_FEATURE];

        uint32_t m_tracingDataPageSize;
        uint8_t m_tracingDataLongSize;
        std::vector<char> m_tracingDataHeaderPage;
        std::vector<char> m_tracingDataHeaderEvent;
        std::vector<std::vector<char>> m_tracingDataFtraces;
        std::vector<char> m_tracingDataKallsyms;
        std::vector<char> m_tracingDataPrintk;
        std::vector<char> m_tracingDataSavedCmdline;

    public:

        PerfDataFileWriter(PerfDataFileWriter const&) = delete;
        void operator=(PerfDataFileWriter const&) = delete;

        // Calls CloseNoFinalize.
        ~PerfDataFileWriter() noexcept;

        // May throw bad_alloc.
        PerfDataFileWriter() noexcept(false);

        // Immediately closes the current output file (if any).
        // Does not finalize headers - resulting file will not be usable.
        void
        CloseNoFinalize() noexcept;

        // Writes footer, finalizes header, and closes the output file.
        // On error, closes the output file and returns errno.
        _Success_(return == 0) int
        FinalizeAndClose() noexcept;

        // Calls CloseNoFinalize() to close any previous output file, then creates a new
        // file using open(filePath, O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC, mode) and
        // writes the file header. On error, closes the output file and returns errno.
        _Success_(return == 0) int
        Create(_In_z_ char const* filePath, int mode = -1) noexcept;

        // Returns the file offset at which the next call to WriteEventData()
        // will begin writing. Returns -1 if file is closed.
        uint64_t
        FilePos() const noexcept;

        // Adds a block of event data to the output file.
        // Data should be a sequence of perf_event_header blocks, i.e. a
        // perf_event_header, then data, then another perf_event_header, etc.
        //
        // On success, returns 0. On error, returns errno, in which case file state is
        // unspecified (may have written some but not all of the data to the file).
        //
        // Notes:
        // - The content of the data is written directly to the event data section of
        //   the output file without any validation.
        // - dataSize should always be a multiple of 8.
        // - dataSize should almost always be the sum of hdr.size for all headers written,
        //   except for PERF_RECORD_HEADER_TRACING_DATA and PERF_RECORD_AUXTRACE which may
        //   have additional data in the block beyond the size indicated in the header.
        // - The trace file will be invalid if any events are written with an id
        //   field that does not have a corresponding entry in the EventDesc table. You
        //   need to provide that information by calling AddTracepointEventDesc(...) or
        //   AddEventDesc(...).
        _Success_(return == 0) int
        WriteEventData(
            _In_reads_bytes_(dataSize) void const* data,
            size_t dataSize) noexcept;

#ifndef _WIN32

        // Advanced: Adds blocks of event data to the output file.
        // Similar to WriteEventData, but accepts multiple blocks of data and returns
        // the number of bytes written instead of errno.
        //
        // On error, returns -1.
        // On success, returns number of bytes written. In rare cases, may succeed with a
        // result less than dataSize (see writev(2) documentation).
        _Success_(return >= 0) ptrdiff_t
        WriteEventDataIovecs(
            _In_reads_(iovecsCount) struct iovec const* iovecs,
            int iovecsCount) noexcept;

#endif // !_WIN32

        // Returns the number of bytes set for the specified header.
        size_t
        GetHeaderSize(PerfHeaderIndex index) const noexcept;

        // Returns a pointer to the data set for the specified header.
        void const*
        GetHeaderData(PerfHeaderIndex index) const noexcept;

        // Directly sets or resets the data for the specified header.
        //
        // Note that the PerfDataFileWriter class has special support for the
        // following headers:
        //
        // - If no data has been set via SetHeader(PERF_HEADER_TRACING_DATA, ...)
        //   then the PERF_HEADER_TRACING_DATA header will be synthesized using
        //   data supplied via AddTracepointEventDesc(...) and SetTracingData(...). 
        // - If no data has been set via SetHeader(PERF_HEADER_EVENT_DESC, ...)
        //   then the PERF_HEADER_EVENT_DESC header will be synthesized using data
        //   supplied via AddTracepointEventDesc(...) and AddEventDesc(...).
        _Success_(return == 0) int
        SetHeader(
            PerfHeaderIndex index,
            _In_reads_bytes_(dataSize) void const* data,
            size_t dataSize) noexcept;

        // Configures information to be included in the synthesized
        // PERF_HEADER_TRACING_DATA header. These settings are given default values
        // when the PerfDataFileWriter is constructed. These settings are used when
        // writing a file for which no data was provided via
        // SetHeader(PERF_HEADER_TRACING_DATA, ...).
        // 
        // For all of the parameters, a 0 or {NULL, 0} value indicates "keep the
        // existing value". To indicate "set the value to empty", use {non-null, 0}.
        //
        // - longSize: Default is sizeof(size_t).
        // - pageSize: Default is sysconf(_SC_PAGESIZE).
        // - headerPage: Default is timestamp64+commit64+overwrite8+data4080. Empty means use default.
        // - headerEvent: Default is type_len5, time_delta27, array32. Empty means use default.
        // - ftraces: Default is "".
        // - kallsyms: Default is "".
        // - printk: Default is "".
        // - savedCmdLine: Default is "".
        _Success_(return == 0) int
        SetTracingData(
            uint8_t longSize,
            uint32_t pageSize,
            std::string_view headerPage,
            std::string_view headerEvent,
            _In_reads_(ftraceCount) std::string_view const* ftraces,
            uint32_t ftraceCount,
            std::string_view kallsyms,
            std::string_view printk,
            std::string_view savedCmdLine) noexcept;

        // Adds perf_event_attr and name information for the specified event ids.
        // Use this for events that do NOT have tracefs format information, i.e.
        // when desc.metadata == NULL.
        // 
        // Requires: desc.attr != NULL, desc.name != NULL.
        //
        // Returns 0 for success, errno for error.
        // Returns E2BIG if desc.name is 64KB or longer.
        //
        // Note that each id used in the trace should map to exactly one attr provided
        // by AddTracepointEventDesc or AddEventDesc, but this is not validated by
        // PerfDataFileWriter. For example, if the same id is provided in two different
        // calls to AddEventDesc, the resulting file may not decode properly.
        _Success_(return == 0) int
        AddEventDesc(PerfEventDesc const& desc) noexcept;

        // Returns true if there has been a successful call to
        // AddTracepointEventDesc(desc) where desc.metadata->Id() == common_type.
        bool
        HasTracepointEventDesc(uint32_t common_type) const noexcept;

        // Adds metadata for the event specified by desc.metadata->Id().
        // If metadata is already set for that event, returns EEXIST.
        // Use this for events that DO have tracefs format information, i.e.
        // when desc.metadata != NULL.
        //
        // Requires: desc.attr != NULL, desc.name != NULL, desc.metadata != NULL.
        // Also, desc.metadata is copied by reference (shallow copy).
        //
        // Returns 0 for success, errno for error.
        // Returns E2BIG if desc.name is 64KB or longer.
        //
        // Note that each id used in the trace should map to exactly one attr provided
        // by AddTracepointEventDesc or AddEventDesc, but this is not validated by
        // PerfDataFileWriter. For example, if the same id is provided in two different
        // calls to AddEventDesc, the resulting file may not decode properly.
        _Success_(return == 0) int
        AddTracepointEventDesc(PerfEventDesc const& desc) noexcept;

    private:

        bool
        ValidFilePos() const noexcept;

        _Success_(return == 0) int
        WriteData(
            _In_reads_bytes_(dataSize) void const* data,
            size_t dataSize) noexcept;

        void
        SynthesizeTracingData();

        void
        SynthesizeEventDesc();

        // Writes the perf_file_sections for m_headers,
        // then writes the data from m_headers.
        _Success_(return == 0) int
        WriteHeaders(_Out_ uint64_t* pFlags0) noexcept;

        // Writes the attr+idSection for each attr.
        // Then writes the id data.
        _Success_(return == 0) int
        WriteAttrs(_Out_ perf_file_section* pAttrsSection) noexcept;
    };
}
// namespace tracepoint_decode

#endif // _included_PerfDataFileWriter_h
