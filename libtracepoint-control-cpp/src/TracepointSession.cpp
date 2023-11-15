// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <tracepoint/TracepointSession.h>
#include <tracepoint/PerfDataFileWriter.h>
#include <algorithm>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <asm/unistd.h> // __NR_perf_event_open
#include <linux/perf_event.h>

#ifdef NDEBUG
#define DEBUG_PRINTF(...) ((void)0)
#else // NDEBUG
#include <stdio.h>
#define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#endif // NDEBUG

#ifndef _Inout_
#define _Inout_
#endif

using namespace std::string_view_literals;
using namespace tracepoint_control;
using namespace tracepoint_decode;

static constexpr unsigned RestoreFdsMax = 65535;
static constexpr char FdNameSeparator = '/';

// Helpers

static long
perf_event_open(
    struct perf_event_attr* pe,
    pid_t pid,
    int cpuIndex,
    int groupFd,
    unsigned long flags) noexcept
{
    return syscall(__NR_perf_event_open, pe, pid, cpuIndex, groupFd, flags);
}

static perf_event_header const*
BufferDataPosToHeader(
    uint8_t const* bufferData,
    uint32_t recordBufferPos) noexcept
{
    assert(0 == (recordBufferPos & 7));
    return reinterpret_cast<perf_event_header const*>(bufferData + recordBufferPos);
}

// Return the smallest power of 2 that is >= pageSize and >= bufferSize.
// Assumes pageSize is a power of 2.
static size_t
RoundUpBufferSize(uint32_t pageSize, size_t bufferSize) noexcept
{
    static constexpr size_t BufferSizeMax =
        static_cast<size_t>(1) << (sizeof(size_t) * 8 - 1);

    assert(0 != pageSize);
    assert(0 == (pageSize & (pageSize - 1)));
    assert(bufferSize <= BufferSizeMax);

    for (size_t roundedSize = pageSize; roundedSize != 0; roundedSize <<= 1)
    {
        if (roundedSize >= bufferSize)
        {
            return roundedSize;
        }
    }

    return BufferSizeMax;
}

static tracepoint_decode::PerfEventSessionInfo
MakeSessionInfo(uint32_t clockid) noexcept
{
    static const auto Billion = 1000000000u;

    tracepoint_decode::PerfEventSessionInfo sessionInfo;
    sessionInfo.SetClockid(clockid);

    timespec monotonic;
    timespec realtime;
    if (0 == clock_gettime(clockid, &monotonic) &&
        0 == clock_gettime(CLOCK_REALTIME, &realtime))
    {
        uint64_t monotonicNS, realtimeNS;
        if (monotonic.tv_sec < realtime.tv_sec ||
            (monotonic.tv_sec == realtime.tv_sec && monotonic.tv_nsec < realtime.tv_nsec))
        {
            monotonicNS = 0;
            realtimeNS = static_cast<uint64_t>(realtime.tv_sec - monotonic.tv_sec) * Billion
                + realtime.tv_nsec - monotonic.tv_nsec;
        }
        else
        {
            realtimeNS = 0;
            monotonicNS = static_cast<uint64_t>(monotonic.tv_sec - realtime.tv_sec) * Billion
                + monotonic.tv_nsec - realtime.tv_nsec;
        }

        sessionInfo.SetClockData(clockid, realtimeNS, monotonicNS);
    }

    return sessionInfo;
}

template<class T>
static void
AppendValue(_Inout_ std::vector<char>* buffer, T const& value)
{
    auto const pbValue = reinterpret_cast<char const*>(&value);
    buffer->insert(buffer->end(), pbValue, pbValue + sizeof(T));
}

namespace
{
    struct PointerFdList
    {
        int* const fds;

        int
        Get(unsigned index) const noexcept
        {
            return fds[index];
        }

        void
        Clear(unsigned index) const noexcept
        {
            fds[index] = -1;
        }
    };

    struct SequentialFdList
    {
        unsigned const start;

        int
        Get(unsigned index) const noexcept
        {
            return start + index;
        }

        void
        Clear(unsigned) const noexcept
        {
            return;
        }
    };

    /*
    Manages a scatter-gather list of chunks of memory that need to be written to
    the perf.data file. This allows us to reduce the number of kernel calls.
    Instead of calling write() once per event (or twice if the event wraps), we
    call writev once for every 16 noncontinguous blocks of memory to be written.
    */
    class IovecList
    {
        static constexpr unsigned Max = 16;
        unsigned m_used = 0;
        iovec m_vecs[Max];

    public:

        unsigned
            RoomLeft() const noexcept
        {
            return Max - m_used;
        }

        void
            Add(uint8_t const* p, size_t c) noexcept
        {
            assert(m_used < Max); // Caller is responsible for checking RoomLeft().
            auto const lastVec = m_used - 1;
            if (0 != m_used &&
                p == static_cast<uint8_t*>(m_vecs[lastVec].iov_base) + m_vecs[lastVec].iov_len)
            {
                // This block immediately follows the last block. Merge the blocks.
                m_vecs[lastVec].iov_len += c;
            }
            else
            {
                m_vecs[m_used].iov_base = const_cast<uint8_t*>(p);
                m_vecs[m_used].iov_len = c;
                m_used += 1;
            }
        }

        _Success_(return == 0) int
        Flush(PerfDataFileWriter& output) noexcept
        {
            assert(m_used <= Max);

            int error;
            if (m_used == 0)
            {
                error = 0;
            }
            else
            {
                size_t cbToWrite = 0;
                for (unsigned i = 0; i != m_used; i += 1)
                {
                    cbToWrite += m_vecs[i].iov_len;
                    if (cbToWrite < m_vecs[i].iov_len)
                    {
                        error = ERANGE;
                        goto Done;
                    }
                }

                for (auto skip = 0u;;)
                {
                    auto const cbWritten = output.WriteEventDataIovecs(m_vecs + skip, m_used - skip);
                    if (cbWritten < 0)
                    {
                        error = errno;
                        break;
                    }
                    else if (static_cast<size_t>(cbWritten) == cbToWrite)
                    {
                        error = 0;
                        break;
                    }

                    // Partial write. Skip what was written and try again.

                    auto cbToSkip = static_cast<size_t>(cbWritten);
                    assert(cbToWrite > cbToSkip);
                    cbToWrite -= cbToSkip;

                    while (cbToSkip >= m_vecs[skip].iov_len)
                    {
                        cbToSkip -= m_vecs[skip].iov_len;
                        skip += 1;
                    }

                    assert(skip < m_used);
                    m_vecs[skip].iov_base = static_cast<uint8_t*>(m_vecs[skip].iov_base) + cbToSkip;
                    m_vecs[skip].iov_len = m_vecs[skip].iov_len - cbToSkip;
                }

                m_used = 0;
            }

        Done:

            return error;
        }
    };
}

// TracepointInfo

TracepointInfo::~TracepointInfo()
{
    return;
}

TracepointInfo::TracepointInfo() noexcept
{
    return;
}

tracepoint_decode::PerfEventMetadata const&
TracepointInfo::Metadata() const noexcept
{
    auto& self = *static_cast<TracepointSession::TracepointInfoImpl const*>(this);
    return *self.m_eventDesc.metadata;
}

tracepoint_decode::PerfEventDesc const&
TracepointInfo::EventDesc() const noexcept
{
    auto& self = *static_cast<TracepointSession::TracepointInfoImpl const*>(this);
    return self.m_eventDesc;
}

TracepointEnableState
TracepointInfo::EnableState() const noexcept
{
    auto& self = *static_cast<TracepointSession::TracepointInfoImpl const*>(this);
    return self.m_enableState;
}

_Success_(return == 0) int
TracepointInfo::GetEventCount(_Out_ uint64_t* value) const noexcept
{
    auto& self = *static_cast<TracepointSession::TracepointInfoImpl const*>(this);
    return self.GetEventCountImpl(value);
}

// ReadFormat

struct TracepointSession::ReadFormat
{
    // This needs to match the attr.read_format used for tracepoints.
    uint64_t value;
    uint64_t id;
};

// RestoreHeader

struct TracepointSession::RestoreHeader
{
    uint16_t Magic;
    uint16_t Size;
    uint32_t ClockId;
    bool ClockOffsetKnown;
    TracepointSessionMode Mode;
    bool WakeupUseWatermark;
    char Padding1;
    uint32_t WakeupValue;
    uint32_t SampleType;
    uint32_t BufferCount;
    uint32_t PageSize;
    uint32_t BufferSize;
    /*
    Followed by 1 or more tightly-packed instances of:
        TracepointEnableState EnableState;
        uint32_t FullNameLength;
        char FullName[FullNameLength]; // Not nul-terminated.
    */

    explicit
    RestoreHeader(_In_reads_bytes_(sizeof(RestoreHeader)) char const* bytes) noexcept
    {
        memcpy(this, bytes, sizeof(RestoreHeader));
    }

