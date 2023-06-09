// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#ifndef _included_TracingSession_h
#define _included_TracingSession_h

#include <tracepoint/PerfEventMetadata.h>
#include <tracepoint/PerfEventInfo.h>
#include <tracepoint/TracingCache.h>

#include <unordered_map>
#include <string_view>
#include <memory>
#include <vector>

#include <signal.h> // sigset_t

#if _WIN32
#include <sal.h>
#else // _WIN32
#ifndef _In_reads_
#define _In_reads_(size)
#endif
#ifndef _In_reads_opt_
#define _In_reads_opt_(size)
#endif
#ifndef _Out_
#define _Out_
#endif
#ifndef _Out_writes_
#define _Out_writes_(size)
#endif
#endif // _WIN32

// Forward declarations:
struct pollfd; // poll.h
struct timespec; // time.h

namespace tracepoint_control
{
    enum class TracingMode : unsigned char
    {
        /*
        Buffers will be managed as circular:

        - If buffer is full, new events will overwrite old events.
        - Events will be read from the buffer newest-to-oldest.
        - Procedure for reading data: pause buffer, read events, unpause.
          Events arriving while buffer is paused will be lost.
        */
        Circular,

        /*
        Buffers will be managed as realtime:

        - If buffer is full, new events will be lost.
        - Events will be read from the buffer oldest-to-newest.
        - Procedure for reading data: read events, mark the events as consumed.
        - Can use WaitForWakeup() or poll() to wait for data to become available
          (wakeup condition).
        */
        RealTime,
    };

    class TracingSessionOptions
    {
        static constexpr auto SampleTypeDefault = 0x486u;
        static constexpr auto SampleTypeSupported = 0x107EFu;

    public:

        /*
        Initializes a TracingSessionOptions with the specified mode and buffer size.

        - mode: controls whether the buffer is managed as circular or realtime.
        - bufferSize: controls the size of each buffer. This value will be rounded up
          to a power of 2. Note that the session will allocate one buffer per CPU.
        */
        constexpr
        TracingSessionOptions(
            TracingMode mode,
            size_t bufferSize) noexcept
            : m_bufferSize(bufferSize)
            , m_mode(mode)
            , m_wakeupUseWatermark(false)
            , m_wakeupValue(0)
            , m_sampleType(SampleTypeDefault)
        {
            return;
        }

        /*
        Flags indicating what information should be recorded for each tracepoint.

        Flags use the perf_event_sample_format values defined in perf_event.h or
        PerfEventAbi.h. The following flags are supported:

        | PERF_SAMPLE_IDENTIFIER
        | PERF_SAMPLE_IP
        | PERF_SAMPLE_TID
        | PERF_SAMPLE_TIME
        | PERF_SAMPLE_ADDR
        | PERF_SAMPLE_ID
        | PERF_SAMPLE_STREAM_ID
        | PERF_SAMPLE_CPU
        | PERF_SAMPLE_PERIOD
        | PERF_SAMPLE_CALLCHAIN
        | PERF_SAMPLE_RAW

        Default value is:

        | PERF_SAMPLE_TID
        | PERF_SAMPLE_TIME
        | PERF_SAMPLE_CPU
        | PERF_SAMPLE_RAW
        */
        constexpr TracingSessionOptions&
        SampleType(uint32_t sampleType) noexcept
        {
            m_sampleType = sampleType & SampleTypeSupported;
            return *this;
        }

        /*
        For realtime sessions only: sets the number of unconsumed
        PERF_RECORD_SAMPLE events that a realtime buffer must contain to
        trigger wakeup.

        The default value is WakeupEvents(0).

        Note that setting this will disable WakeupWatermark().
        */
        constexpr TracingSessionOptions&
        WakeupEvents(uint32_t wakeupEvents) noexcept
        {
            m_wakeupUseWatermark = false;
            m_wakeupValue = wakeupEvents;
            return *this;
        }

        /*
        For realtime sessions only: sets the number of bytes of unconsumed
        event data (both SAMPLE and non-SAMPLE events) that a realtime
        buffer must contain to trigger wakeup.

        The default value is WakeupEvents(0), i.e. wakeup will use event count,
        not watermark.

        Note that setting this will disable WakeupEvents().
        */
        constexpr TracingSessionOptions&
        WakeupWatermark(uint32_t wakeupWatermark) noexcept
        {
            m_wakeupUseWatermark = true;
            m_wakeupValue = wakeupWatermark;
            return *this;
        }

    private:

        friend class TracingSession;

    private:

        size_t const m_bufferSize;
        TracingMode const m_mode;
        bool m_wakeupUseWatermark;
        uint32_t m_wakeupValue;
        uint32_t m_sampleType;
    };

    class TracingSession
    {
    public:

