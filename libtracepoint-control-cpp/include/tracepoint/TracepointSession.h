// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*
TracepointSession class that manages a tracepoint collection session.
*/

#pragma once
#ifndef _included_TracepointSession_h
#define _included_TracepointSession_h

#include "TracepointName.h"
#include <tracepoint/PerfEventMetadata.h>
#include <tracepoint/PerfEventInfo.h>
#include <tracepoint/PerfEventSessionInfo.h>
#include <tracepoint/PerfDataFileDefs.h>
#include <tracepoint/TracepointCache.h>

#include <unordered_map>
#include <memory>
#include <vector>

#include <signal.h> // sigset_t

#ifndef _In_
#define _In_
#endif
#ifndef _In_opt_
#define _In_opt_
#endif
#ifndef _In_z_
#define _In_z_
#endif
#ifndef _In_reads_
#define _In_reads_(size)
#endif
#ifndef _In_reads_opt_
#define _In_reads_opt_(size)
#endif
#ifndef _Inout_updates_
#define _Inout_updates_(size)
#endif
#ifndef _Out_opt_
#define _Out_opt_
#endif
#ifndef _Out_writes_
#define _Out_writes_(size)
#endif

// Forward declarations:
struct pollfd; // poll.h
struct timespec; // time.h

namespace tracepoint_control
{
    /*
    Mode to use for a tracepoint collection session:

    - Circular: Used for "flight recorder" scenarios. Events are collected
      into fixed-size buffers (one buffer per CPU). When a buffer is full, new
      events overwrite old events. At any point, you can pause collection,
      enumerate the contents of the buffer, and resume collection. Events
      received while collection is paused will be lost.

      For example, you can record information about what is happening on the
      system into memory, and then if a program crashes, you save the data to
      disk so you can discover what was happening on the system in the period
      leading up to the crash.

    - RealTime: Used for logging/tracing scenarios. Events are collected into
      fixed-size buffers (one buffer per CPU). When a buffer is full, events
      will be lost. At any point, you can enumerate events from the buffer,
      consuming them to make room for new events (no pause required).
    */
    enum class TracepointSessionMode : unsigned char
    {
        /*
        Buffers will be managed as circular:

        - If buffer is full, new events will overwrite old events.
        - Natural event enumeration order is newest-to-oldest (per buffer).
        - Procedure for reading data: pause buffer, enumerate events, unpause.
          (Events arriving while buffer is paused will be lost.)
        - Cannot be notified when data becomes available.
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
    Enablement status of a tracepoint that has been added to a session.
    */
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

    /*
    Function type used for the callback of SetSaveToFds or ConfigureWithFdstore.

    The function should dup(...) the specified FD into the recorded state. The recorded
    FD should be tagged with a copy of the specified name.

    If using a systemd fdstore, the implementation might be something like:

        sd_pid_notifyf_with_fds(0, false, &fd, 1, "FDSTORE=1\nFDNAME=%s", name);
    */
    using TracepointSaveToFdsCallback = void(
        uintptr_t callbackContext,
        _In_z_ char const* name,
        int fd) noexcept;

    /*
    Type of TracepointSession's SaveToFds property.
    */
    struct TracepointSaveToFds
    {
        char const* NamePrefix;
        TracepointSaveToFdsCallback* Callback;
        uintptr_t CallbackContext;
    };

    /*
    Configuration settings for a tracepoint collection session.

    Required settings are specified as constructor parameters.
    Optional settings are set by calling methods.

    Example:

        TracepointCache cache;
        TracepointSession session(
            cache,
            TracepointSessionOptions(TracepointSessionMode::RealTime, 65536) // Required
                .WakeupWatermark(32768)                                      // Optional
                );
    */
    class TracepointSessionOptions
    {
        friend class TracepointSession;

    public:

        /*
        The flags that are set in the default value of the SampleType property:

        | PERF_SAMPLE_IDENTIFIER
        | PERF_SAMPLE_TID
        | PERF_SAMPLE_TIME
        | PERF_SAMPLE_CPU
        | PERF_SAMPLE_RAW
        */
        static constexpr auto SampleTypeDefault = 0x10486u;

        /*
        The flags that are supported for use with the SampleType property:

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
        */
        static constexpr auto SampleTypeSupported = 0x107EFu;

        /*
        Initializes a TracepointSessionOptions to configure a session with the
        specified mode and buffer size.

        - mode: controls whether the buffer is managed as Circular or RealTime.

        - bufferSize: specifies the size of each buffer in bytes. This value will be
          rounded up to a power of 2 that is equal to or greater than the page size.
          Note that the session will allocate one buffer for each CPU.
        */
        constexpr
        TracepointSessionOptions(
            TracepointSessionMode mode,
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

        The following flags are enabled by default (SampleTypeDefault):

        | PERF_SAMPLE_IDENTIFIER
        | PERF_SAMPLE_TID
        | PERF_SAMPLE_TIME
        | PERF_SAMPLE_CPU
        | PERF_SAMPLE_RAW

        The following flags are supported (SampleTypeSupported):

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
        constexpr TracepointSessionOptions&
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
        constexpr TracepointSessionOptions&
        WakeupWatermark(uint32_t wakeupWatermark) noexcept
        {
            m_wakeupUseWatermark = true;
            m_wakeupValue = wakeupWatermark;
            return *this;
        }

    private:

        uint32_t const m_bufferSize;
        TracepointSessionMode const m_mode;
        bool m_wakeupUseWatermark;
        uint32_t m_wakeupValue;
        uint32_t m_sampleType;
    };