    explicit constexpr
    RestoreHeader(TracepointSession const& session) noexcept
        : Magic(0x0001)
        , Size(sizeof(RestoreHeader))
        , ClockId(session.m_sessionInfo.ClockId())
        , ClockOffsetKnown(session.m_sessionInfo.ClockOffsetKnown())
        , Mode(session.m_mode)
        , WakeupUseWatermark(session.m_wakeupUseWatermark)
        , Padding1(0)
        , WakeupValue(session.m_wakeupValue)
        , SampleType(session.m_sampleType)
        , BufferCount(session.m_bufferCount)
        , PageSize(session.m_pageSize)
        , BufferSize(session.m_bufferSize)
    {
        return;
    }

    constexpr bool
    operator==(RestoreHeader const& other) const noexcept
    {
        return
            Magic == other.Magic &&
            Size == other.Size &&
            ClockId == other.ClockId &&
            ClockOffsetKnown == other.ClockOffsetKnown &&
            Mode == other.Mode &&
            WakeupUseWatermark == other.WakeupUseWatermark &&
            //Padding1 == other.Padding1 &&
            WakeupValue == other.WakeupValue &&
            SampleType == other.SampleType &&
            BufferCount == other.BufferCount &&
            PageSize == other.PageSize &&
            BufferSize == other.BufferSize;
    }

    constexpr bool
    operator!=(RestoreHeader const& other) const noexcept
    {
        return !(*this == other);
    }
};

// TracepointInfoImpl

TracepointSession::TracepointInfoImpl::~TracepointInfoImpl()
{
    return;
}

TracepointSession::TracepointInfoImpl::TracepointInfoImpl(
    tracepoint_decode::PerfEventDesc const& eventDesc,
    std::unique_ptr<char unsigned[]> eventDescStorage,
    std::unique_ptr<unique_fd[]> bufferFiles,
    unsigned bufferFilesCount,
    uint32_t restoreInfoFileEnableStateOffset
    ) noexcept
    : m_eventDesc(eventDesc)
    , m_eventDescStorage(std::move(eventDescStorage))
    , m_bufferFiles(std::move(bufferFiles))
    , m_bufferFilesCount(bufferFilesCount)
    , m_restoreInfoFileEnableStateOffset(restoreInfoFileEnableStateOffset)
    , m_enableState(TracepointEnableState::Unknown)
{
    return;
}

_Success_(return == 0) int
TracepointSession::TracepointInfoImpl::Read(unsigned index, _Out_ ReadFormat* data) const noexcept
{
    assert(index < m_bufferFilesCount);
    auto const size = read(m_bufferFiles[index].get(), data, sizeof(data[0]));
    int const error = size == sizeof(data[0])
        ? 0
        : size < 0
        ? errno
        : EPIPE;
    return error;
}

_Success_(return == 0) int
TracepointSession::TracepointInfoImpl::GetEventCountImpl(_Out_ uint64_t * value) const noexcept
{
    int error = 0;
    uint64_t total = 0;

    for (unsigned i = 0; i != m_bufferFilesCount; i += 1)
    {
        ReadFormat data;
        error = Read(i, &data);
        if (error != 0)
        {
            break;
        }

        total += data.value;
    }

    *value = total;
    return error;
}

// BufferInfo

TracepointSession::BufferInfo::~BufferInfo()
{
    return;
}

TracepointSession::BufferInfo::BufferInfo() noexcept
    : Mmap()
    , DataPos()
    , DataTail()
    , DataHead64()
{
    return;
}

// TracepointBookmark

TracepointSession::TracepointBookmark::TracepointBookmark(
    uint64_t timestamp,
    uint16_t bufferIndex,
    uint16_t recordSize,
    uint32_t recordBufferPos) noexcept
    : Timestamp(timestamp)
    , BufferIndex(bufferIndex)
    , RecordSize(recordSize)
    , RecordBufferPos(recordBufferPos)
{
    return;
}

// UnorderedEnumerator

TracepointSession::UnorderedEnumerator::~UnorderedEnumerator()
{
    m_session.EnumeratorEnd(m_bufferIndex);
}

TracepointSession::UnorderedEnumerator::UnorderedEnumerator(
    TracepointSession& session,
    uint32_t bufferIndex) noexcept
    : m_session(session)
    , m_bufferIndex(bufferIndex)
{
    m_session.EnumeratorBegin(bufferIndex);
}

bool
TracepointSession::UnorderedEnumerator::MoveNext() noexcept
{
    auto& session = m_session;
    return session.EnumeratorMoveNext(
        m_bufferIndex,
        [&session](
            uint8_t const* bufferData,
            uint16_t recordSize,
            uint32_t recordBufferPos)
        {
            auto const headerType = BufferDataPosToHeader(bufferData, recordBufferPos)->type;
            return headerType == PERF_RECORD_SAMPLE &&
                session.ParseSample(bufferData, recordSize, recordBufferPos);
        });
}

//  OrderedEnumerator

TracepointSession::OrderedEnumerator::~OrderedEnumerator()
{
    if (m_needsCleanup)
    {
        for (unsigned bufferIndex = 0; bufferIndex != m_session.m_bufferCount; bufferIndex += 1)
        {
            m_session.EnumeratorEnd(bufferIndex);
        }
    }
}

TracepointSession::OrderedEnumerator::OrderedEnumerator(TracepointSession& session) noexcept
    : m_session(session)
    , m_needsCleanup(false)
    , m_index(0)
{
    return;
}

_Success_(return == 0) int
TracepointSession::OrderedEnumerator::LoadAndSort() noexcept
{
    int error;
    auto& session = m_session;

    if (0 == (session.m_sampleType & PERF_SAMPLE_TIME))
    {
        error = EPERM;
    }
    else try
    {
        auto const sampleType = session.m_sampleType;
        unsigned const bytesBeforeTime = sizeof(uint64_t) * (
            1u + // perf_event_header
            (0 != (sampleType & PERF_SAMPLE_IDENTIFIER)) +
            (0 != (sampleType & PERF_SAMPLE_IP)) +
            (0 != (sampleType & PERF_SAMPLE_TID)));

        for (uint32_t bufferIndex = 0; bufferIndex != session.m_bufferCount; bufferIndex += 1)
        {
            session.EnumeratorBegin(bufferIndex);
        }

        // Circular: If we throw an exception, we need to unpause during cleanup.
        // Realtime: If we throw an exception, we don't update tail pointers during cleanup.
        m_needsCleanup = !session.IsRealtime();

        session.m_enumeratorBookmarks.clear();
        for (uint32_t bufferIndex = 0; bufferIndex != session.m_bufferCount; bufferIndex += 1)
        {
            auto const startSize = session.m_enumeratorBookmarks.size();

            // Only need to call EnumeratorMoveNext once per buffer - it will loop until a callback
            // returns true or it reaches end of buffer, and our callback never returns true.
            session.EnumeratorMoveNext(
                bufferIndex,
                [bufferIndex, bytesBeforeTime, &session](
                    uint8_t const* bufferData,
                    uint16_t recordSize,
                    uint32_t recordBufferPos)
                {
                    assert(0 == (recordSize & 7));
                    assert(0 == (recordBufferPos & 7));

                    if (PERF_RECORD_SAMPLE != BufferDataPosToHeader(bufferData, recordBufferPos)->type)
                    {
                        return false; // Keep going.
                    }

                    if (recordSize <= bytesBeforeTime)
                    {
                        session.m_corruptEventCount += 1;
                        return false;
                    }

                    auto const timePos = (recordBufferPos + bytesBeforeTime) & (session.m_bufferSize - 1);
                    auto const timestamp = *reinterpret_cast<uint64_t const*>(bufferData + timePos);
                    session.m_enumeratorBookmarks.emplace_back( // May throw bad_alloc.
                        timestamp,
                        bufferIndex,
                        recordSize,
                        recordBufferPos);
                    return false; // Keep going.
                });

            if (!session.IsRealtime())
            {
                // Circular buffers enumerate in reverse order. Fix that.
                auto const endSize = session.m_enumeratorBookmarks.size();
                auto const bookmarksData = session.m_enumeratorBookmarks.data();
                std::reverse(bookmarksData + startSize, bookmarksData + endSize);
            }
        }

        auto const bookmarksData = session.m_enumeratorBookmarks.data();
        std::stable_sort(
            bookmarksData,
            bookmarksData + session.m_enumeratorBookmarks.size(),
            [](TracepointBookmark const& a, TracepointBookmark const& b) noexcept
            {
                return a.Timestamp < b.Timestamp;
            });

        // From here on, do full cleanup.
        m_needsCleanup = true;
        error = 0;
    }
    catch (...)
    {
        error = ENOMEM;
    }

    return error;
}

