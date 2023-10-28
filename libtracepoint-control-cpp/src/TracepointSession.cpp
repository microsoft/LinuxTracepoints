// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <tracepoint/TracepointSession.h>
#include <algorithm>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <asm/unistd.h> // __NR_perf_event_open
#include <linux/perf_event.h>

#ifdef NDEBUG
#define DEBUG_PRINTF(...) ((void)0)
#else // NDEBUG
#include <stdio.h>
#define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#endif // NDEBUG

using namespace std::string_view_literals;
using namespace tracepoint_control;
using namespace tracepoint_decode;

// Helpers

static bool
NonSampleFnReturnFalse(
    [[maybe_unused]] uint8_t const* bufferData,
    [[maybe_unused]] uint16_t sampleDataSize,
    [[maybe_unused]] uint32_t sampleDataBufferPos,
    [[maybe_unused]] perf_event_header eventHeader) noexcept
{
    return false;
}

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

// TracepointInfoImpl

TracepointSession::TracepointInfoImpl::~TracepointInfoImpl()
{
    return;
}

TracepointSession::TracepointInfoImpl::TracepointInfoImpl(
    tracepoint_decode::PerfEventDesc const& eventDesc,
    std::unique_ptr<char unsigned[]> eventDescStorage,
    std::unique_ptr<unique_fd[]> bufferFiles,
    unsigned bufferFilesCount
    ) noexcept
    : m_eventDesc(eventDesc)
    , m_eventDescStorage(std::move(eventDescStorage))
    , m_bufferFiles(std::move(bufferFiles))
    , m_bufferFilesCount(bufferFilesCount)
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
        :  EPIPE;
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
    uint16_t dataSize,
    uint32_t dataPos) noexcept
    : Timestamp(timestamp)
    , BufferIndex(bufferIndex)
    , DataSize(dataSize)
    , DataPos(dataPos)
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
            uint16_t sampleDataSize,
            uint32_t sampleDataBufferPos)
        {
            return session.ParseSample(bufferData, sampleDataSize, sampleDataBufferPos);
        },
        NonSampleFnReturnFalse);
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
        unsigned const bytesBeforeTime = sizeof(uint64_t) * (0 +
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
                    uint16_t sampleDataSize,
                    uint32_t sampleDataBufferPos)
                {
                    assert(0 == (sampleDataSize & 7));
                    assert(0 == (sampleDataBufferPos & 7));

                    if (sampleDataSize <= bytesBeforeTime)
                    {
                        session.m_corruptEventCount += 1;
                        return false;
                    }

                    auto const timePos = (sampleDataBufferPos + bytesBeforeTime) & (session.m_bufferSize - 1);
                    auto const timestamp = *reinterpret_cast<uint64_t const*>(bufferData + timePos);
                    session.m_enumeratorBookmarks.emplace_back( // May throw bad_alloc.
                        timestamp,
                        bufferIndex,
                        sampleDataSize,
                        sampleDataBufferPos);
                    return false; // Keep going.
                },
                NonSampleFnReturnFalse);

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
            item.DataSize,
            item.DataPos))
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
    : m_cache(cache)
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
    , m_eventDataBuffer()
    , m_enumeratorBookmarks()
    , m_pollfd(nullptr)
    , m_bufferLeaderFiles(nullptr)
    , m_sampleEventCount(0)
    , m_lostEventCount(0)
    , m_corruptEventCount(0)
    , m_corruptBufferCount(0)
    , m_sessionInfo()
    , m_enumEventInfo()
{
    static const auto Billion = 1000000000u;

    assert(options.m_mode <= TracepointSessionMode::RealTime);
    m_sessionInfo.SetClockid(CLOCK_MONOTONIC_RAW);

    timespec monotonic;
    timespec realtime;
    if (0 == clock_gettime(CLOCK_MONOTONIC_RAW, &monotonic) &&
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

        m_sessionInfo.SetClockData(CLOCK_MONOTONIC_RAW, realtimeNS, monotonicNS);
    }

    return;
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
    m_tracepointInfoByCommonType.clear();
    m_tracepointInfoBySampleId.clear();
    m_bufferLeaderFiles = nullptr;
    for (uint32_t bufferIndex = 0; bufferIndex != m_bufferCount; bufferIndex += 1)
    {
        m_buffers[bufferIndex].Mmap.reset();
    }
}

_Success_(return == 0) int
TracepointSession::DisableTracePoint(unsigned id) noexcept
{
    auto const metadata = m_cache.FindById(id);
    auto const error = metadata == nullptr
        ? ENOENT
        : DisableTracePointImpl(*metadata);

    return error;
}