    /*
    Configuration settings for TracepointSession::SavePerfDataFile.

    Example:

        error = session.SavePerfDataFile(
            "perf.data",
            TracepointSavePerfDataFileOptions().OpenMode(S_IRUSR | S_IWUSR));
    */
    class TracepointSavePerfDataFileOptions
    {
        friend class TracepointSession;

    public:

        /*
        Initializes a TracepointSavePerfDataFileOptions to use the default settings.

        - OpenMode = -1 (use default file permissions based on process umask).
        - TimestampFilter = 0..MAX_UINT64 (no timestamp filtering).
        - TimestampWrittenRange = nullptr (do not return timestamp range).
        */
        constexpr
        TracepointSavePerfDataFileOptions() noexcept
            : m_openMode(-1)
            , m_timestampFilterMin(0)
            , m_timestampFilterMax(UINT64_MAX)
            , m_timestampWrittenRangeFirst(nullptr)
            , m_timestampWrittenRangeLast(nullptr)
        {
            return;
        }

        /*
        Sets the permissions mode to use when creating the perf.data file. The file will
        be created as: open(perfDataFileName, O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC, OpenMode).

        Default value is -1 (use default file permissions based on process umask).
        This can be one or more of S_IRUSR, S_IWUSR, S_IRGRP, S_IWGRP, etc.
        */
        constexpr TracepointSavePerfDataFileOptions&
        OpenMode(int openMode) noexcept
        {
            m_openMode = openMode;
            return *this;
        }

        /*
        Sets the timestamp filter. Only sample events where
        timeMin <= event.timestamp <= timeMax will be written to the file. (Timestamp on
        non-sample events will be ignored.)

        Default value is 0..UINT64_MAX (no timestamp filter).

        For example, to write only events since the last Save:

        uint64_t lastTimestampWritten = 0;

        // First save does not filter-out any events based on timestamp,
        // and records the timestamp of the last event for use in next save:
        session.SavePerfDataFile("perf.data.0", TracepointSavePerfDataFileOptions()
            .TimestampFilter(lastTimestampWritten) // = 0, so no timestamp filter.
            .TimestampWrittenRange(nullptr, &lastTimestampWritten));

        ...

        // Subsequent saves use last event timestamp from previous save, and
        // update that timestamp for use in subsequent saves:
        session.SavePerfDataFile("perf.data.1", TracepointSavePerfDataFileOptions()
            .TimestampFilter(lastTimestampWritten) // filter out old events
            .TimestampWrittenRange(nullptr, &lastTimestampWritten));

        Note that in this pattern, the last event saved to file N will also be included
        in file N+1. If you want to avoid that, use
        TimestampFilter(lastTimestampWritten + 1), though that risks missing new events
        with timestamp exactly equal to lastTimestampWritten.
        */
        constexpr TracepointSavePerfDataFileOptions&
        TimestampFilter(uint64_t filterMin, uint64_t filterMax = UINT64_MAX) noexcept
        {
            m_timestampFilterMin = filterMin;
            m_timestampFilterMax = filterMax;
            return *this;
        }

        /*
        Sets the variables that will receive the timestamp range of the events that were
        written to the file.

        Default value is nullptr (do not return timestamp range).
        */
        constexpr TracepointSavePerfDataFileOptions&
        TimestampWrittenRange(_Out_opt_ uint64_t* first, _Out_opt_ uint64_t* last = nullptr) noexcept
        {
            m_timestampWrittenRangeFirst = first;
            m_timestampWrittenRangeLast = last;
            return *this;
        }

    private:

        int m_openMode;
        uint64_t m_timestampFilterMin;
        uint64_t m_timestampFilterMax;
        uint64_t* m_timestampWrittenRangeFirst;
        uint64_t* m_timestampWrittenRangeLast;
    };

    /*
    Information about a tracepoint that has been added to a session.
    */
    class TracepointInfo
    {
        // Note: Implemented as a pimpl.
        // - I want the constructor to be private on the type that the user sees.
        // - Constructor on concrete type needs to be public so that it can be
        //   constructed by a container's emplace method.
        // - Therefore the concrete type needs to be a private type with a public
        //   constructor.

        friend class TracepointSession;

        ~TracepointInfo();
        TracepointInfo() noexcept;

    public:

        TracepointInfo(TracepointInfo const&) = delete;
        void operator=(TracepointInfo const&) = delete;

        tracepoint_decode::PerfEventMetadata const&
        Metadata() const noexcept;

        tracepoint_decode::PerfEventDesc const&
        EventDesc() const noexcept;

        TracepointEnableState
        EnableState() const noexcept;

        _Success_(return == 0) int
        GetEventCount(_Out_ uint64_t* value) const noexcept;
    };