bool
TracepointSession::OrderedEnumerator::MoveNext() noexcept
{
    auto& session = m_session;
    auto const buffers = session.m_buffers.get();
    auto const pageSize = session.m_pageSize;
    auto const enumeratorBookmarksData = session.m_enumeratorBookmarks.data();
    auto const enumeratorBookmarksSize = session.m_enumeratorBookmarks.size();
    while (m_index < enumeratorBookmarksSize)
    {
        auto const& item = enumeratorBookmarksData[m_index];
        m_index += 1;

        if (session.ParseSample(
            static_cast<uint8_t const*>(buffers[item.BufferIndex].Mmap.get()) + pageSize,
            item.RecordSize,
            item.RecordBufferPos))
        {
            return true;
        }
    }

    return false;
}

// TracepointInfoIterator

TracepointInfoIterator::TracepointInfoIterator(InnerItTy it) noexcept
    : m_it(it)
{
    return;
}

TracepointInfoIterator::TracepointInfoIterator() noexcept
    : m_it()
{
    return;
}

TracepointInfoIterator&
TracepointInfoIterator::operator++() noexcept
{
    ++m_it;
    return *this;
}

TracepointInfoIterator
TracepointInfoIterator::operator++(int) noexcept
{
    TracepointInfoIterator old(m_it);
    ++m_it;
    return old;
}

TracepointInfoIterator::pointer
TracepointInfoIterator::operator->() const noexcept
{
    return &m_it->second;
}

TracepointInfoIterator::reference
TracepointInfoIterator::operator*() const noexcept
{
    return m_it->second;
}

bool
TracepointInfoIterator::operator==(TracepointInfoIterator other) const noexcept
{
    return m_it == other.m_it;
}

bool
TracepointInfoIterator::operator!=(TracepointInfoIterator other) const noexcept
{
    return m_it != other.m_it;
}

// TracepointInfoRange

TracepointInfoRange::TracepointInfoRange(RangeTy const& range) noexcept
    : m_range(range)
{
    return;
}

TracepointInfoIterator
TracepointInfoRange::begin() const noexcept
{
    return TracepointInfoIterator(m_range.begin());
}

TracepointInfoIterator
TracepointInfoRange::end() const noexcept
{
    return TracepointInfoIterator(m_range.end());
}

// TracepointSession

TracepointSession::~TracepointSession()
{
    return;
}

TracepointSession::TracepointSession(
    TracepointCache& cache,
    TracepointSessionMode mode,
    uint32_t bufferSize) noexcept(false)
    : TracepointSession(cache, TracepointSessionOptions(mode, bufferSize))
{
    return;
}

TracepointSession::TracepointSession(
    TracepointCache& cache,
    TracepointSessionOptions const& options) noexcept(false)
    : m_sessionInfo(MakeSessionInfo(CLOCK_MONOTONIC_RAW))
    , m_cache(cache)
    , m_mode(options.m_mode)
    , m_wakeupUseWatermark(options.m_wakeupUseWatermark)
    , m_wakeupValue(options.m_wakeupValue)
    , m_sampleType(options.m_sampleType)
    , m_bufferCount(sysconf(_SC_NPROCESSORS_ONLN))
    , m_pageSize(sysconf(_SC_PAGESIZE))
    , m_bufferSize(RoundUpBufferSize(m_pageSize, options.m_bufferSize))
    , m_buffers(std::make_unique<BufferInfo[]>(m_bufferCount)) // may throw bad_alloc.
    , m_tracepointInfoByCommonType() // may throw bad_alloc (but probably doesn't).
    , m_tracepointInfoBySampleId() // may throw bad_alloc (but probably doesn't).
    , m_bufferLeaderFiles(nullptr)
    , m_restoreFds()
    , m_restoreInfoFile()
    , m_restoreInfoFilePos(sizeof(RestoreHeader))
    , m_saveToFdsNamePrefix()
    , m_saveToFdsCallback(nullptr)
    , m_saveToFdsCallbackContext(0)
    , m_sampleEventCount(0)
    , m_lostEventCount(0)
    , m_corruptEventCount(0)
    , m_corruptBufferCount(0)
    , m_eventDataBuffer()
    , m_enumeratorBookmarks()
    , m_pollfd(nullptr)
    , m_enumEventInfo()
{
    assert(options.m_mode <= TracepointSessionMode::RealTime);
}

_Success_(return == 0) int
TracepointSession::ConfigureWithFdstore(
    _In_z_ char const* namePrefix,
    _In_opt_ TracepointSaveToFdsCallback* callback,
    uintptr_t callbackContext,
    int count,
    unsigned listenFdsStart,
    _Inout_updates_(count) char const** names) noexcept
{
    if (callback != nullptr)
    {
        SetSaveToFds(namePrefix, callback, callbackContext, false);
    }

    auto const error = count > 0
        ? RestoreFromFdsImpl(namePrefix, count, SequentialFdList{ listenFdsStart }, names)
        : 0;
    return error;
}

_Success_(return == 0) int
TracepointSession::RestoreFromFds(
    _In_z_ char const* namePrefix,
    unsigned count,
    _Inout_updates_(count) int* fds,
    _Inout_updates_(count) char const** names) noexcept
{
    auto const error = count > 0
        ? RestoreFromFdsImpl(namePrefix, count, PointerFdList{ fds }, names)
        : 0;
    return error;
}

TracepointSaveToFds
TracepointSession::GetSaveToFds() const noexcept
{
    return { m_saveToFdsNamePrefix, m_saveToFdsCallback, m_saveToFdsCallbackContext };
}

void
TracepointSession::ClearSaveToFds() noexcept
{
    m_saveToFdsNamePrefix[0] = '\0';
    m_saveToFdsCallback = nullptr;
    m_saveToFdsCallbackContext = 0;
}

void
TracepointSession::SetSaveToFds(
    _In_z_ char const* namePrefix,
    _In_ TracepointSaveToFdsCallback* callback,
    uintptr_t callbackContext,
    bool invokeCallbackForExistingFds) noexcept
{
    assert(strlen(namePrefix) < sizeof(m_saveToFdsNamePrefix));

    strncpy(m_saveToFdsNamePrefix, namePrefix, sizeof(m_saveToFdsNamePrefix) - 1);
    m_saveToFdsNamePrefix[sizeof(m_saveToFdsNamePrefix) - 1] = '\0';
    m_saveToFdsCallback = callback;
    m_saveToFdsCallbackContext = callbackContext;

    if (invokeCallbackForExistingFds)
    {
        InvokeSaveToFdsCallbackForExistingFds();
    }
}

TracepointCache&
TracepointSession::Cache() const noexcept
{
    return m_cache;
}

TracepointSessionMode
TracepointSession::Mode() const noexcept
{
    return m_mode;
}

bool
TracepointSession::IsRealtime() const noexcept
{
    return m_mode != TracepointSessionMode::Circular;
}

uint32_t
TracepointSession::BufferSize() const noexcept
{
    return m_bufferSize;
}

uint32_t
TracepointSession::BufferCount() const noexcept
{
    return m_bufferCount;
}

uint64_t
TracepointSession::SampleEventCount() const noexcept
{
    return m_sampleEventCount;
}

uint64_t
TracepointSession::LostEventCount() const noexcept
{
    return m_lostEventCount;
}

uint64_t
TracepointSession::CorruptEventCount() const noexcept
{
    return m_corruptEventCount;
}

uint64_t
TracepointSession::CorruptBufferCount() const noexcept
{
    return m_corruptBufferCount;
}

void
TracepointSession::Clear() noexcept
{
    for (uint32_t bufferIndex = 0; bufferIndex != m_bufferCount; bufferIndex += 1)
    {
        m_buffers[bufferIndex].Mmap.reset();
    }

    m_tracepointInfoByCommonType.clear();
    m_tracepointInfoBySampleId.clear();
    m_bufferLeaderFiles = nullptr;
    m_restoreFds.clear();
    m_restoreInfoFile.reset();
    m_restoreInfoFilePos = sizeof(RestoreHeader);

    ClearSaveToFds();

    m_sampleEventCount = 0;
    m_lostEventCount = 0;
    m_corruptEventCount = 0;
    m_corruptBufferCount = 0;
}

_Success_(return == 0) int
TracepointSession::DisableTracepoint(unsigned id) noexcept
{
    auto const metadata = m_cache.FindById(id);
    auto const error = metadata == nullptr
        ? ENOENT
        : DisableTracepointImpl(*metadata);

    return error;
}

_Success_(return == 0) int
TracepointSession::DisableTracepoint(TracepointName name) noexcept
{
    int error;

    PerfEventMetadata const* metadata;
    error = m_cache.FindOrAddFromSystem(name, &metadata);
    if (error == 0)
    {
        error = DisableTracepointImpl(*metadata);
    }

    return error;
}

_Success_(return == 0) int
TracepointSession::EnableTracepoint(unsigned id) noexcept
{
    auto const metadata = m_cache.FindById(id);
    auto const error = metadata == nullptr
        ? ENOENT
        : EnableTracepointImpl(*metadata);

    return error;
}