        TracingSession(TracingSession const&) = delete;
        void operator=(TracingSession const&) = delete;
        ~TracingSession();

        /*
        May throw std::bad_alloc.
        */
        TracingSession(
            TracingCache& cache,
            TracingSessionOptions const& options) noexcept(false);

        /*
        Returns the mode that was specified at construction.
        */
        TracingMode
        Mode() const noexcept;

        /*
        Returns true if Mode() == Realtime, false if Mode() == Circular.
        */
        bool
        IsRealtime() const noexcept;

        /*
        Returns the size of the buffers used for the session.
        */
        size_t
        BufferSize() const noexcept;

        /*
        Returns the number of buffers used for the session.
        Usually this is the number of CPUs.
        */
        uint32_t
        BufferCount() const noexcept;

        /*
        Returns the number of SAMPLE events that have been processed
        by this session.
        */
        uint64_t
        SampleEventCount() const noexcept;

        /*
        Returns the number of lost events that have been processed
        by this session. Events can be lost due to:

        - Memory allocation failure during buffer enumeration.
        - Event received while session is paused (circular mode only).
        - Event received while buffer is full (realtime mode only).
        */
        uint64_t
        LostEventCount() const noexcept;

        /*
        Returns the number of corrupt events that have been processed
        by this session. An event is detected as corrupt if the event
        header's size is too small for the event's SampleType.
        */
        uint64_t
        CorruptEventCount() const noexcept;

        /*
        Returns the number of times buffer corruption has been detected
        by this session. The buffer is detected as corrupt if the buffer
        header has invalid values or if an event header has an invalid
        size. Buffer corruption generally causes the buffer's remaining
        contents to be ignored.
        */
        uint64_t
        CorruptBufferCount() const noexcept;

        /*
        Clears the list of tracepoints we are listening to.
        Frees all buffers.
        */
        void
        Clear() noexcept;

        /*
        Disables collection of the specified tracepoint.

        tracepointPath is in the format "systemName:eventName" or
        "systemName/eventName", e.g. "user_events:MyEvent" or
         "ftrace/function".

        - systemName is the name of a subdirectory of
          "/sys/kernel/tracing/events" such as "user_events" or "ftrace".
        - eventName is the name of a subdirectory of
          "/sys/kernel/tracing/events/systemName", e.g. "MyEvent" or
          "function".

        Returns 0 for success, errno for error.
        Errors include but are not limited to:
        - ENOENT: tracefs metadata not found (tracepoint may not be registered yet).
        - ENOTSUP: unable to find tracefs mount point.
        - EPERM: access denied to tracefs metadata.
        - ENODATA: unable to parse tracefs metadata.
        - ENOMEM: memory allocation failed.
        */
        _Success_(return == 0) int
        DisableTracePoint(
            std::string_view tracepointPath) noexcept;

        /*
        Disables collection of the specified tracepoint.

        - systemName is the name of a subdirectory of
          "/sys/kernel/tracing/events" such as "user_events" or "ftrace".
        - eventName is the name of a subdirectory of
          "/sys/kernel/tracing/events/systemName", e.g. "MyEvent" or
          "function".

        Returns 0 for success, errno for error.
        Errors include but are not limited to:
        - ENOENT: tracefs metadata not found (tracepoint may not be registered yet).
        - ENOTSUP: unable to find tracefs mount point.
        - EPERM: access denied to tracefs metadata.
        - ENODATA: unable to parse tracefs metadata.
        - ENOMEM: memory allocation failed.
        */
        _Success_(return == 0) int
        DisableTracePoint(
            std::string_view systemName,
            std::string_view eventName) noexcept;

        /*
        Enables collection of the specified tracepoint.

        tracepointPath is in the format "systemName:eventName" or
        "systemName/eventName", e.g. "user_events:MyEvent" or
         "ftrace/function".

        - systemName is the name of a subdirectory of
          "/sys/kernel/tracing/events" such as "user_events" or "ftrace".
        - eventName is the name of a subdirectory of
          "/sys/kernel/tracing/events/systemName", e.g. "MyEvent" or
          "function".

        Returns 0 for success, errno for error.
        Errors include but are not limited to:
        - ENOENT: tracefs metadata not found (tracepoint may not be registered yet).
        - ENOTSUP: unable to find tracefs mount point.
        - EPERM: access denied to tracefs metadata.
        - ENODATA: unable to parse tracefs metadata.
        - ENOMEM: memory allocation failed.
        */
        _Success_(return == 0) int
        EnableTracePoint(
            std::string_view tracepointPath) noexcept;