    /*
    Manages a tracepoint collection session.

    Basic usage:

        TracepointCache cache; // May be shared by multiple sessions.
        TracepointSession session(
            cache,                           // The metadata cache to use for this session.
            TracepointSessionMode::RealTime, // Collection mode: RealTime or Circular.
            65536);                          // Size of each buffer (one buffer per CPU).

        error = session.EnableTracepoint(TracepointName("user_events", "MyFirstTracepoint"));
        if (error != 0) abort(); // TODO: handle error.

        error = session.EnableTracepoint(TracepointName("user_events:MySecondTracepoint"));
        if (error != 0) abort(); // TODO: handle error.

        for (;;)
        {
            // Wait until one or more of the buffers reaches 32768 bytes of event data.
            error = session.WaitForWakeup();
            if (error != 0) abort(); // TODO: handle error. (Don't get into a busy loop if waiting fails!)

            error = session.EnumerateSampleEventsUnordered(
                [](PerfSampleEventInfo const& event)
                {
                    // This code will run once for each SAMPLE event.
                    // It should record or process the event's data.
                    return 0; // If we return an error, enumeration will stop.
                });
            if (error != 0) abort(); // TODO: handle error.
        }
    */
    class TracepointSession
    {
        friend class TracepointInfo;

        struct ReadFormat; // Forward declaration
        struct RestoreHeader; // Forward declaration

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

        struct TracepointInfoImpl : TracepointInfo
        {
            tracepoint_decode::PerfEventDesc const m_eventDesc;
            std::unique_ptr<char unsigned[]> const m_eventDescStorage;
            std::unique_ptr<unique_fd[]> const m_bufferFiles; // size is BufferFilesCount
            unsigned const m_bufferFilesCount;
            uint32_t const m_restoreInfoFileEnableStateOffset;
            TracepointEnableState m_enableState;

            TracepointInfoImpl(TracepointInfoImpl const&) = delete;
            void operator=(TracepointInfoImpl const&) = delete;
            ~TracepointInfoImpl();
            TracepointInfoImpl(
                tracepoint_decode::PerfEventDesc const& eventDesc,
                std::unique_ptr<char unsigned[]> eventDescStorage,
                std::unique_ptr<unique_fd[]> bufferFiles,
                unsigned bufferFilesCount,
                uint32_t restoreInfoFileEnableStateOffset) noexcept;

            // read(m_bufferFiles[i], data, sizeof(ReadFormat)).
            _Success_(return == 0) int
            Read(unsigned index, _Out_ ReadFormat* data) const noexcept;

            // Calls read() on each file, returns sum of the value fields.
            _Success_(return == 0) int
            GetEventCountImpl(_Out_ uint64_t* value) const noexcept;
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

        struct TracepointBookmark
        {
            uint64_t Timestamp;
            uint16_t BufferIndex;
            uint16_t RecordSize;
            uint32_t RecordBufferPos;

            TracepointBookmark(
                uint64_t timestamp,
                uint16_t bufferIndex,
                uint16_t recordSize,
                uint32_t recordBufferPos) noexcept;
        };

        class UnorderedEnumerator
        {
            TracepointSession& m_session;
            uint32_t const m_bufferIndex;

        public:

            UnorderedEnumerator(UnorderedEnumerator const&) = delete;
            void operator=(UnorderedEnumerator const&) = delete;
            ~UnorderedEnumerator();

            UnorderedEnumerator(
                TracepointSession& session,
                uint32_t bufferIndex) noexcept;

            bool
            MoveNext() noexcept;
        };

        class OrderedEnumerator
        {
            TracepointSession& m_session;
            bool m_needsCleanup;
            size_t m_index;

        public:

            OrderedEnumerator(OrderedEnumerator const&) = delete;
            void operator=(OrderedEnumerator const&) = delete;
            ~OrderedEnumerator();

            explicit
            OrderedEnumerator(TracepointSession& session) noexcept;

            _Success_(return == 0) int
            LoadAndSort() noexcept;

            bool
            MoveNext() noexcept;
        };

    public:

        class TracepointInfoRange; // Forward declaration

        class TracepointInfoIterator
        {
            friend class TracepointSession;
            friend class TracepointInfoRange;
            using InnerItTy = std::unordered_map<unsigned, TracepointInfoImpl>::const_iterator;
            InnerItTy m_it;

            explicit
            TracepointInfoIterator(InnerItTy it) noexcept;

        public:

            using difference_type = std::ptrdiff_t;
            using value_type = TracepointInfo;
            using pointer = TracepointInfo const*;
            using reference = TracepointInfo const&;
            using iterator_category = std::forward_iterator_tag;

            TracepointInfoIterator() noexcept;
            TracepointInfoIterator& operator++() noexcept;
            TracepointInfoIterator operator++(int) noexcept;
            pointer operator->() const noexcept;
            reference operator*() const noexcept;
            bool operator==(TracepointInfoIterator other) const noexcept;
            bool operator!=(TracepointInfoIterator other) const noexcept;
        };

        class TracepointInfoRange
        {
            friend class TracepointSession;
            using RangeTy = std::unordered_map<unsigned, TracepointInfoImpl>;
            RangeTy const& m_range;

            explicit
            TracepointInfoRange(RangeTy const& range) noexcept;

        public:

            TracepointInfoIterator begin() const noexcept;
            TracepointInfoIterator end() const noexcept;
        };

        TracepointSession(TracepointSession const&) = delete;
        void operator=(TracepointSession const&) = delete;
        ~TracepointSession();