_Success_(return == 0) int
TracepointSession::EnableTracepoint(TracepointName name) noexcept
{
    int error;

    PerfEventMetadata const* metadata;
    error = m_cache.FindOrAddFromSystem(name, &metadata);
    if (error == 0)
    {
        error = EnableTracepointImpl(*metadata);
    }

    return error;
}

TracepointInfoRange
TracepointSession::TracepointInfos() const noexcept
{
    return TracepointInfoRange(m_tracepointInfoByCommonType);
}

TracepointInfoIterator
TracepointSession::TracepointInfosBegin() const noexcept
{
    return TracepointInfoIterator(m_tracepointInfoByCommonType.begin());
}

TracepointInfoIterator
TracepointSession::TracepointInfosEnd() const noexcept
{
    return TracepointInfoIterator(m_tracepointInfoByCommonType.end());
}

TracepointInfoIterator
TracepointSession::FindTracepointInfo(unsigned id) const noexcept
{
    return TracepointInfoIterator(m_tracepointInfoByCommonType.find(id));
}

TracepointInfoIterator
TracepointSession::FindTracepointInfo(TracepointName name) const noexcept
{
    auto metadata = m_cache.FindByName(name);
    return TracepointInfoIterator(metadata
        ? m_tracepointInfoByCommonType.find(metadata->Id())
        : m_tracepointInfoByCommonType.end());
}

_Success_(return == 0) int
TracepointSession::WaitForWakeup(
    timespec const* timeout,
    sigset_t const* sigmask,
    _Out_opt_ int* pActiveCount) noexcept
{
    int error;
    int activeCount;

    if (!IsRealtime() || m_bufferLeaderFiles == nullptr)
    {
        activeCount = 0;
        error = EPERM;
    }
    else try
    {
        if (m_pollfd == nullptr)
        {
            m_pollfd = std::make_unique<pollfd[]>(m_bufferCount);
        }

        for (unsigned i = 0; i != m_bufferCount; i += 1)
        {
            m_pollfd[i] = { m_bufferLeaderFiles[i].get(), POLLIN, 0 };
        }

        activeCount = ppoll(m_pollfd.get(), m_bufferCount, timeout, sigmask);
        if (activeCount < 0)
        {
            activeCount = 0;
            error = errno;
        }
        else
        {
            error = 0;
        }
    }
    catch (...)
    {
        activeCount = 0;
        error = ENOMEM;
    }

    if (pActiveCount)
    {
        *pActiveCount = activeCount;
    }
    return error;
}

_Success_(return == 0) int
TracepointSession::GetBufferFiles(
    _Out_writes_(BufferCount()) int* pBufferFiles) const noexcept
{
    int error;

    if (m_bufferLeaderFiles == nullptr)
    {
        memset(pBufferFiles, 0, m_bufferCount * sizeof(pBufferFiles[0]));
        error = EPERM;
    }
    else
    {
        for (unsigned i = 0; i != m_bufferCount; i += 1)
        {
            pBufferFiles[i] = m_bufferLeaderFiles[i].get();
        }

        error = 0;
    }

    return error;
}

_Success_(return == 0) int
TracepointSession::SavePerfDataFile(
    _In_z_ char const* perfDataFileName,
    TracepointSavePerfDataFileOptions const& options) noexcept
{
    int error = 0;
    PerfDataFileWriter output;
    IovecList vecList;
    struct {
        uint64_t first;
        uint64_t last;
    } times = { ~uint64_t(0), 0 };

    if (options.m_timestampWrittenRangeFirst)
    {
        *options.m_timestampWrittenRangeFirst = 0;
    }

    if (options.m_timestampWrittenRangeLast)
    {
        *options.m_timestampWrittenRangeLast = 0;
    }

    error = output.Create(perfDataFileName, options.m_openMode);
    if (error != 0)
    {
        goto Done;
    }

    // Write event data:

    if (m_bufferLeaderFiles != nullptr)
    {
        auto recordFn = [this, &vecList, &times, &options](
            uint8_t const* bufferData,
            uint16_t recordSize,
            uint32_t recordBufferPos) noexcept
        {
            // Look up the correct value for m_enumEventInfo.event_desc.

            m_enumEventInfo.event_desc = nullptr;
            if (PERF_RECORD_SAMPLE == BufferDataPosToHeader(bufferData, recordBufferPos)->type)
            {
                // TODO: We don't need a full parse here. Could potentially
                // save a few cycles by inlining ParseSample and removing the
                // parts we don't need.

                // If this succeeds it will set m_enumEventInfo.
                if (ParseSample(bufferData, recordSize, recordBufferPos))
                {
                    if (options.m_timestampFilterMin > m_enumEventInfo.time ||
                        options.m_timestampFilterMax < m_enumEventInfo.time)
                    {
                        // TODO: Optimization - in some cases, this means we're going
                        // to skip the rest of the buffer, so perhaps detect those
                        // cases and stop the enumeration?
                        return false; // Skip this event.
                    }

                    if (m_enumEventInfo.time < times.first)
                    {
                        times.first = m_enumEventInfo.time;
                    }

                    if (m_enumEventInfo.time > times.last)
                    {
                        times.last = m_enumEventInfo.time;
                    }
                }
            }

            // Add event data to vecList.

            auto const unmaskedPosEnd = recordBufferPos + recordSize;
            if (unmaskedPosEnd <= m_bufferSize)
            {
                // Event does not wrap.
                vecList.Add(bufferData + recordBufferPos, recordSize);
            }
            else
            {
                // Event wraps.
                vecList.Add(bufferData + recordBufferPos, m_bufferSize - recordBufferPos);
                vecList.Add(bufferData, unmaskedPosEnd - m_bufferSize);
            }

            return true;
        };

        // Pause one buffer at a time.
        for (uint32_t bufferIndex = 0; bufferIndex != m_bufferCount; bufferIndex += 1)
        {
            EnumeratorBegin(bufferIndex);

            while (EnumeratorMoveNext(bufferIndex, recordFn))
            {
                if (m_enumEventInfo.event_desc)
                {
                    error = output.AddTracepointEventDesc(*m_enumEventInfo.event_desc);
                    if (error != EEXIST && error != 0)
                    {
                        EnumeratorEnd(bufferIndex);
                        goto Done;
                    }
                }

                if (vecList.RoomLeft() < 2) // Next recordFn may need up to 2 calls to Add().
                {
                    error = vecList.Flush(output);
                    if (error != 0)
                    {
                        EnumeratorEnd(bufferIndex);
                        goto Done;
                    }
                }
            }

            error = vecList.Flush(output);

            EnumeratorEnd(bufferIndex);

            if (error != 0)
            {
                goto Done;
            }
        }
    }

    // Write system information headers:

    utsname uts;
    if (0 == uname(&uts))
    {
        // HOSTNAME, OSRELEASE, ARCH
        error = output.SetUtsNameHeaders(uts);
        if (error != 0)
        {
            goto Done;
        }
    }

    {
        auto const conf = sysconf(_SC_NPROCESSORS_CONF);
        auto const onln = sysconf(_SC_NPROCESSORS_ONLN);
        if (conf > 0 && onln > 0)
        {
            // NRCPUS
            error = output.SetNrCpusHeader(static_cast<uint32_t>(conf), static_cast<uint32_t>(onln));
            if (error != 0)
            {
                goto Done;
            }
        }
    }

    if (0 != (m_sampleType & PERF_SAMPLE_TIME) &&
        times.first <= times.last)
    {
        // SAMPLE_TIME
        error = output.SetSampleTimeHeader(times.first, times.last);
        if (error != 0)
        {
            goto Done;
        }

        if (options.m_timestampWrittenRangeFirst)
        {
            *options.m_timestampWrittenRangeFirst = times.first;
        }

        if (options.m_timestampWrittenRangeLast)
        {
            *options.m_timestampWrittenRangeLast = times.last;
        }
    }

    // CLOCKID, CLOCK_DATA
    error = output.SetSessionInfoHeaders(m_sessionInfo);
    if (error != 0)
    {
        goto Done;
    }

    // Flush to disk:

    error = output.FinalizeAndClose();

Done:

    return error;
}

_Success_(return == 0) int
TracepointSession::DisableTracepointImpl(PerfEventMetadata const& metadata) noexcept
{
    int error;

    auto const existingIt = m_tracepointInfoByCommonType.find(metadata.Id());
    if (existingIt == m_tracepointInfoByCommonType.end())
    {
        error = ENOENT;
    }
    else
    {
        error = SetTracepointEnableState(existingIt->second, false);
    }

    return error;
}

_Success_(return == 0) int
TracepointSession::EnableTracepointImpl(PerfEventMetadata const& metadata) noexcept
{
    int error;

    auto const existingIt = m_tracepointInfoByCommonType.find(metadata.Id());
    if (existingIt == m_tracepointInfoByCommonType.end())
    {
        error = AddTracepoint(metadata, nullptr, TracepointEnableState::Enabled);
    }
    else
    {
        error = SetTracepointEnableState(existingIt->second, true);
    }

    return error;
}