        /*
        Enables collection of the specified tracepoint.

        - systemName is the name of a subdirectory of
          "/sys/kernel/tracing/events" such as "user_events" or "ftrace".
        - eventName is the name of a subdirectory of
          "/sys/kernel/tracing/events/systemName", e.g. "MyEvent" or
          "function".

        Returns 0 for success, errno for error.
        Errors include but are not limited to:
        - ENOENT: tracefs metadata not found (tracepoint may not be registered yet).
        - ENOTSUP: unable to find tracefs mount point.
        - EPERM: access denied to tracefs metadata.
        - ENODATA: unable to parse tracefs metadata.
        - ENOMEM: memory allocation failed.
        */
        _Success_(return == 0) int
        EnableTracePoint(
            std::string_view systemName,
            std::string_view eventName) noexcept;

        /*
        For realtime sessions only: Waits for the wakeup condition using
        ppoll(bufferFiles, bufferCount, timeout, sigmask). The wakeup condition
        is configured by calling WakeupEvents or WakeupWatermark on a config
        before passing the config to the session's constructor.

        - timeout: Maximum time to wait. NULL means wait forever.
        - sigmask: Signal mask to apply before waiting. NULL means don't mask.
        - activeCount: On success, receives the number of buffers that meet the
          wakeup condition, or 0 if wait ended due to timeout or signal.

        Returns EPERM if the session is not realtime.

        Returns EPERM if the session is inactive. After construction and after
        Clear(), the session will be inactive until a tracepoint is added.
        */
        _Success_(return == 0) int
        WaitForWakeup(
            timespec const* timeout,
            sigset_t const* sigmask,
            _Out_ int* pActiveCount) noexcept;

        /*
        Advanced scenarios: Returns the file descriptors used for the buffers
        of the session. The returned file descriptors may be used for poll()
        but should not be read-from, written-to, closed, etc.

        Returns EPERM if the session is inactive. After construction and after
        Clear(), the session will be inactive until a tracepoint is added.

        Most users should use WaitForWakeup() instead of GetBufferFiles().
        */
        _Success_(return == 0) int
        GetBufferFiles(
            _Out_writes_(BufferCount()) int* pBufferFiles) const noexcept;

        /*
        For each PERF_RECORD_SAMPLE event in the session's buffers, invoke:
        
            error = sampleFn(cpu, event, args...);

        - int sampleFn(uint32_t cpu, PerfSampleEventInfo const& event, ...):
          If this returns a nonzero value then FlushEventsUnordered will
          immediately stop and return the specified error value.

        - args...: optional additional parameters to be passed to sampleFn.

        For efficiency, events will be provided in a natural enumeration order.
        This is usually not the same as event timestamp order, so you may need to
        sort the events after receiving them.

        Note that the PerfSampleEventInfo& provided to the sampleFn callback will
        contain pointers into the active trace buffers. The pointers will become
        invalidated after the callback returns. Any data that you need to use after
        that point must be copied.

        Note that this method does not throw any of its own exceptions, but it may
        exit via exception if sampleFn() throws an exception.

        *** Circular session behavior ***
        
        For each CPU:

        - Pause the CPU's buffer.
        - Enumerate the buffer's events newest-to-oldest.
        - Unpause the CPU's buffer.

        Note that events are lost if they arrive while the buffer is paused. The lost
        event count indicates how many events were lost during previous pauses that would
        have been part of this flush if there had been no pauses. It does not include the
        count of events that were lost due to the current flush's pause (those will show
        up in a subsequent flush).

        *** Realtime session behavior ***
        
        For each CPU:

        - Enumerate the buffer's events oldest-to-newest.
        - Mark the CPU's buffer as consumed.

        Note that events are lost if they arrive while the buffer is full. The lost
        event count indicates how many events were lost during previous periods when
        the buffer was full that would have been part of this flush if the buffer had
        never become full. It does not include the count of events that were lost due
        to the buffer being full at the start of the current flush (those will show up
        in a subsequent flush).

        Note that if sampleFn throws or returns a nonzero value, events will be marked
        consumed up to but not including the event for which sampleFn returned an error.

        Note that it is possible for events to arrive while the flush is occuring.
        Flush may ignore events that arrive after flush began. If you need to be
        sure that all events are flushed, repeat the call to FlushEventsUnordered until
        no more events are flushed.
        */
        template<class SampleFnTy, class... ArgTys>
        _Success_(return == 0) int
        FlushSampleEventsUnordered(
            SampleFnTy&& sampleFn,
            ArgTys&&... args) noexcept(noexcept(sampleFn(
                static_cast<uint32_t>(0),
                std::declval<tracepoint_decode::PerfSampleEventInfo const&>(),
                args...
                )))
        {
            int error = 0;

            if (m_leaderCpuFiles != nullptr)
            {
                for (unsigned cpuIndex = 0; cpuIndex != m_cpuCount; cpuIndex += 1)
                {
                    BufferEnumerator enumerator(*this, cpuIndex);
                    while (enumerator.MoveNext())
                    {
                        error = sampleFn(cpuIndex, enumerator.Current(), args...);
                        if (error != 0)
                        {
                            break;
                        }
                    }

                    if (error != 0)
                    {
                        break;
                    }
                }
            }

            return error;
        }