        /*
        Constructs a session using defaults for advanced options.
        May throw std::bad_alloc.

        - cache: The TracepointCache that this session will use to locate metadata
          (format) information about tracepoints. Multiple sessions may share a
          cache.

        - mode: controls whether the buffer is managed as Circular or RealTime.

        - bufferSize: specifies the size of each buffer in bytes. This value will be
          rounded up to a power of 2 that is equal to or greater than the page size.
          Note that the session will allocate one buffer for each CPU.

        Example:

            TracepointCache cache;
            TracepointSession session(
                cache,                           // The metadata cache to use for this session.
                TracepointSessionMode::RealTime, // Collection mode: RealTime or Circular.
                65536);                          // Size of each buffer (one buffer per CPU).
        */
        TracepointSession(
            TracepointCache& cache,
            TracepointSessionMode mode,
            uint32_t bufferSize) noexcept(false);

        /*
        Constructs a session using TracepointSessionOptions to set advanced options.
        May throw std::bad_alloc.

        - cache: The TracepointCache that this session will use to locate metadata
          (format) information about tracepoints. Multiple sessions may share a
          cache.

        - options: Configuration settings that this session will use.

        Example:

            TracepointCache cache;
            TracepointSession session(
                cache, // The metadata cache to use for this session.
                TracepointSessionOptions(TracepointSessionMode::RealTime, 65536) // Required settings
                    .SampleType(PERF_SAMPLE_TIME | PERF_SAMPLE_RAW)              // Optional setting
                    .WakeupWatermark(32768));                                    // Optional setting
        */
        TracepointSession(
            TracepointCache& cache,
            TracepointSessionOptions const& options) noexcept(false);

        /*
        Configures this session to save/restore using systemd fdstore (see:
        https://systemd.io/FILE_DESCRIPTOR_STORE).

        Requires:

        - Session has no tracepoints added (freshly constructed or cleared).
        - namePrefix is a nul-terminated string of up to 15 alphanumeric chars, i.e.
          strlen(namePrefix) <= 15.

        Parameters:

        - namePrefix: A nul-terminated string of up to 15 alphanumeric chars, used to
          identify this session (in case multiple components are using the same fdstore).
          All names that are saved/restored by this session will start with
          "namePrefix/...".
        - callback: The user-provided callback that will be invoked when a name/FD pair
          should be recorded in the fdstore (see SetSaveToFds). This may be NULL, in
          which case ConfigureWithFdstore will not invoke SetSaveToFds.
        - callbackContext: The user-provided value that will be passed to the callback.
        - count: The number of names and FDs that are provided. This should be set to the
          value returned by sd_listen_fds_with_names. If this value is zero or negative,
          ConfigureWithFdstore will not invoke RestoreFromFds and will return success.
        - listenFdsStart: The value of the first FD to use. This should be set to the
          value of SD_LISTEN_FDS_START, which is defined in <systemd/sd-daemon.h>.
        - names: An array of nul-terminated strings that identify the FDs. This should be
          set to the names received from the call to sd_listen_fds_with_names.

        Effects:

        - If callback != NULL, configures session as if by
          SetSaveToFds(namePrefix, callback, callbackContext, false).
        - If count > 0, attempts to restore session state using names and FDs as if by
          RestoreFromFds(namePrefix, count, List(listenFdsStart, count), names).
        - Returns the error code from RestoreFromFds(...), or 0 if count <= 0.

        Note that the callback (if configured) will be invoked during the call to
        ConfigureWithFdstore (as part of the RestoreFromFds step) to record the names and
        FDs of the restored session.

        Returns 0 for success, nonzero errno for failure from RestoreFromFds(...). The
        return code should usually be ignored in release but may be useful for debugging
        or diagnostics. See RestoreFromFds(...) for possible error cases.

        In the success or partial-success cases, the session will take ownership of names
        and FDs that it uses (i.e. the names that start with "namePrefix/..." and the
        corresponding FDs). For each I where names[I] and listenFdsStart+I correspond to
        a name and fd that were used to restore state, this function will set
        names[I] = NULL and will take responsibility for calling free(name) and close(fd)
        when appropriate.

        After calling this function, even if it succeeds, you'll probably want to review
        the session's enabled/disabled events to make sure it is configured the way you
        want. For example, you may need to enable/disable some tracepoints if the desired
        session configuration has changed since last time.

        Typical usage:

            int error;

            // Session construction comes before the call to
            // sd_listen_fds_with_names(...) because constructor may throw exceptions:

            TracepointSession session1(cache, TracepointSessionOptions(...)); // May throw.
            TracepointSession session2(cache, TracepointSessionOptions(...)); // May throw.

            // Get the FDs from systemd's fdstore:

            char** names = NULL;
            int const count = sd_listen_fds_with_names(true, &names); // true = clear the fdstore

            // Restore sessions from past fdstore (best-effort, may fail), and configure
            // sessions to save to future fdstore (always, even if restore fails). Note
            // that count may be negative (if sd_listen_fds_with_names failed) or 0 (no
            // prior session), in which case nothing will be restored.

            error = session1.ConfigureWithFdstore(
                "prefix1",          // Prefix is used to identify which names/FDs belong to session1.
                &StoreFdsCallback,  // User-defined callback, typically invokes sd_pid_notifyf_with_fds.
                storeFdsContext,    // Context to pass to StoreFdsCallback.
                count,              // The return value from sd_listen_fds_with_names.
                SD_LISTEN_FDS_START,// Value defined in <systemd/sd-daemon.h>.
                names);             // The names received from sd_listen_fds_with_names.
            Log("restore session1", error);

            error = session2.ConfigureWithFdstore(
                "prefix2",          // Prefix is used to identify which names/FDs belong to session2.
                &StoreFdsCallback,  // User-defined callback, typically invokes sd_pid_notifyf_with_fds.
                storeFdsContext,    // Context to pass to StoreFdsCallback.
                count,              // The return value from sd_listen_fds_with_names.
                SD_LISTEN_FDS_START,// Value defined in <systemd/sd-daemon.h>.
                names);             // The names received from sd_listen_fds_with_names.
            Log("restore session2", error);

            // Clean up names and fds that weren't used by ConfigureWithFdstore:

            for (int i = 0; i < count; i += 1)
            {
                if (names[i] != NULL)
                {
                    free(names[i]);
                    close(SD_LISTEN_FDS_START + i);
                }
            }

            free(names);

            // Make sure that the sessions are configured properly, e.g. if the fdstore
            // was empty because this is the first time we start the session since boot,
            // or if the call to sd_listen_fds_with_names failed for any reason.

            for (auto& tracepointName : DefaultNamesForSession1)
            {
                session1.EnableTracepoint(tracepointName);
            }

            for (auto& tracepointName : DefaultNamesForSession2)
            {
                session2.EnableTracepoint(tracepointName);
            }
        */
        _Success_(return == 0) int
        ConfigureWithFdstore(
            _In_z_ char const* namePrefix,
            _In_opt_ TracepointSaveToFdsCallback* callback,
            uintptr_t callbackContext,
            int count,
            unsigned listenFdsStart,
            _Inout_updates_(count) char const** names) noexcept;