_Success_(return == 0) int
TracepointSession::IoctlForEachFile(
    _In_reads_(filesCount) unique_fd const* files,
    unsigned filesCount,
    unsigned long request,
    _In_reads_opt_(filesCount) unique_fd const* values) noexcept
{
    int error = 0;

    for (unsigned i = 0; i != filesCount; i += 1)
    {
        errno = 0;
        auto const value = values ? values[i].get() : 0;
        if (-1 == ioctl(files[i].get(), request, value))
        {
            error = errno;
            if (error == 0)
            {
                error = ENODEV;
            }
        }
    }

    return error;
}

bool
TracepointSession::ParseSample(
    uint8_t const* bufferData,
    uint16_t recordSize,
    uint32_t recordBufferPos) noexcept
{
    assert(0 == (recordSize & 7));
    assert(0 == (recordBufferPos & 7));
    assert(recordSize <= m_bufferSize);
    assert(recordBufferPos < m_bufferSize);

    uint8_t const* p;

    if (recordBufferPos + recordSize <= m_bufferSize)
    {
        // Event does not wrap.
        p = bufferData + recordBufferPos;
    }
    else
    {
        // Event wraps. We need to double-buffer it.

        if (m_eventDataBuffer.size() < recordSize)
        {
            try
            {
                m_eventDataBuffer.resize(recordSize);
            }
            catch (...)
            {
                m_lostEventCount += 1;
                return false; // out of memory
            }
        }

        auto const afterWrap = recordBufferPos + recordSize - m_bufferSize;
        auto const beforeWrap = m_bufferSize - recordBufferPos;
        auto const buffer = m_eventDataBuffer.data();
        memcpy(buffer, bufferData + recordBufferPos, beforeWrap);
        memcpy(buffer + beforeWrap, bufferData, afterWrap);
        p = buffer;
    }

    auto const pEnd = p + recordSize;
    auto const infoSampleTypes = m_sampleType;
    uint64_t infoId = 0;
    PerfEventDesc const* infoEventDesc;
    char const* infoRawData = nullptr;
    uint32_t infoRawDataSize = 0;

    perf_event_header const* const infoHeader = reinterpret_cast<perf_event_header const*>(p);
    p += sizeof(perf_event_header);

    auto const SampleTypeSupported = 0u
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
        | PERF_SAMPLE_RAW;
    static_assert(
        SampleTypeSupported == TracepointSessionOptions::SampleTypeSupported,
        "SampleTypeSupported out of sync");

    auto const SampleTypeDefault = 0u
        | PERF_SAMPLE_IDENTIFIER
        | PERF_SAMPLE_TID
        | PERF_SAMPLE_TIME
        | PERF_SAMPLE_CPU
        | PERF_SAMPLE_RAW;
    static_assert(
        SampleTypeDefault == TracepointSessionOptions::SampleTypeDefault,
        "SampleTypeDefault out of sync");

    // Fast path for default sample type.
    if (infoSampleTypes == SampleTypeDefault)
    {
        if (recordSize <
            sizeof(perf_event_header) +
            sizeof(uint64_t) + // PERF_SAMPLE_IDENTIFIER
            sizeof(uint64_t) + // PERF_SAMPLE_TID
            sizeof(uint64_t) + // PERF_SAMPLE_TIME
            sizeof(uint64_t) + // PERF_SAMPLE_CPU
            sizeof(uint64_t))  // PERF_SAMPLE_RAW
        {
            goto Error;
        }

        // PERF_SAMPLE_IDENTIFIER
        infoId = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);

        // PERF_SAMPLE_TID
        auto const pTid = reinterpret_cast<uint32_t const*>(p);
        m_enumEventInfo.pid = pTid[0];
        m_enumEventInfo.tid = pTid[1];
        p += sizeof(uint64_t);

        // PERF_SAMPLE_TIME
        m_enumEventInfo.time = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);

        // PERF_SAMPLE_CPU
        auto const pCpu = reinterpret_cast<uint32_t const*>(p);
        m_enumEventInfo.cpu = pCpu[0];
        m_enumEventInfo.cpu_reserved = pCpu[1];
        p += sizeof(uint64_t);

        // PERF_SAMPLE_RAW
        goto PerfSampleRaw;
    }

    if (infoSampleTypes & PERF_SAMPLE_IDENTIFIER)
    {
        if (p == pEnd) goto Error;
        infoId = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_IP)
    {
        if (p == pEnd) goto Error;
        m_enumEventInfo.ip = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_TID)
    {
        if (p == pEnd) goto Error;
        auto const pTid = reinterpret_cast<uint32_t const*>(p);
        m_enumEventInfo.pid = pTid[0];
        m_enumEventInfo.tid = pTid[1];
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_TIME)
    {
        if (p == pEnd) goto Error;
        m_enumEventInfo.time = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_ADDR)
    {
        if (p == pEnd) goto Error;
        m_enumEventInfo.addr = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_ID)
    {
        if (p == pEnd) goto Error;
        infoId = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_STREAM_ID)
    {
        if (p == pEnd) goto Error;
        m_enumEventInfo.stream_id = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_CPU)
    {
        if (p == pEnd) goto Error;
        auto const pCpu = reinterpret_cast<uint32_t const*>(p);
        m_enumEventInfo.cpu = pCpu[0];
        m_enumEventInfo.cpu_reserved = pCpu[1];
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_PERIOD)
    {
        if (p == pEnd) goto Error;
        m_enumEventInfo.period = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_CALLCHAIN)
    {
        if (p == pEnd) goto Error;
        auto const infoCallchain = reinterpret_cast<uint64_t const*>(p);
        m_enumEventInfo.callchain = infoCallchain;
        auto const count = *infoCallchain;
        p += sizeof(uint64_t);

        if ((pEnd - p) / sizeof(uint64_t) < count)
        {
            goto Error;
        }
        p += count * sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_RAW)
    {
        if (p == pEnd) goto Error;

    PerfSampleRaw:

        assert(p < pEnd);

        auto const* pRaw = reinterpret_cast<uint32_t const*>(p);
        infoRawDataSize = pRaw[0];
        infoRawData = reinterpret_cast<char const*>(pRaw + 1);
        if ((pEnd - p) - sizeof(uint32_t) < infoRawDataSize)
        {
            goto Error;
        }

        assert(p + sizeof(uint32_t) + infoRawDataSize <= pEnd);

        // Try to look up eventDesc by common type field:

        decltype(m_tracepointInfoByCommonType.end()) infoIt;
        auto const commonTypeOffset = static_cast<uint32_t>(m_cache.CommonTypeOffset());
        auto const commonTypeSize = m_cache.CommonTypeSize();
        if (infoRawDataSize <= commonTypeOffset ||
            infoRawDataSize - commonTypeOffset <= commonTypeSize)
        {
            infoIt = m_tracepointInfoByCommonType.end();
        }
        else if (commonTypeSize == sizeof(uint16_t))
        {
            uint16_t commonType;
            memcpy(&commonType, infoRawData + commonTypeOffset, sizeof(commonType));
            infoIt = m_tracepointInfoByCommonType.find(commonType);
        }
        else if (commonTypeSize == sizeof(uint32_t))
        {
            uint32_t commonType;
            memcpy(&commonType, infoRawData + commonTypeOffset, sizeof(commonType));
            infoIt = m_tracepointInfoByCommonType.find(commonType);
        }
        else
        {
            assert(commonTypeSize == 1);
            uint8_t commonType;
            memcpy(&commonType, infoRawData + commonTypeOffset, sizeof(commonType));
            infoIt = m_tracepointInfoByCommonType.find(commonType);
        }

        if (infoIt != m_tracepointInfoByCommonType.end())
        {
            infoEventDesc = &infoIt->second.m_eventDesc;
            goto Done;
        }
    }
    else
    {
        assert(p <= pEnd);
    }

    if (infoSampleTypes & (PERF_SAMPLE_IDENTIFIER | PERF_SAMPLE_ID))
    {
        // Try to look up eventDesc by sample id:

        auto infoIt = m_tracepointInfoBySampleId.find(infoId);
        if (infoIt != m_tracepointInfoBySampleId.end())
        {
            infoEventDesc = &infoIt->second->m_eventDesc;
            goto Done;
        }
    }

    // Unable to locate eventDesc.
    goto Error;

Done:

    m_enumEventInfo.event_desc = infoEventDesc;
    m_enumEventInfo.session_info = &m_sessionInfo;
    m_enumEventInfo.header = infoHeader;
    m_enumEventInfo.id = infoId;
    m_enumEventInfo.raw_data = infoRawData;
    m_enumEventInfo.raw_data_size = infoRawDataSize;

    m_sampleEventCount += 1;
    return true;

Error:

    assert(p <= pEnd);

    m_enumEventInfo.event_desc = {};
    m_enumEventInfo.session_info = {};
    m_enumEventInfo.header = {};
    m_enumEventInfo.id = {};
    m_enumEventInfo.raw_data = {};
    m_enumEventInfo.raw_data_size = {};

    m_corruptEventCount += 1;
    return false;
}

