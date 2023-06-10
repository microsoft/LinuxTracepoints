// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*
TracingSession class that manages a tracepoint collection session.
*/

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
#ifndef _Out_opt_
#define _Out_opt_
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
    /*
    Mode to use for a tracepoint collection session:

    - Circular: Used for "flight recorder" scenarios. Events are collected
      into fixed-size buffers (one buffer per CPU). When buffer is full, new
      events overwrite old events. At any point, you can pause collection,
      enumerate the contents of the buffer, and resume collection. (Events
      received while collection is paused will be lost.)

      For example, we can record information about what is happening on the
      system into memory, and then if a program crashes, we save the data to
      disk so we can discover what was happening on the system in the moments
      leading up to the crash.

    - RealTime: Used for logging scenarios. Events are collected into
      fixed-size buffers (one buffer per CPU). When buffer is full, events will
      be lost. At any point, we can enumerate events from the buffer, consuming
      them to make room for new events (no pause required).
    */
    enum class TracingMode : unsigned char
    {
        /*
        Buffers will be managed as circular:

        - If buffer is full, new events will overwrite old events.
        - Natural event enumeration order is newest-to-oldest (per buffer).
        - Procedure for reading data: pause buffer, enumerate events, unpause.
          (Events arriving while buffer is paused will be lost.)
        */
        Circular,

        /*
        Buffers will be managed as realtime:

        - If buffer is full, new events will be lost.
        - Natural event enumeration order is oldest-to-newest (per buffer).
        - Procedure for reading data: enumerate events, marking the events as
          consumed to make room for new events.
        - Can use WaitForWakeup() or poll() to wait for data to become available.
        */
        RealTime,
    };

    /*
    Configuration settings for a tracepoint collection session.

    Required settings are specified as constructor parameters.
    Optional settings are set by calling methods.

    Example:

        TracingCache cache;
        TracingSession session(
            cache,
            TracingSessionOptions(TracingMode::RealTime, 65536) // Required
                .WakeupWatermark(32768)                         // Optional
                );
    */
    class TracingSessionOptions
    {
        static constexpr auto SampleTypeDefault = 0x486u;
        static constexpr auto SampleTypeSupported = 0x107EFu;

    public:

        /*
        Initializes a TracingSessionOptions to configure a session with the specified
        mode and buffer size.

        - mode: controls whether the buffer is managed as Circular or RealTime.

        - bufferSize: specifies the size of each buffer in bytes. This value will be
          rounded up to a power of 2 that is equal to or greater than the page size.
          Note that the session will allocate one buffer for each CPU.
        */
        constexpr
        TracingSessionOptions(
            TracingMode mode,
            uint32_t bufferSize) noexcept
            : m_bufferSize(bufferSize)
            , m_mode(mode)
            , m_wakeupUseWatermark(true)
            , m_wakeupValue(0)
            , m_sampleType(SampleTypeDefault)
        {
            return;
        }

        /*
        Flags indicating what information should be recorded for each tracepoint.

        Flags use the perf_event_sample_format values defined in <linux/perf_event.h>
        or <tracepoint/PerfEventAbi.h>.

        The default value is:

        | PERF_SAMPLE_TID
        | PERF_SAMPLE_TIME
        | PERF_SAMPLE_CPU
        | PERF_SAMPLE_RAW

        The following flags are supported:

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

        Note that you'll almost always want to include PERF_SAMPLE_RAW since that
        is the event's raw data (the event field values).
        */
        constexpr TracingSessionOptions&
        SampleType(uint32_t sampleType) noexcept
        {
            m_sampleType = sampleType & SampleTypeSupported;
            return *this;
        }

        /*
        For realtime sessions only: sets the number of bytes of unconsumed event
        data (counting both SAMPLE and non-SAMPLE events) that a realtime buffer
        must contain to trigger wakeup (see WaitForWakeup).

        The default value is WakeupWatermark(0).

        Note that wakeup conditions are evaluated per-buffer. For example, if 3
        buffers each contain 32760 bytes of pending data, none of them would
        trigger a WakeupWatermark(32768) condition.
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

        uint32_t const m_bufferSize;
        TracingMode const m_mode;
        bool m_wakeupUseWatermark;
        uint32_t m_wakeupValue;
        uint32_t m_sampleType;
    };

    /*
    Manages a tracepoint collection session.

    Basic usage:

        TracingCache cache;
        TracingSession session(
            cache, // A metadata cache to use for this session.
            TracingSessionOptions(TracingMode::RealTime, 65536) // Required settings
                .SampleType(PERF_SAMPLE_TIME | PERF_SAMPLE_RAW) // Optional setting
                .WakeupWatermark(32768)                         // Optional setting
                );

        error = session.EnableTracepoint("user_events", "MyFavoriteTracepoint");
        // ... check for error.

        error = session.EnableTracepoint("user_events", "MySecondTracepoint");
        // ... check for error.

        for (;;)
        {
            // Wait until one or more of the buffers reaches 32768 bytes of event data.
            error = session.WaitForWakeup();
            // ... check for error. Don't get into a busy loop if waiting fails!

            error = session.EnumerateSampleEventsUnordered(
                [](PerfSampleEventInfo const& event)
                {
                    // This will be called once for each SAMPLE event.
                    // It should record or process the event's data.
                    return 0; // If we return an error, enumeration will stop.
                });
            // ... check for error.
        }
    */
    class TracingSession
    {
    public:

        TracingSession(TracingSession const&) = delete;
        void operator=(TracingSession const&) = delete;
        ~TracingSession();

        /*
        May throw std::bad_alloc.

        - cache: The TracingCache that this session will use to locate metadata
          (format) information about tracepoints. Multiple sessions may share a
          cache.

        - options: Configuration settings that this session will use.

        Example:

            TracingCache cache;
            TracingSession session(
                cache, // A metadata cache to use for this session.
                TracingSessionOptions(TracingMode::RealTime, 65536) // Required settings
                    .SampleType(PERF_SAMPLE_TIME | PERF_SAMPLE_RAW) // Optional setting
                    .WakeupWatermark(32768)                         // Optional setting
                    );
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
        Returns the size (in bytes) of the buffers used for the session.
        */
        uint32_t
        BufferSize() const noexcept;

        /*
        Returns the number of buffers used for the session.
        Usually this is the number of CPUs.
        */
        uint32_t
        BufferCount() const noexcept;

        /*
        Returns the number of SAMPLE events that have been enumerated by this
        session.
        */
        uint64_t
        SampleEventCount() const noexcept;

        /*
        Returns the number of lost events that have been enumerated by this
        session. Events can be lost due to:

        - Memory allocation failure during buffer enumeration.
        - Event received while session is paused (circular mode only).
        - Event received while buffer is full (realtime mode only).
        */
        uint64_t
        LostEventCount() const noexcept;

        /*
        Returns the number of corrupt events that have been enumerated by this
        session. An event is detected as corrupt if the event's size is too
        small (based on the event's SampleType).
        */
        uint64_t
        CorruptEventCount() const noexcept;

        /*
        Returns the number of times buffer corruption has been detected by this
        session. The buffer is detected as corrupt if the buffer header has
        invalid values or if an event's size is invalid. Buffer corruption
        generally causes the buffer's remaining contents to be skipped.
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
        is configured by calling WakeupWatermark on a config before passing the
        config to the session's constructor.

        - timeout: Maximum time to wait. NULL means wait forever.
        - sigmask: Signal mask to apply before waiting. NULL means don't mask.
        - activeCount: On success, receives the number of buffers that meet the
          wakeup condition, or 0 if wait ended due to a timeout or a signal.

        Returns EPERM if the session is not realtime.

        Returns EPERM if the session is inactive. After construction and after
        Clear(), the session will be inactive until a tracepoint is added.

        Note that wakeup conditions are evaluated per-buffer. For example, if 3
        buffers each contain 32760 bytes of pending data, none of them would
        trigger a WakeupWatermark(32768) condition.
        */
        _Success_(return == 0) int
        WaitForWakeup(
            timespec const* timeout = nullptr,
            sigset_t const* sigmask = nullptr,
            _Out_opt_ int* pActiveCount = nullptr) noexcept;

        /*
        Advanced scenarios: Returns the file descriptors used for the buffers
        of the session. The returned file descriptors may be used for poll()
        but should not be read-from, written-to, closed, etc. This may be
        useful if you want to use a single thread to poll for events from
        multiple sessions or to poll for both events and some other condition.

        Returns EPERM if the session is inactive. After construction and after
        Clear(), the session will be inactive until a tracepoint is added.

        Most users should use WaitForWakeup() instead of GetBufferFiles().
        */
        _Success_(return == 0) int
        GetBufferFiles(
            _Out_writes_(BufferCount()) int* pBufferFiles) const noexcept;

        /*
        For each PERF_RECORD_SAMPLE record in the session's buffers, in timestamp
        order, invoke:

            int error = eventInfoCallback(eventInfo, args...);

        - eventInfoCallback: Callable object (e.g. a function pointer or a lambda)
          to invoke for each event.

          This callback should return an int (0 for success, errno for error). If
          eventInfoCallback returns a nonzero value then EnumerateEventsUnordered
          will immediately stop and return the specified error value.

          This callback should take a PerfSampleEventInfo const& as its first
          parameter.

          The args... (if any) are from the args... of the call to
          EnumerateEventsUnordered(eventInfoCallback, args...).

        - args...: optional additional parameters to be passed to eventInfoCallback.

        Returns: int error code (errno), or 0 for success.

        Events will be sorted based on timestamp (session's SampleType() must include
        PERF_SAMPLE_TIME) before invoking the callback. If your callback does not
        need events to be sorted based on timestamp, use EnumerateSampleEventsUnordered
        to avoid the sorting overhead.

        Note that the eventInfo provided to eventInfoCallback will contain pointers
        into the trace buffers. The pointers will become invalidated after
        eventInfoCallback returns. Any data that you need to use after that point
        must be copied.

        Note that this method does not throw any of its own exceptions, but it may
        exit via exception if eventInfoCallback() throws an exception.

        *** Circular session behavior ***

        - Pause all CPU buffers.
        - Scan all buffers to find events.
        - Sort the events based on timestamp.
        - Invoke eventInfoCallback for each event.
        - Unpause all CPU buffers.

        Note that events are lost if they arrive while the buffer is paused. The lost
        event count indicates how many events were lost during previous pauses that would
        have been part of an enumeration if there had been no pauses. It does not include
        the count of events that were lost due to the current enumeration's pause (those
        will show up after a subsequent enumeration).

        *** Realtime session behavior ***

        - Scan all buffers to find events.
        - Sort the events based on timestamp.
        - Invoke eventInfoCallback for each event.
        - Mark the enumerated events as consumed, making room for subsequent events.

        Note that events are lost if they arrive while the buffer is full. The lost
        event count indicates how many events were lost during previous periods when
        the buffer was full. It does not include the count of events that were lost
        due to the buffer being full at the start of the current enumeration (those will
        show up after a subsequent enumeration).

        Note that if eventInfoCallback throws or returns a nonzero value,
        all events (FEEDBACK: should it be no events?) will be marked as consumed.
        */
        template<class EventInfoCallbackTy, class... ArgTys>
        _Success_(return == 0) int
        EnumerateSampleEvents(
            EventInfoCallbackTy&& eventInfoCallback, // int eventInfoCallback(PerfSampleEventInfo const&, args...)
            ArgTys&&... args // optional parameters to be passed to eventInfoCallback
        ) noexcept(noexcept(eventInfoCallback( // Throws exceptions if and only if eventInfoCallback throws.
            std::declval<tracepoint_decode::PerfSampleEventInfo const&>(),
            args...)))
        {
            int error = 0;

            if (m_bufferLeaderFiles != nullptr)
            {
                OrderedEnumerator enumerator(*this);
                error = enumerator.LoadAndSort();
                while (error == 0 && enumerator.MoveNext())
                {
                    error = eventInfoCallback(m_enumEventInfo, args...);
                }
            }

            return error;
        }

        /*
        For each PERF_RECORD_SAMPLE record in the session's buffers, in unspecified
        order, invoke:

            int error = eventInfoCallback(eventInfo, args...);

        - eventInfoCallback: Callable object (e.g. a function pointer or a lambda)
          to invoke for each event.

          This callback should return an int (0 for success, errno for error). If
          eventInfoCallback returns a nonzero value then EnumerateEventsUnordered
          will immediately stop and return the specified error value.

          This callback should take a PerfSampleEventInfo const& as its first
          parameter.

          The args... (if any) are from the args... of the call to
          EnumerateEventsUnordered(eventInfoCallback, args...).

        - args...: optional additional parameters to be passed to eventInfoCallback.

        Returns: int error code (errno), or 0 for success.

        For efficiency, events will be provided in a natural enumeration order.
        This is usually not the same as event timestamp order, so you may need to
        sort the events after receiving them.

        Note that the eventInfo provided to eventInfoCallback will contain pointers
        into the trace buffers. The pointers will become invalidated after
        eventInfoCallback returns. Any data that you need to use after that point
        must be copied.

        Note that this method does not throw any of its own exceptions, but it may
        exit via exception if eventInfoCallback() throws an exception.

        *** Circular session behavior ***

        For each CPU:

        - Pause the CPU's buffer.
        - Enumerate the buffer's events newest-to-oldest.
        - Unpause the CPU's buffer.

        Note that events are lost if they arrive while the buffer is paused. The lost
        event count indicates how many events were lost during previous pauses that would
        have been part of an enumeration if there had been no pauses. It does not include
        the count of events that were lost due to the current enumeration's pause (those
        will show up after a subsequent enumeration).

        *** Realtime session behavior ***

        For each CPU:

        - Enumerate the buffer's events oldest-to-newest.
        - Mark the enumerated events as consumed, making room for subsequent events.

        Note that events are lost if they arrive while the buffer is full. The lost
        event count indicates how many events were lost during previous periods when
        the buffer was full. It does not include the count of events that were lost
        due to the buffer being full at the start of the current enumeration (those will
        show up after a subsequent enumeration).

        Note that if eventInfoCallback throws or returns a nonzero value, events will be
        marked consumed up to but not including the event for which eventInfoCallback
        returned an error.
        */
        template<class EventInfoCallbackTy, class... ArgTys>
        _Success_(return == 0) int
        EnumerateSampleEventsUnordered(
            EventInfoCallbackTy&& eventInfoCallback, // int eventInfoCallback(PerfSampleEventInfo const&, args...)
            ArgTys&&... args // optional parameters to be passed to eventInfoCallback
        ) noexcept(noexcept(eventInfoCallback( // Throws exceptions if and only if eventInfoCallback throws.
            std::declval<tracepoint_decode::PerfSampleEventInfo const&>(),
            args...)))
        {
            int error = 0;

            if (m_bufferLeaderFiles != nullptr)
            {
                for (uint32_t bufferIndex = 0; bufferIndex != m_bufferCount; bufferIndex += 1)
                {
                    UnorderedEnumerator enumerator(*this, bufferIndex);
                    while (enumerator.MoveNext())
                    {
                        error = eventInfoCallback(m_enumEventInfo, args...);
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

        struct BufferInfo
        {
            unique_mmap Mmap;
            size_t DataPos;
            size_t DataTail;
            uint64_t DataHead64;

            BufferInfo(BufferInfo const&) = delete;
            void operator=(BufferInfo const&) = delete;
            ~BufferInfo();
            BufferInfo() noexcept;
        };

        struct TracepointInfo
        {
            std::unique_ptr<unique_fd[]> BufferFiles; // size is m_BufferCount
            tracepoint_decode::PerfEventMetadata const& Metadata;
            TracepointEnableState EnableState;

            TracepointInfo(TracepointInfo const&) = delete;
            void operator=(TracepointInfo const&) = delete;
            ~TracepointInfo();
            TracepointInfo(
                std::unique_ptr<unique_fd[]> bufferFiles,
                tracepoint_decode::PerfEventMetadata const& metadata) noexcept;
        };

        struct TracepointBookmark
        {
            uint64_t Timestamp;
            uint16_t BufferIndex;
            uint16_t DataSize;
            uint32_t DataPos;
            TracepointBookmark(
                uint64_t timestamp,
                uint16_t bufferIndex,
                uint16_t dataSize,
                uint32_t dataPos) noexcept;
        };

        class UnorderedEnumerator
        {
            TracingSession& m_session;
            uint32_t const m_bufferIndex;

        public:

            UnorderedEnumerator(UnorderedEnumerator const&) = delete;
            void operator=(UnorderedEnumerator const&) = delete;
            ~UnorderedEnumerator();

            UnorderedEnumerator(
                TracingSession& session,
                uint32_t bufferIndex) noexcept;

            bool
            MoveNext() noexcept;
        };

        class OrderedEnumerator
        {
            TracingSession& m_session;
            bool m_needsCleanup;
            size_t m_index;

        public:

            OrderedEnumerator(OrderedEnumerator const&) = delete;
            void operator=(OrderedEnumerator const&) = delete;
            ~OrderedEnumerator();

            explicit
            OrderedEnumerator(TracingSession& session) noexcept;

            _Success_(return == 0) int
            LoadAndSort() noexcept;

            bool
            MoveNext() noexcept;
        };

        _Success_(return == 0) static int
        IoctlForEachFile(
            _In_reads_(filesCount) unique_fd const* files,
            unsigned filesCount,
            unsigned long request,
            _In_reads_opt_(filesCount) unique_fd const* values) noexcept;

        bool
        ParseSample(
            uint8_t const* bufferData,
            uint16_t sampleDataSize,
            uint32_t sampleDataBufferPos) noexcept;

        void
        EnumeratorEnd(uint32_t bufferIndex) const noexcept;

        void
        EnumeratorBegin(uint32_t bufferIndex) noexcept;

        template<class SampleFn, class NonSampleFn>
        bool
        EnumeratorMoveNext(
            uint32_t bufferIndex,
            SampleFn&& sampleFn,
            NonSampleFn&& nonSampleFn);

    private:

        TracingCache& m_cache;
        TracingMode const m_mode;
        bool const m_wakeupUseWatermark;
        uint32_t const m_wakeupValue;
        uint32_t const m_sampleType;
        uint32_t const m_bufferCount;
        uint32_t const m_pageSize;
        uint32_t const m_bufferSize;
        std::unique_ptr<BufferInfo[]> const m_buffers; // size is m_bufferCount
        std::unordered_map<unsigned, TracepointInfo> m_tracepointInfoById;
        std::vector<uint8_t> m_eventDataBuffer; // Double-buffer for events that wrap.
        std::vector<TracepointBookmark> m_enumSort;
        std::unique_ptr<pollfd[]> m_pollfd;
        unique_fd const* m_bufferLeaderFiles; // == m_tracepointInfoById[N].BufferFiles.get() for some N, size is m_bufferCount
        uint64_t m_sampleEventCount;
        uint64_t m_lostEventCount;
        uint64_t m_corruptEventCount;
        uint64_t m_corruptBufferCount;
        tracepoint_decode::PerfSampleEventInfo m_enumEventInfo;
        char m_enumNameBuffer[512];
    };
}
// namespace tracepoint_control

#endif // _included_TracingSession_h