        /*
        Configures this TracepointSession object so that it takes over the management of
        an existing tracepoint collection session that was managed by a previous
        TracepointSession object.

        Parameters:

        - namePrefix: A nul-terminated string of up to 15 alphanumeric chars, used to
          identify this session in case multiple components are using the same fdstore.
          The restore operation will ignore all names do not start with "namePrefix/...".
        - count: The number of names and FDs that are provided. If this value is zero,
          RestoreFromFds will do nothing.
        - fds: An array (length = count) of FDs from the fdstore. This should contain FDs
          that were dup'ed from the FDs that were passed to a TracepointSaveToFdsCallback
          by a previous TracepointSession.
        - names: An array (length = count) of names from the fdstore. This should contain
          strings that were strdup'ed from the names that were passed to a
          TracepointSaveToFdsCallback by a previous TracepointSession.

        Requires:

        - Session has no tracepoints added (freshly constructed or cleared).
        - namePrefix is a nul-terminated string of up to 15 alphanumeric chars, i.e.
          strlen(namePrefix) <= 15.
        - Assumes that the provided fds/names were dup'ed/strdup'ed from values that were
          passed to a TracepointSaveToFdsCallback by a previous TracepointSession.
        - Assumes that the previous TracepointSession object has been Clear'ed or
          destroyed so that there is no contention on access to the underlying files.
        - Assumes that for each I in the range 0 <= I < count, each non-NULL names[I]
          that starts with "namePrefix/..." corresponds to a non-negative fds[I], i.e.
          that names[I] is strcmp-equal to the name that was provided by the call to
          TracepointSaveToFdsCallback that provided fds[I].

        Returns 0 for success, nonzero errno for failure. The return code should usually
        be ignored in release but may be useful for debugging or diagnostics. Errors may
        include the following:

        - EPERM: One or more tracepoints have already been added to the session.
          RestoreFromFds(...) may not be used after a call to EnableTracepoint(...) or
          RestoreFromFds(...). The session state was NOT restored.

        - EILSEQ: Session state is corrupt or unusable. The session state was NOT
          restored.

        - EMEDIUMTYPE: Existing session options (from constructor) conflict with new
          options (from fdstore session). The session state was NOT restored.

        - All other errors: The session state was partially restored (best effort).

        In the success or partial-restore cases, the session will take ownership of the
        names and FDs that it uses (i.e. the names that start with "namePrefix/..." and
        the corresponding FDs). For each I where names[I] and fds[I] correspond to a name
        and fd that were used, this function will set names[I] = NULL, will set
        fds[I] = -1, and will take responsibility for calling free(name) and close(fd)
        when appropriate. Caller is responsible for the remaining names and fds.

        In the success or partial-restore cases, if a SaveToFds callback is configured,
        it will be invoked for any FDs that are added to the session restore state.

        After calling this function, even if it succeeds, you'll probably want to review
        the session's enabled/disabled events to make sure it is configured the way you
        want. For example, you may need to enable/disable some tracepoints if the desired
        session configuration has changed since last time.

        Typical usage:

            int error;

            TracepointSession session1(cache, TracepointSessionOptions(...)); // May throw.
            TracepointSession session2(cache, TracepointSessionOptions(...)); // May throw.

            // Get the FDs and names from previous sessions.

            unsigned const count = (number of names/fds from previous sessions);
            char const** const names = (array of names from previous sessions);
            int* const fds = (array of fds from previous sessions);

            // Restore sessions from past fdstore (best-effort, may fail). Note that
            // count may be 0 (e.g. no prior session), in which case nothing will be
            // restored.

            error = session1.RestoreFromFds("prefix1", count, fds, names);
            Log("restore session1", error);

            error = session2.RestoreFromFds("prefix2", count, fds, names);
            Log("restore session2", error);

            // Clean up names and fds that weren't used by RestoreFromFds.

            for (int i = 0; i < count; i += 1)
            {
                if (names[i] != NULL)
                {
                    free(names[i]);
                    close(fds[i]);
                }
            }

            free(names); // Assuming that the names array was allocated with malloc.
            free(fds);   // Assuming that the fds array was allocated with malloc.

            // Make sure that the sessions are configured properly, e.g. if the count was
            // 0 because this is the first time we start the session since boot, or if
            // some other failure occurred during restore.

            for (auto& tracepointName : DefaultNamesForSession1)
            {
                session1.EnableTracepoint(tracepointName);
            }

            for (auto& tracepointName : DefaultNamesForSession2)
            {
                session2.EnableTracepoint(tracepointName);
            }

            // Configure callbacks for future restores.
            // Note that this may be done before or after the restore step.

            session1.SetSaveToFds("prefix1", &StoreFdsCallback, storeFdsContext);
            session2.SetSaveToFds("prefix2", &StoreFdsCallback, storeFdsContext);

        */
        _Success_(return == 0) int
        RestoreFromFds(
            _In_z_ char const* namePrefix,
            unsigned count,
            _Inout_updates_(count) int* fds,
            _Inout_updates_(count) char const** names) noexcept;