void
TracepointSession::EnumeratorEnd(uint32_t bufferIndex) const noexcept
{
    auto const& buffer = m_buffers[bufferIndex];
    if (!IsRealtime())
    {
        // Should not change while collection paused.
        assert(buffer.DataHead64 == __atomic_load_n(
            &static_cast<perf_event_mmap_page const*>(buffer.Mmap.get())->data_head,
            __ATOMIC_RELAXED));

        int error = ioctl(m_bufferLeaderFiles[bufferIndex].get(), PERF_EVENT_IOC_PAUSE_OUTPUT, 0);
        if (error != 0)
        {
            DEBUG_PRINTF("CPU%u unpause error %u\n", bufferIndex, error);
        }
    }
    else if (buffer.DataPos != buffer.DataTail)
    {
        // Create a new 64-bit tail value.
        uint64_t newTail64;
        static_assert(sizeof(buffer.DataPos) == 8 || sizeof(buffer.DataPos) == 4);
        if constexpr (sizeof(buffer.DataPos) == 8)
        {
            newTail64 = buffer.DataPos;
        }
        else
        {
            // Convert m_bufferDataPos to a 64-bit value relative to m_bufferDataHead64.
            // Order of operations needs to be careful about 64-bit wrapping, e.g.
            // - DataHead64 = 0x600000000
            // - DataHead32 = 0x000000000
            // - DataPos32  = 0x0FFFFFFF8
            // Correct newTail64 is 0x5FFFFFFF8, not 0x6FFFFFFF8
            newTail64 = buffer.DataHead64 - (static_cast<size_t>(buffer.DataHead64) - buffer.DataPos);
        }

        assert(buffer.DataHead64 - newTail64 <= m_bufferSize);

        auto const bufferHeader = static_cast<perf_event_mmap_page*>(buffer.Mmap.get());

        // ATOMIC_RELEASE: perf_events.h recommends smp_mb() here.
        // For future consideration: Ordered enumerator could probably merge barriers.
        // For future consideration: This probably just needs a compiler barrier.
        __atomic_store_n(&bufferHeader->data_tail, newTail64, __ATOMIC_RELEASE);
    }
}

void
TracepointSession::EnumeratorBegin(uint32_t bufferIndex) noexcept
{
    auto const realtime = IsRealtime();
    if (!realtime)
    {
        int error = ioctl(m_bufferLeaderFiles[bufferIndex].get(), PERF_EVENT_IOC_PAUSE_OUTPUT, 1);
        if (error != 0)
        {
            DEBUG_PRINTF("CPU%u pause error %u\n", bufferIndex, error);
        }
    }

    auto& buffer = m_buffers[bufferIndex];
    auto const bufferHeader = static_cast<perf_event_mmap_page const*>(buffer.Mmap.get());

    // ATOMIC_ACQUIRE: perf_events.h recommends smp_rmb() here.
    // For future consideration: Ordered enumerator could probably merge barriers.
    buffer.DataHead64 = __atomic_load_n(&bufferHeader->data_head, __ATOMIC_ACQUIRE);

    if (0 != (buffer.DataHead64 & 7) ||
        m_pageSize != bufferHeader->data_offset ||
        m_bufferSize != bufferHeader->data_size)
    {
        // Unexpected - corrupt trace buffer.
        DEBUG_PRINTF("CPU%u bad perf_event_mmap_page: head=%llx offset=%lx size=%lx\n",
            bufferIndex,
            (unsigned long long)buffer.DataHead64,
            (unsigned long)bufferHeader->data_offset,
            (unsigned long)bufferHeader->data_size);
        buffer.DataTail = static_cast<size_t>(buffer.DataHead64) - m_bufferSize;
        buffer.DataPos = static_cast<size_t>(buffer.DataHead64);
        m_corruptBufferCount += 1;
    }
    else if (!realtime)
    {
        // Circular: write_backward == 1
        buffer.DataTail = static_cast<size_t>(buffer.DataHead64) - m_bufferSize;
        buffer.DataPos = buffer.DataTail;
    }
    else
    {
        // Realtime: write_backward == 0
        auto const bufferDataTail64 = bufferHeader->data_tail;
        buffer.DataTail = static_cast<size_t>(bufferDataTail64);
        if (buffer.DataHead64 - bufferDataTail64 > m_bufferSize)
        {
            // Unexpected - assume bad tail pointer.
            DEBUG_PRINTF("CPU%u bad data_tail: head=%llx tail=%llx\n",
                bufferIndex,
                (unsigned long long)buffer.DataHead64,
                (unsigned long long)bufferDataTail64);
            buffer.DataTail = static_cast<size_t>(buffer.DataHead64) - m_bufferSize; // Ensure tail gets updated.
            buffer.DataPos = static_cast<size_t>(buffer.DataHead64);
            m_corruptBufferCount += 1;
        }
        else
        {
            buffer.DataPos = buffer.DataTail;
        }
    }
}

template<class RecordFn>
bool
TracepointSession::EnumeratorMoveNext(
    uint32_t bufferIndex,
    RecordFn&& recordFn) noexcept(noexcept(recordFn(nullptr, 0, 0)))
{
    auto& buffer = m_buffers[bufferIndex];
    auto const bufferData = static_cast<uint8_t const*>(buffer.Mmap.get()) + m_pageSize;
    for (;;)
    {
        auto const remaining = static_cast<size_t>(buffer.DataHead64) - buffer.DataPos;
        if (remaining == 0)
        {
            break;
        }

        auto const eventHeaderBufferPos = buffer.DataPos & (m_bufferSize - 1);
        auto const eventHeader = *BufferDataPosToHeader(bufferData, eventHeaderBufferPos);

        if (eventHeader.size == 0 ||
            eventHeader.size > remaining)
        {
            // - Circular: this is probably not a real problem - it's probably
            //   unused buffer space or a partially-overwritten event.
            // - Realtime: The buffer is corrupt.
            m_corruptBufferCount += IsRealtime();

            // In either case, buffer is done. Mark the buffer's events as consumed.
            buffer.DataPos = static_cast<size_t>(buffer.DataHead64);
            break;
        }

        if (0 != (eventHeader.size & 7))
        {
            // Unexpected - corrupt event header.
            DEBUG_PRINTF("CPU%u unaligned eventHeader.Size at pos %lx: %u\n",
                bufferIndex, (unsigned long)buffer.DataPos, eventHeader.size);

            // The event is corrupt, can't parse beyond it. Mark the buffer's events as consumed.
            m_corruptBufferCount += 1;
            buffer.DataPos = static_cast<size_t>(buffer.DataHead64);
            break;
        }

        buffer.DataPos += eventHeader.size;

        if (eventHeader.type == PERF_RECORD_LOST)
        {
            auto const newEventsLost64 = *reinterpret_cast<uint64_t const*>(
                bufferData + ((eventHeaderBufferPos + sizeof(perf_event_header) + sizeof(uint64_t)) & (m_bufferSize - 1)));
            m_lostEventCount += newEventsLost64;
        }

        if (recordFn(bufferData, eventHeader.size, eventHeaderBufferPos))
        {
            return true;
        }
    }

    return false;
}

_Success_(return == 0) int
TracepointSession::SetTracepointEnableState(
    TracepointInfoImpl & tpi,
    bool enabled) noexcept
{
    int error;

    static auto const UnknownState = TracepointEnableState::Unknown;
    auto const desiredState = enabled
        ? TracepointEnableState::Enabled
        : TracepointEnableState::Disabled;

    if (desiredState == tpi.m_enableState)
    {
        error = 0;
        goto Done;
    }

    assert(m_restoreInfoFile);
    assert(m_restoreInfoFilePos > tpi.m_restoreInfoFileEnableStateOffset);
    assert(tpi.m_restoreInfoFileEnableStateOffset != 0);

    if (1 != pwrite(m_restoreInfoFile.get(), &UnknownState, sizeof(TracepointEnableState), tpi.m_restoreInfoFileEnableStateOffset))
    {
        error = errno;
        goto Done;
    }

    tpi.m_enableState = UnknownState;

    error = IoctlForEachFile(
        tpi.m_bufferFiles.get(),
        tpi.m_bufferFilesCount,
        enabled ? PERF_EVENT_IOC_ENABLE : PERF_EVENT_IOC_DISABLE,
        nullptr);
    if (error == 0)
    {
        tpi.m_enableState = desiredState;

        // Best-effort.
        pwrite(m_restoreInfoFile.get(), &desiredState, sizeof(TracepointEnableState), tpi.m_restoreInfoFileEnableStateOffset);
    }

Done:

    return error;
}