    private:

        class unique_fd
        {
            int m_fd;
        public:
            ~unique_fd();
            unique_fd() noexcept;
            explicit unique_fd(int fd) noexcept;
            unique_fd(unique_fd&&) noexcept;
            unique_fd& operator=(unique_fd&&) noexcept;
            explicit operator bool() const noexcept;
            void reset() noexcept;
            void reset(int fd) noexcept;
            int get() const noexcept;
        };

        class unique_mmap
        {
            void* m_addr;
            size_t m_size;
        public:
            ~unique_mmap();
            unique_mmap() noexcept;
            unique_mmap(void* addr, size_t size) noexcept;
            unique_mmap(unique_mmap&&) noexcept;
            unique_mmap& operator=(unique_mmap&&) noexcept;
            explicit operator bool() const noexcept;
            void reset() noexcept;
            void reset(void* addr, size_t size) noexcept;
            void* get() const noexcept;
            size_t get_size() const noexcept;
        };

        enum class TracepointEnableState : unsigned char
        {
            /*
            An error occurred while trying to enable/disable the tracepoint.
            Actual status is unknown.
            */
            Unknown,

            /*
            Tracepoint is enabled.
            */
            Enabled,

            /*
            Tracepoint is disabled.
            */
            Disabled,
        };

        struct PerfEventHeader
        {
            uint32_t Type;
            uint16_t Misc;
            uint16_t Size;
            PerfEventHeader(uint32_t type, uint16_t misc, uint16_t size) noexcept;
        };

        struct TracepointInfo
        {
            std::unique_ptr<unique_fd[]> CpuFiles; // size is m_cpuCount
            tracepoint_decode::PerfEventMetadata const& Metadata;
            TracepointEnableState EnableState;

            ~TracepointInfo();
            TracepointInfo(
                std::unique_ptr<unique_fd[]> cpuFiles,
                tracepoint_decode::PerfEventMetadata const& metadata) noexcept;
        };

        class BufferEnumerator
        {
            TracingSession& m_session;
            uint32_t const m_cpuIndex;
            tracepoint_decode::PerfSampleEventInfo m_current;

            // These should be treated as const after the constructor finishes:
            uint64_t m_bufferDataHead64;// data_head
            size_t m_bufferDataTail;    // data_tail
            uint8_t const* m_bufferData;// buffer + data_offset
            size_t m_bufferDataPosMask; // data_size - 1

            size_t m_bufferDataPos;     // data_tail..data_head
            char m_nameBuffer[512];

        public:

            BufferEnumerator(BufferEnumerator const&) = delete;
            void operator=(BufferEnumerator const&) = delete;
            ~BufferEnumerator();

            BufferEnumerator(
                TracingSession& session,
                unsigned cpuIndex) noexcept;

            bool
            MoveNext() noexcept;

            tracepoint_decode::PerfSampleEventInfo const&
            Current() const noexcept;

        private:

            bool
            ParseSample(
                PerfEventHeader eventHeader,
                size_t eventHeaderBufferPos) noexcept;
        };

        _Success_(return == 0) static int
        IoctlForEachFile(
            _In_reads_(filesCount) unique_fd const* files,
            unsigned filesCount,
            unsigned long request,
            _In_reads_opt_(filesCount) unique_fd const* values) noexcept;

        size_t
        MmapSize() const noexcept;

    private:

        TracingCache& m_cache;
        TracingMode const m_mode;
        bool const m_wakeupUseWatermark;
        uint32_t const m_wakeupValue;
        uint32_t const m_sampleType;
        uint32_t const m_cpuCount;
        uint32_t const m_pageSize;
        size_t const m_bufferSize;
        std::unique_ptr<unique_mmap[]> const m_cpuMmaps; // size is m_cpuCount
        std::unordered_map<unsigned, TracepointInfo> m_tracepointInfoById;
        std::vector<uint8_t> m_eventDataBuffer; // Double-buffer wrapped events.
        std::unique_ptr<pollfd[]> m_pollfd;
        unique_fd const* m_leaderCpuFiles; // == m_tracepointInfoById[N].cpuFiles.get() for some N, size is m_cpuCount
        uint64_t m_sampleEventCount;
        uint64_t m_lostEventCount;
        uint64_t m_corruptEventCount;
        uint64_t m_corruptBufferCount;
    };
}
// namespace tracepoint_control

#endif // _included_TracingSession_h