        /*
        Gets the values specified in the most-recent call to SetSaveToFds(...).
        Returns {"", NULL, 0} if the values have never been set or have been cleared.
        */
        TracepointSaveToFds
        GetSaveToFds() const noexcept;

        /*
        Clears the previous SaveToFds settings, if any.
        Sets SaveToFds.NamePrefix = "", SaveToFds.Callback = NULL, and
        SaveToFds.CallbackContext = 0.
        */
        void
        ClearSaveToFds() noexcept;

        /*
        Configures this session to automatically save the state of this session.
        Replaces the previous SaveToFds settings, if any.

        Requires:

        - namePrefix is a nul-terminated string of up to 15 alphanumeric chars, i.e.
          strlen(namePrefix) <= 15.

        Effects:

        - Sets SaveToFds.NamePrefix = namePrefix, SaveToFds.Callback = callback, and
          SaveToFds.CallbackContext = callbackContext.
        - If invokeCallbackForExistingFds is true, immediately invokes the provided
          callback as needed to record the current state of the system.

        After a call to SetSaveToFds, the callback will be invoked as needed to record
        subsequent changes to the session state, e.g. it could be invoked one or more
        times by a call to the EnableTracepoint(...) function.

        Note that some state is tracked internally (within the files) so not all changes
        to the TracepointSession configuration will result in a callback.
        */
        void
        SetSaveToFds(
            _In_z_ char const* namePrefix,
            _In_ TracepointSaveToFdsCallback* callback,
            uintptr_t callbackContext,
            bool invokeCallbackForExistingFds = true) noexcept;

        /*
        Returns the tracepoint cache associated with this session.
        */
        TracepointCache&
        Cache() const noexcept;

        /*
        Returns the mode that was specified at construction.
        */
        TracepointSessionMode
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
        small for the event's expected SampleType.
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

        Note that ID is from the event's common_type field and is not the PERF_SAMPLE_ID
        or PERF_SAMPLE_IDENTIFIER value.

        - Uses Cache().FindById(id) to look up the specified tracepoint.
        - If that succeeds and the specified tracepoint is in the list of session
          tracepoints, disables the tracepoint.

        Note that the tracepoint remains in the list of session tracepoints, but is set
        to the "disabled" state.

        Returns 0 for success, errno for error.

        Errors include but are not limited to:
        - ENOENT: tracefs metadata not found (tracepoint may not be registered yet)
          or tracepoint is not in the list of session tracepoints.
        - ENOTSUP: unable to find tracefs mount point.
        - EPERM: access denied to tracefs metadata.
        - ENODATA: unable to parse tracefs metadata.
        - ENOMEM: memory allocation failed.
        */
        _Success_(return == 0) int
        DisableTracepoint(unsigned id) noexcept;

        /*
        Disables collection of the specified tracepoint.

        - Uses Cache().FindOrAddFromSystem(name) to look up the specified tracepoint.
        - If that succeeds and the specified tracepoint is in the list of session
          tracepoints, disables the tracepoint.

        Note that the tracepoint remains in the list of session tracepoints, but is set
        to the "disabled" state.

        Returns 0 for success, errno for error.

        Errors include but are not limited to:
        - ENOENT: tracefs metadata not found (tracepoint may not be registered yet).
        - ENOTSUP: unable to find tracefs mount point.
        - EPERM: access denied to tracefs metadata.
        - ENODATA: unable to parse tracefs metadata.
        - ENOMEM: memory allocation failed.
        */
        _Success_(return == 0) int
        DisableTracepoint(TracepointName name) noexcept;

        /*
        Enables collection of the specified tracepoint.

        Note that ID is from the event's common_type field and is not the PERF_SAMPLE_ID
        or PERF_SAMPLE_IDENTIFIER value.

        - Uses Cache().FindById(name) to look up the specified tracepoint.
        - If that succeeds, enables the tracepoint (adding it to the list of session
          tracepoints if it is not already in the list).

        Returns 0 for success, errno for error.
        Errors include but are not limited to:
        - ENOENT: tracefs metadata not found (tracepoint may not be registered yet).
        - ENOTSUP: unable to find tracefs mount point.
        - EPERM: access denied to tracefs metadata.
        - ENODATA: unable to parse tracefs metadata.
        - ENOMEM: memory allocation failed.
        */
        _Success_(return == 0) int
        EnableTracepoint(unsigned id) noexcept;