void
TracepointSession::InvokeSaveToFdsCallbackForExistingFds() const noexcept
{
    if (m_saveToFdsCallback != nullptr)
    {
        assert(m_restoreFds.size() <= RestoreFdsMax);
        for (uint16_t restoreFdsIndex = 0; restoreFdsIndex != m_restoreFds.size(); restoreFdsIndex += 1)
        {
            InvokeSaveToFdsCallback(restoreFdsIndex);
        }
    }
}

void
TracepointSession::InvokeSaveToFdsCallback(uint16_t restoreFdsIndex) const noexcept
{
    assert(m_saveToFdsCallback != nullptr);
    assert(restoreFdsIndex < m_restoreFds.size());

    char name[sizeof(m_saveToFdsNamePrefix) + sizeof("/12345678")];
    snprintf(name, sizeof(name), "%s%c%X",
        m_saveToFdsNamePrefix, FdNameSeparator, restoreFdsIndex);
    m_saveToFdsCallback(m_saveToFdsCallbackContext, name, m_restoreFds[restoreFdsIndex]);
}

_Success_(return == 0) int
TracepointSession::AddTracepoint(
    PerfEventMetadata const& metadata,
    std::unique_ptr<unique_fd[]> existingFiles,
    TracepointEnableState enableState) noexcept(false)
{
    int error;
    uint32_t cIdsAdded = 0;
    uint64_t* pIds = nullptr;

    try
    {
        auto const systemName = metadata.SystemName();
        auto const eventName = metadata.Name();
        if (systemName.size() > 65535 ||
            eventName.size() > 65535)
        {
            error = E2BIG;
            goto Error;
        }

        auto const cbEventDescStorage =
            sizeof(perf_event_attr) +
            m_bufferCount * sizeof(uint64_t) +
            systemName.size() + 1 + eventName.size() + 1;
        auto eventDescStorage = std::make_unique<char unsigned[]>(cbEventDescStorage);

        auto const pAttr = reinterpret_cast<perf_event_attr*>(eventDescStorage.get());
        pAttr->type = PERF_TYPE_TRACEPOINT;
        pAttr->size = sizeof(perf_event_attr);
        pAttr->config = metadata.Id();
        pAttr->sample_period = 1;
        pAttr->sample_type = m_sampleType;
        pAttr->read_format = PERF_FORMAT_ID; // Must match definition of struct ReadFormat.
        pAttr->watermark = m_wakeupUseWatermark;
        pAttr->use_clockid = 1;
        pAttr->write_backward = !IsRealtime();
        pAttr->wakeup_events = m_wakeupValue;
        pAttr->clockid = m_sessionInfo.Clockid();

        // pIds will be initialized after file handle creation.
        // cIdsAdded tracks initialization.
        pIds = reinterpret_cast<uint64_t*>(pAttr + 1);

        auto const pName = reinterpret_cast<char*>(pIds + m_bufferCount);
        {
            size_t i = 0;
            memcpy(&pName[i], systemName.data(), systemName.size());
            i += systemName.size();
            pName[i] = ':';
            i += 1;
            memcpy(&pName[i], eventName.data(), eventName.size());
            i += eventName.size();
            pName[i] = '\0';
        }

        PerfEventDesc const eventDesc = {
            pAttr,
            pName,
            &metadata,
            pIds,
            m_bufferCount
        };

        auto const existingFilesProvided = existingFiles != nullptr;
        auto er = m_tracepointInfoByCommonType.try_emplace(metadata.Id(),
            eventDesc,
            std::move(eventDescStorage),
            existingFilesProvided ? std::move(existingFiles) : std::make_unique<unique_fd[]>(m_bufferCount),
            m_bufferCount,
            m_restoreInfoFilePos);
        assert(er.second);
        auto& tpi = er.first->second;
        tpi.m_enableState = enableState;

        // Starting from here, if there is an error then we must erase(metadata.Id).

        if (!existingFilesProvided)
        {
            for (uint32_t bufferIndex = 0; bufferIndex != m_bufferCount; bufferIndex += 1)
            {
                errno = 0;
                tpi.m_bufferFiles[bufferIndex].reset(perf_event_open(pAttr, -1, bufferIndex, -1, PERF_FLAG_FD_CLOEXEC));
                if (!tpi.m_bufferFiles[bufferIndex])
                {
                    error = errno;
                    if (error == 0)
                    {
                        error = ENODEV;
                    }

                    goto Error;
                }
            }

            if (m_bufferLeaderFiles)
            {
                // Leader already exists. Add this event to the leader's mmaps.
                error = IoctlForEachFile(tpi.m_bufferFiles.get(), m_bufferCount, PERF_EVENT_IOC_SET_OUTPUT, m_bufferLeaderFiles);
                if (error)
                {
                    goto Error;
                }
            }
        }

        if (m_bufferLeaderFiles == nullptr)
        {
            // This is the first event. Make it the "leader" (the owner of the session buffers).
            auto const mmapSize = m_pageSize + m_bufferSize;
            auto const prot = IsRealtime()
                ? PROT_READ | PROT_WRITE
                : PROT_READ;
            for (uint32_t bufferIndex = 0; bufferIndex != m_bufferCount; bufferIndex += 1)
            {
                errno = 0;
                auto cpuMap = mmap(nullptr, mmapSize, prot, MAP_SHARED, tpi.m_bufferFiles[bufferIndex].get(), 0);
                if (MAP_FAILED == cpuMap)
                {
                    error = errno;

                    // Clean up any mmaps that we opened.
                    for (uint32_t bufferIndex2 = 0; bufferIndex2 != bufferIndex; bufferIndex2 += 1)
                    {
                        m_buffers[bufferIndex2].Mmap.reset();
                    }

                    if (error == 0)
                    {
                        error = ENODEV;
                    }

                    goto Error;
                }

                m_buffers[bufferIndex].Mmap.reset(cpuMap, mmapSize);
            }
        }

        // Find the sample_ids for the new tracepoints.
        for (; cIdsAdded != m_bufferCount; cIdsAdded += 1)
        {
            ReadFormat data;
            error = tpi.Read(cIdsAdded, &data);
            if (error != 0)
            {
                goto Error;
            }

            pIds[cIdsAdded] = data.id;

            auto const added = m_tracepointInfoBySampleId.emplace(data.id, &tpi).second;
            assert(added);
            (void)added;
        }

        // Set up the restore info for this tracepoint.

        if (!m_restoreInfoFile)
        {
            assert(m_restoreInfoFilePos == sizeof(RestoreHeader));
            assert(m_restoreFds.empty());
            m_restoreFds.clear();
            m_restoreFds.reserve(1 + m_bufferCount); // May throw.

            unique_fd restoreInfoFile(memfd_create("TracepointSessionRestoreInfo", MFD_CLOEXEC));
            if (!restoreInfoFile)
            {
                error = errno;
                goto Error;
            }

            RestoreHeader const restoreHeader(*this);
            auto const written = write(restoreInfoFile.get(), &restoreHeader, sizeof(restoreHeader));
            if (written != sizeof(restoreHeader))
            {
                error = written < 0 ? errno : EIO;
                goto Error;
            }

            // Success. Commit it.

            m_restoreInfoFile = std::move(restoreInfoFile);
            m_restoreInfoFilePos = sizeof(restoreHeader);
            m_restoreFds.push_back(m_restoreInfoFile.get()); // Won't throw (reserved above).
            if (m_saveToFdsCallback != nullptr)
            {
                InvokeSaveToFdsCallback(m_restoreFds.size() - 1);
            }
        }

        if (m_restoreFds.size() + m_bufferCount > RestoreFdsMax)
        {
            error = E2BIG;
            goto Error;
        }

        m_restoreFds.reserve(m_restoreFds.size() + m_bufferCount); // May throw.

        auto const nameSize = static_cast<uint32_t>(systemName.size() + 1 + eventName.size());

        std::vector<char> buffer;
        buffer.reserve(sizeof(enableState) + sizeof(nameSize) + nameSize); // May throw.

        AppendValue<TracepointEnableState>(&buffer, enableState);
        AppendValue<uint32_t>(&buffer, nameSize);
        buffer.insert(buffer.end(), systemName.data(), systemName.data() + systemName.size());
        buffer.push_back(':');
        buffer.insert(buffer.end(), eventName.data(), eventName.data() + eventName.size());

        auto const written = write(m_restoreInfoFile.get(), buffer.data(), buffer.size());
        if (written != static_cast<ssize_t>(buffer.size()))
        {
            if (written < 0)
            {
                error = errno;
            }
            else
            {
                if (written > 0)
                {
                    // Try to undo (best-effort).
                    lseek64(m_restoreInfoFile.get(), m_restoreInfoFilePos, SEEK_SET);
                    ftruncate64(m_restoreInfoFile.get(), m_restoreInfoFilePos);
                }

                error = EIO;
            }

            goto Error;
        }

        m_restoreInfoFilePos += static_cast<uint32_t>(buffer.size());

        // Success. Commit it. (No exceptions beyond this point.)

        for (uint32_t bufferIndex = 0; bufferIndex != m_bufferCount; bufferIndex += 1)
        {
            m_restoreFds.push_back(tpi.m_bufferFiles[bufferIndex].get()); // Won't throw (reserved above).
            if (m_saveToFdsCallback != nullptr)
            {
                InvokeSaveToFdsCallback(m_restoreFds.size() - 1);
            }
        }

        if (!m_bufferLeaderFiles)
        {
            m_bufferLeaderFiles = tpi.m_bufferFiles.get(); // Commit this event as the leader.
        }

        goto Done;
    }
    catch (...)
    {
        error = ENOMEM;
        goto Error;
    }

Error:

    for (uint32_t i = 0; i != cIdsAdded; i += 1)
    {
        m_tracepointInfoBySampleId.erase(pIds[i]);
    }

    // May or may not have been added yet. If not, erase does nothing.
    m_tracepointInfoByCommonType.erase(metadata.Id());

Done:

    return error;
}