_Success_(return == 0) int
TracepointSession::DisableTracePoint(TracepointName name) noexcept
{
    int error;

    PerfEventMetadata const* metadata;
    error = m_cache.FindOrAddFromSystem(name, &metadata);
    if (error == 0)
    {
        error = DisableTracePointImpl(*metadata);
    }

    return error;
}

_Success_(return == 0) int
TracepointSession::EnableTracePoint(unsigned id) noexcept
{
    auto const metadata = m_cache.FindById(id);
    auto const error = metadata == nullptr
        ? ENOENT
        : EnableTracePointImpl(*metadata);

    return error;
}

_Success_(return == 0) int
TracepointSession::EnableTracePoint(TracepointName name) noexcept
{
    int error;

    PerfEventMetadata const* metadata;
    error = m_cache.FindOrAddFromSystem(name, &metadata);
    if (error == 0)
    {
        error = EnableTracePointImpl(*metadata);
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
TracepointSession::DisableTracePointImpl(PerfEventMetadata const& metadata) noexcept
{
    int error;

    auto const it = m_tracepointInfoByCommonType.find(metadata.Id());
    if (it == m_tracepointInfoByCommonType.end())
    {
        error = ENOENT;
    }
    else if (it->second.m_enableState == TracepointEnableState::Disabled)
    {
        error = 0;
    }
    else
    {
        error = IoctlForEachFile(it->second.m_bufferFiles.get(), m_bufferCount, PERF_EVENT_IOC_DISABLE, nullptr);
        it->second.m_enableState = error
            ? TracepointEnableState::Unknown
            : TracepointEnableState::Disabled;
    }

    return error;
}

_Success_(return == 0) int
TracepointSession::EnableTracePointImpl(PerfEventMetadata const& metadata) noexcept
{
    int error;
    uint32_t cIdsAdded = 0;
    uint64_t* pIds = nullptr;

    auto existingIt = m_tracepointInfoByCommonType.find(metadata.Id());
    if (existingIt != m_tracepointInfoByCommonType.end())
    {
        // Event already in list. Make sure it's enabled.
        if (existingIt->second.m_enableState == TracepointEnableState::Enabled)
        {
            error = 0;
        }
        else
        {
            auto const& tpi = existingIt->second;
            error = IoctlForEachFile(
                tpi.m_bufferFiles.get(),
                tpi.m_bufferFilesCount,
                PERF_EVENT_IOC_ENABLE,
                nullptr);
            existingIt->second.m_enableState = error
                ? TracepointEnableState::Unknown
                : TracepointEnableState::Enabled;
        }

        // Note: skip cleanup, even for error case.
        goto Done;
    }

    try
    {
        auto const systemName = metadata.SystemName();
        auto const eventName = metadata.Name();
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
        pAttr->clockid = CLOCK_MONOTONIC_RAW;

        // pIds will be initialized after file handle creation.
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

        auto er = m_tracepointInfoByCommonType.try_emplace(metadata.Id(),
            eventDesc,
            std::move(eventDescStorage),
            std::make_unique<unique_fd[]>(m_bufferCount),
            m_bufferCount);
        assert(er.second);
        auto& tpi = er.first->second;

        // Starting from here, if there is an error then we must erase(metadata.Id).

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
        else
        {
            auto const mmapSize = m_pageSize + m_bufferSize;
            auto const prot = IsRealtime()
                ? PROT_READ | PROT_WRITE
                : PROT_READ;

            // This is the first event. Make it the "leader".
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

        if (!m_bufferLeaderFiles)
        {
            m_bufferLeaderFiles = tpi.m_bufferFiles.get(); // Commit this event as the leader.
        }

        tpi.m_enableState = TracepointEnableState::Enabled;
        error = 0;
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
    uint16_t sampleDataSize,
    uint32_t sampleDataBufferPos) noexcept
{
    assert(0 == (sampleDataSize & 7));
    assert(0 == (sampleDataBufferPos & 7));
    assert(sampleDataSize <= m_bufferSize);
    assert(sampleDataBufferPos < m_bufferSize);

    uint8_t const* p;

    auto const unmaskedDataPosEnd = sampleDataBufferPos + sampleDataSize;
    if (unmaskedDataPosEnd <= m_bufferSize)
    {
        // Event does not wrap.
        p = bufferData + sampleDataBufferPos;
    }
    else
    {
        // Event wraps. We need to double-buffer it.

        if (m_eventDataBuffer.size() < sampleDataSize)
        {
            try
            {
                m_eventDataBuffer.resize(sampleDataSize);
            }
            catch (...)
            {
                m_lostEventCount += 1;
                return false; // out of memory
            }
        }

        auto const afterWrap = unmaskedDataPosEnd - m_bufferSize;
        auto const beforeWrap = m_bufferSize - sampleDataBufferPos;
        auto const buffer = m_eventDataBuffer.data();
        memcpy(buffer, bufferData + sampleDataBufferPos, beforeWrap);
        memcpy(buffer + beforeWrap, bufferData, afterWrap);
        p = buffer;
    }

    auto const pEnd = p + sampleDataSize;
    auto const infoSampleTypes = m_sampleType;
    uint64_t infoId = -1;
    PerfEventDesc const* infoEventDesc = nullptr;
    char const* infoRawData = nullptr;
    uint32_t infoRawDataSize = 0;
    auto& enumEventInfo = m_enumEventInfo;

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
        if (sampleDataSize <
            sizeof(uint64_t) + // PERF_SAMPLE_TID
            sizeof(uint64_t) + // PERF_SAMPLE_TIME
            sizeof(uint64_t) + // PERF_SAMPLE_CPU
            sizeof(uint64_t))  // PERF_SAMPLE_RAW
        {
            goto Error;
        }

        // PERF_SAMPLE_TID
        auto const pTid = reinterpret_cast<uint32_t const*>(p);
        enumEventInfo.pid = pTid[0];
        enumEventInfo.tid = pTid[1];
        p += sizeof(uint64_t);

        // PERF_SAMPLE_TIME
        enumEventInfo.time = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);

        // PERF_SAMPLE_CPU
        auto const pCpu = reinterpret_cast<uint32_t const*>(p);
        enumEventInfo.cpu = pCpu[0];
        enumEventInfo.cpu_reserved = pCpu[1];
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
        enumEventInfo.ip = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_TID)
    {
        if (p == pEnd) goto Error;
        auto const pTid = reinterpret_cast<uint32_t const*>(p);
        enumEventInfo.pid = pTid[0];
        enumEventInfo.tid = pTid[1];
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_TIME)
    {
        if (p == pEnd) goto Error;
        enumEventInfo.time = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_ADDR)
    {
        if (p == pEnd) goto Error;
        enumEventInfo.addr = *reinterpret_cast<uint64_t const*>(p);
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
        enumEventInfo.stream_id = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_CPU)
    {
        if (p == pEnd) goto Error;
        auto const pCpu = reinterpret_cast<uint32_t const*>(p);
        enumEventInfo.cpu = pCpu[0];
        enumEventInfo.cpu_reserved = pCpu[1];
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_PERIOD)
    {
        if (p == pEnd) goto Error;
        enumEventInfo.period = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_CALLCHAIN)
    {
        if (p == pEnd) goto Error;
        auto const infoCallchain = reinterpret_cast<uint64_t const*>(p);
        enumEventInfo.callchain = infoCallchain;
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

    enumEventInfo.event_desc = infoEventDesc;
    enumEventInfo.session_info = &m_sessionInfo;
    enumEventInfo.id = infoId;
    enumEventInfo.raw_data = infoRawData;
    enumEventInfo.raw_data_size = infoRawDataSize;

    m_sampleEventCount += 1;
    return true;

Error:

    assert(p <= pEnd);

    enumEventInfo.event_desc = {};
    enumEventInfo.session_info = {};
    enumEventInfo.id = {};
    enumEventInfo.raw_data = {};
    enumEventInfo.raw_data_size = {};

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

template<class SampleFn, class NonSampleFn>
bool
TracepointSession::EnumeratorMoveNext(
    uint32_t bufferIndex,
    SampleFn&& sampleFn,
    NonSampleFn&& nonSampleFn)
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
        auto const eventHeader = *reinterpret_cast<perf_event_header const*>(bufferData + eventHeaderBufferPos);

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

        uint16_t const dataSize = eventHeader.size - sizeof(perf_event_header);
        uint32_t const dataPos = (eventHeaderBufferPos + sizeof(perf_event_header)) & (m_bufferSize - 1);
        if (eventHeader.type == PERF_RECORD_SAMPLE)
        {
            if (sampleFn(bufferData, dataSize, dataPos))
            {
                return true;
            }
        }
        else
        {
            if (eventHeader.type == PERF_RECORD_LOST)
            {
                auto const newEventsLost64 = *reinterpret_cast<uint64_t const*>(
                    bufferData + ((eventHeaderBufferPos + sizeof(perf_event_header) + sizeof(uint64_t)) & (m_bufferSize - 1)));
                m_lostEventCount += newEventsLost64;
            }

            if (nonSampleFn(bufferData, dataSize, dataPos, eventHeader))
            {
                return true;
            }
        }
    }

    return false;
}