        /*
        Enables collection of the specified tracepoint.

        - Uses Cache().FindOrAddFromSystem(name) to look up the specified tracepoint.
        - If that succeeds, enables the tracepoint (adding it to the list of session
          tracepoints if it is not already in the list).

        Returns 0 for success, errno for error.
        Errors include but are not limited to:
        - ENOENT: tracefs metadata not found (tracepoint may not be registered yet).
        - ENOTSUP: unable to find tracefs mount point.
        - EPERM: access denied to tracefs metadata.
        - ENODATA: unable to parse tracefs metadata.
        - ENOMEM: memory allocation failed.
        */
        _Success_(return == 0) int
        EnableTracepoint(TracepointName name) noexcept;

        /*
        Returns a range for enumerating the tracepoints in the session (includes
        both enabled and disabled tracepoints). Returned range is equivalent to
        TracepointInfoBegin()..TracepointInfoEnd().
        */
        TracepointInfoRange
        TracepointInfos() const noexcept;

        /*
        Returns the begin iterator of a range for enumerating the tracepoints in
        the session.
        */
        TracepointInfoIterator
        TracepointInfosBegin() const noexcept;

        /*
        Returns the end iterator of a range for enumerating the tracepoints in
        the session.
        */
        TracepointInfoIterator
        TracepointInfosEnd() const noexcept;

        /*
        Returns an iterator referencing a tracepoint in this session. Returns
        TracepointInfoEnd() if the specified tracepoint is not in this session.

        Note that ID is from the event's common_type field and is not the PERF_SAMPLE_ID
        or PERF_SAMPLE_IDENTIFIER value.
        */
        TracepointInfoIterator
        FindTracepointInfo(unsigned id) const noexcept;

        /*
        Returns an iterator referencing a tracepoint in this session. Returns
        TracepointInfoEnd() if the specified tracepoint is not in this session.
        */
        TracepointInfoIterator
        FindTracepointInfo(TracepointName name) const noexcept;

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
        Creates a perf.data-format file and writes all pending data from the
        current session's buffers to the file. This can be done for all session
        types but is usually used with circular sessions.

        File is created as:

            open(perfDataFileName, O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC, options.OpenMode());

        Returns: int error code (errno), or 0 for success.

        *** Circular session behavior ***

        For each buffer (usually one per CPU):

        - Pause collection into the buffer.
        - Write buffer's data to the file.
        - Unpause the buffer.

        Note that events are lost if they arrive while the buffer is paused. The lost
        event count indicates how many events were lost during previous pauses that would
        have been part of a enumeration if there had been no pauses. It does not include
        the count of events that were lost due to the current enumeration's pause (those
        will show up after a subsequent enumeration).

        *** Realtime session behavior ***

        For each buffer (usually one per CPU):

        - Write buffer's pending (unconsumed) events to the file.
        - Mark the enumerated events as consumed, making room for subsequent events.

        Note that events are lost if they arrive while the buffer is full. The lost
        event count indicates how many events were lost during previous periods when
        the buffer was full. It does not include the count of events that were lost
        due to the buffer being full at the start of the current enumeration (those will
        show up after a subsequent enumeration).
        */
        _Success_(return == 0) int
        SavePerfDataFile(
            _In_z_ char const* perfDataFileName,
            TracepointSavePerfDataFileOptions const& options = TracepointSavePerfDataFileOptions()) noexcept;

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

        Requires:

        - The session's SampleType() must include PERF_SAMPLE_TIME so that the events
          can be sorted based on timestamp.

        Examples:

            // Use a callback function pointer and callback context:
            error = session.EnumerateSampleEvents(functionPointer, functionContext);

            // Use a lambda:
            error = session.EnumerateSampleEvents(
                [&](PerfSampleEventInfo const& event) -> int
                {
                    ...
                    return 0;
                });

        Events will be sorted based on timestamp before invoking the callback. If your
        callback does not need events to be sorted based on timestamp, use
        EnumerateSampleEventsUnordered to avoid the sorting overhead.

        Note that the eventInfo provided to eventInfoCallback will contain pointers
        into the trace buffers. The pointers will become invalid after eventInfoCallback
        returns. Any data that you need to use after that point must be copied.

        Note that this method does not throw any of its own exceptions, but it may
        exit via exception if your eventInfoCallback(...) throws an exception.

        *** Circular session behavior ***

        - Pause collection into all buffers.
        - Scan all buffers to find events.
        - Sort the events based on timestamp.
        - Invoke eventInfoCallback(...) for each event.
        - Unpause all buffers.

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

        Note that if eventInfoCallback throws or returns a nonzero value, all events will
        be marked as consumed.
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

        Examples:

            // Use a callback function pointer and callback context:
            error = session.EnumerateSampleEventsUnordered(functionPointer, functionContext);

            // Use a lambda:
            error = session.EnumerateSampleEventsUnordered(
                [&](PerfSampleEventInfo const& event) -> int
                {
                    ...
                    return 0;
                });

        For efficiency, events will be provided in a natural enumeration order. This
        is usually not the same as event timestamp order, so you need to be able to
        accept the events out-of-order. If you need the events to be provided in
        timestamp order, use EnumerateSampleEvents.