template<class FdList>
_Success_(return == 0) int
TracepointSession::RestoreFromFdsImpl(
    _In_z_ char const* namePrefix,
    unsigned count,
    FdList fdList,
    _Inout_updates_(count) char const** names) noexcept
{
    assert(strlen(namePrefix) < sizeof(m_saveToFdsNamePrefix));

    int error;

    struct FdImport
    {
        /*
        ListIndex == UINT32_MAX : We own Fd (if any), name has been freed.
        ListIndex != UINT32_MAX : Caller owns Fd and name.
        */

        int Fd = -1;
        unsigned ListIndex = UINT32_MAX;
    };

    std::vector<FdImport> fdImports;

    char namePrefixBuf[sizeof(m_saveToFdsNamePrefix)];
    auto const namePrefixLen = strnlen(namePrefix, sizeof(namePrefixBuf) - 1);
    memcpy(namePrefixBuf, namePrefix, namePrefixLen);
    namePrefixBuf[namePrefixLen] = FdNameSeparator;

    if (m_bufferLeaderFiles)
    {
        error = EPERM;
        goto Done;
    }

    assert(m_restoreFds.size() <= 1); // Should be empty or just the restore info file.

    try
    {
        std::vector<char> restoreInfo;

        for (unsigned listIndex = 0; listIndex != count; listIndex += 1)
        {
            auto const fd = fdList.Get(listIndex);
            if (fd < 0)
            {
                continue; // Invalid FD.
            }

            auto const name = names[listIndex];
            if (name == nullptr)
            {
                continue; // Invalid name.
            }

            if (0 != strncmp(name, namePrefixBuf, namePrefixLen + 1))
            {
                continue; // Somebody else's name/FD.
            }

            auto const indexBegin = name + namePrefixLen + 1;
            char* indexEnd = nullptr;
            auto fdImportsIndex = strtoul(indexBegin, &indexEnd, 16);
            if (indexEnd == indexBegin || indexEnd[0] != '\0' || fdImportsIndex > 65535)
            {
                continue; // Expected name = "namePrefix/fdImportsIndex".
            }

            if (fdImportsIndex >= fdImports.size())
            {
                fdImports.resize(fdImportsIndex + 1);
            }
            else if (fdImports[fdImportsIndex].Fd >= 0)
            {
                error = EILSEQ; // Duplicate fdImportsIndex.
                goto Done;
            }

            fdImports[fdImportsIndex] = { fd, listIndex };
        }

        if (fdImports.empty() || fdImports[0].Fd < 0)
        {
            error = EILSEQ; // No restoreInfo file.
            goto Done;
        }

        auto const restoreInfoFileEnd = lseek64(fdImports[0].Fd, 0, SEEK_END);
        if (restoreInfoFileEnd < static_cast<int>(sizeof(RestoreHeader)) ||
            restoreInfoFileEnd >= UINT32_MAX)
        {
            error = EILSEQ; // Invalid restoreInfo file.
            goto Done;
        }

        restoreInfo.resize(static_cast<uint32_t>(restoreInfoFileEnd));

        auto const restoreInfoFileRead = pread(fdImports[0].Fd, restoreInfo.data(), restoreInfo.size(), 0);
        if (static_cast<size_t>(restoreInfoFileRead) != restoreInfo.size())
        {
            error = EILSEQ; // Failed to read restoreInfo file.
            goto Done;
        }

        size_t restoreInfoPos = 0;
        if (RestoreHeader(*this) != RestoreHeader(restoreInfo.data() + restoreInfoPos))
        {
            error = EMEDIUMTYPE; // Configuration mismatch.
            goto Done;
        }

        restoreInfoPos += sizeof(RestoreHeader);

        // Looks good enough for us to begin restore.
        // Best-effort, might still complete with nonzero error.
        // No "goto Done" after this point.

        error = 0;

        // Take ownership of names and fds that we will be using.
        for (auto& fdImport : fdImports)
        {
            auto const listIndex = fdImport.ListIndex;
            if (listIndex != UINT32_MAX)
            {
                // Caller is no longer responsible for FD.
                // It will either move into new session or be closed on exit.
                fdList.Clear(listIndex);

                // Caller is no longer responsible for name.
                // Free it now.
                free(const_cast<char*>(names[listIndex]));
                names[listIndex] = nullptr;

                fdImport.ListIndex = UINT32_MAX;
            }
        }

        // We're done with the restore info file.
        close(fdImports[0].Fd);
        fdImports[0].Fd = -1;

        std::unique_ptr<unique_fd[]> existingFiles;

        for (unsigned fdImportsIndex = 1;
            fdImportsIndex + m_bufferCount <= fdImports.size();
            fdImportsIndex += m_bufferCount)
        {
            auto const leader = fdImportsIndex == 1;

            if (!existingFiles)
            {
                existingFiles = std::make_unique<unique_fd[]>(m_bufferCount);
            }

            bool allPresent = true;
            for (unsigned i = 0; i != m_bufferCount; i += 1)
            {
                existingFiles[i].reset(fdImports[fdImportsIndex + i].Fd);
                fdImports[fdImportsIndex + i].Fd = -1;

                if (!existingFiles[i])
                {
                    allPresent = false;
                    break;
                }
            }

            if (!allPresent)
            {
                // Unexpected: Event incompletely stored.
                if (error == 0)
                {
                    error = ENOENT;
                }

                if (leader)
                {
                    break; // Cannot proceed.
                }

                continue;
            }

            assert(restoreInfoPos <= restoreInfo.size());

            TracepointEnableState enableState;
            uint32_t fullNameSize;
            if (restoreInfo.size() - restoreInfoPos < sizeof(enableState) + sizeof(fullNameSize))
            {
                // Unexpected: invalid metadata.
                if (error == 0)
                {
                    error = ENOENT;
                }

                break; // Cannot proceed.
            }

            memcpy(&enableState, restoreInfo.data() + restoreInfoPos, sizeof(enableState));
            restoreInfoPos += sizeof(enableState);

            memcpy(&fullNameSize, restoreInfo.data() + restoreInfoPos, sizeof(fullNameSize));
            restoreInfoPos += sizeof(fullNameSize);
            if (restoreInfo.size() - restoreInfoPos < fullNameSize)
            {
                // Unexpected: invalid metadata.
                if (error == 0)
                {
                    error = ENOENT;
                }

                break; // Cannot proceed.
            }

            std::string_view const fullName(restoreInfo.data() + restoreInfoPos, fullNameSize);
            restoreInfoPos += fullNameSize;

            if (enableState == TracepointEnableState::Disabled && !leader)
            {
                // Don't restore disabled events.
                continue;
            }

            auto const colonPos = fullName.find(':');
            if (colonPos == std::string_view::npos)
            {
                // Unexpected: invalid name.
                if (error == 0)
                {
                    error = ENOENT;
                }

                if (leader)
                {
                    break; // Cannot proceed.
                }

                continue;
            }

            PerfEventMetadata const* metadata = nullptr;
            auto const metadataError = m_cache.FindOrAddFromSystem(
                TracepointName(fullName.substr(0, colonPos), fullName.substr(colonPos + 1)),
                &metadata);
            if (metadata == nullptr)
            {
                // Unexpected: metadata not found.
                if (error == 0)
                {
                    error = metadataError;
                }

                if (leader)
                {
                    break; // Cannot proceed.
                }

                continue;
            }

            auto const addError = AddTracepoint(*metadata, std::move(existingFiles), enableState);
            if (addError != 0)
            {
                // Unexpected: Event failed to add.
                if (error == 0)
                {
                    error = addError;
                }

                if (leader)
                {
                    break; // Cannot proceed.
                }

                continue;
            }
        }
    }
    catch (...)
    {
        error = ENOMEM;
    }

    for (auto& fdImport : fdImports)
    {
        if (fdImport.Fd >= 0 && fdImport.ListIndex == UINT32_MAX)
        {
            close(fdImport.Fd);
        }
    }

    InvokeSaveToFdsCallbackForExistingFds();

Done:

    return error;
}