        Note that the eventInfo provided to eventInfoCallback will contain pointers
        into the trace buffers. The pointers will become invalid after eventInfoCallback
        returns. Any data that you need to use after that point must be copied.

        Note that this method does not throw any of its own exceptions, but it may
        exit via exception if your eventInfoCallback(...) throws an exception.

        *** Circular session behavior ***

        For each buffer (usually one per CPU):

        - Pause collection into the buffer.
        - Invoke eventInfoCallback(...) for each of the buffer's events, newest-to-oldest.
        - Unpause the buffer.

        Note that events are lost if they arrive while the buffer is paused. The lost
        event count indicates how many events were lost during previous pauses that would
        have been part of an enumeration if there had been no pauses. It does not include
        the count of events that were lost due to the current enumeration's pause (those
        will show up after a subsequent enumeration).

        *** Realtime session behavior ***

        For each buffer (usually one per CPU):

        - Invoke eventInfoCallback(...) for each of the buffer's events, oldest-to-newest.
        - Mark the enumerated events as consumed, making room for subsequent events.

        Note that events are lost if they arrive while the buffer is full. The lost
        event count indicates how many events were lost during previous periods when
        the buffer was full. It does not include the count of events that were lost
        due to the buffer being full at the start of the current enumeration (those will
        show up after a subsequent enumeration).

        Note that if eventInfoCallback throws or returns a nonzero value, events will be
        marked consumed up to and including the event for which eventInfoCallback returned
        an error.
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

        _Success_(return == 0) int
        DisableTracepointImpl(tracepoint_decode::PerfEventMetadata const& metadata) noexcept;

        _Success_(return == 0) int
        EnableTracepointImpl(tracepoint_decode::PerfEventMetadata const& metadata) noexcept;

        _Success_(return == 0) static int
        IoctlForEachFile(
            _In_reads_(filesCount) unique_fd const* files,
            unsigned filesCount,
            unsigned long request,
            _In_reads_opt_(filesCount) unique_fd const* values) noexcept;

        bool
        ParseSample(
            uint8_t const* bufferData,
            uint16_t recordSize,
            uint32_t recordBufferPos) noexcept;

        void
        EnumeratorEnd(uint32_t bufferIndex) const noexcept;

        void
        EnumeratorBegin(uint32_t bufferIndex) noexcept;

        template<class RecordFn>
        bool
        EnumeratorMoveNext(
            uint32_t bufferIndex,
            RecordFn&& recordFn) noexcept(noexcept(recordFn(nullptr, 0, 0)));

        _Success_(return == 0) int
        SetTracepointEnableState(
            TracepointInfoImpl& tpi,
            bool enabled) noexcept;

        void
        InvokeSaveToFdsCallbackForExistingFds() const noexcept;

        void
        InvokeSaveToFdsCallback(uint16_t restoreFdsIndex) const noexcept;

        _Success_(return == 0) int
        AddTracepoint(
            tracepoint_decode::PerfEventMetadata const& metadata,
            std::unique_ptr<unique_fd[]> existingFiles,
            TracepointEnableState enableState) noexcept(false);

        template<class FdList>
        _Success_(return == 0) int
        RestoreFromFdsImpl(
            _In_z_ char const* namePrefix,
            unsigned count,
            FdList fdList,
            _Inout_updates_(count) char const** names) noexcept;

    private:

        // Constant

        tracepoint_decode::PerfEventSessionInfo const m_sessionInfo;
        TracepointCache& m_cache;
        TracepointSessionMode const m_mode;
        bool const m_wakeupUseWatermark;
        uint32_t const m_wakeupValue;
        uint32_t const m_sampleType;
        uint32_t const m_bufferCount;
        uint32_t const m_pageSize;
        uint32_t const m_bufferSize;

        // State

        std::unique_ptr<BufferInfo[]> const m_buffers; // size is m_bufferCount
        std::unordered_map<unsigned, TracepointInfoImpl> m_tracepointInfoByCommonType;
        std::unordered_map<uint64_t, TracepointInfoImpl const*> m_tracepointInfoBySampleId;
        unique_fd const* m_bufferLeaderFiles; // == m_tracepointInfoByCommonType[N].BufferFiles.get() for some N, size is m_bufferCount
        
        std::vector<int> m_restoreFds;
        unique_fd m_restoreInfoFile;
        uint32_t m_restoreInfoFilePos;
        char m_saveToFdsNamePrefix[16];
        TracepointSaveToFdsCallback* m_saveToFdsCallback;
        uintptr_t m_saveToFdsCallbackContext;

        // Statistics

        uint64_t m_sampleEventCount;
        uint64_t m_lostEventCount;
        uint64_t m_corruptEventCount;
        uint64_t m_corruptBufferCount;

        // Transient

        std::vector<uint8_t> m_eventDataBuffer; // Double-buffer for events that wrap.
        std::vector<TracepointBookmark> m_enumeratorBookmarks;
        std::unique_ptr<pollfd[]> m_pollfd;
        tracepoint_decode::PerfSampleEventInfo m_enumEventInfo;
    };

    using TracepointInfoRange = TracepointSession::TracepointInfoRange;
    using TracepointInfoIterator = TracepointSession::TracepointInfoIterator;
}
// namespace tracepoint_control

#endif // _included_TracepointSession_h
