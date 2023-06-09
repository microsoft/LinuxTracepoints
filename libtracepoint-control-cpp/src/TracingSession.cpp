// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <tracepoint/TracingSession.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

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

TracingSession::TracingSession(
    TracingCache& cache,
    TracingSessionOptions const& options) noexcept(false)
    : m_cache(cache)
    , m_mode(options.m_mode)
    , m_wakeupUseWatermark(options.m_wakeupUseWatermark)
    , m_wakeupValue(options.m_wakeupValue)
    , m_sampleType(options.m_sampleType)
    , m_bufferCount(sysconf(_SC_NPROCESSORS_ONLN))
    , m_pageSize(sysconf(_SC_PAGESIZE))
    , m_bufferSize(RoundUpBufferSize(m_pageSize, options.m_bufferSize))
    , m_bufferMmaps(std::make_unique<unique_mmap[]>(m_bufferCount)) // may throw bad_alloc.
    , m_tracepointInfoById() // may throw bad_alloc (but probably doesn't).
    , m_eventDataBuffer()
    , m_pollfd(nullptr)
    , m_bufferLeaderFiles(nullptr)
    , m_sampleEventCount(0)
    , m_lostEventCount(0)
    , m_corruptEventCount(0)
    , m_corruptBufferCount(0)
{
    assert(options.m_mode <= TracingMode::RealTime);
    return;
}

TracingMode
TracingSession::Mode() const noexcept
{
    return m_mode;
}

bool
TracingSession::IsRealtime() const noexcept
{
    return m_mode != TracingMode::Circular;
}

size_t
TracingSession::BufferSize() const noexcept
{
    return m_bufferSize;
}

uint32_t
TracingSession::BufferCount() const noexcept
{
    return m_bufferCount;
}

uint64_t
TracingSession::SampleEventCount() const noexcept
{
    return m_sampleEventCount;
}

uint64_t
TracingSession::LostEventCount() const noexcept
{
    return m_lostEventCount;
}

uint64_t
TracingSession::CorruptEventCount() const noexcept
{
    return m_corruptEventCount;
}

uint64_t
TracingSession::CorruptBufferCount() const noexcept
{
    return m_corruptBufferCount;
}

void
TracingSession::Clear() noexcept
{
    m_tracepointInfoById.clear();
    m_bufferLeaderFiles = nullptr;
    for (unsigned bufferIndex = 0; bufferIndex != m_bufferCount; bufferIndex += 1)
    {
        m_bufferMmaps[bufferIndex].reset();
    }
}

_Success_(return == 0) int
TracingSession::DisableTracePoint(std::string_view tracepointPath) noexcept
{
    auto const splitPos = tracepointPath.find_first_of(":/"sv);
    return splitPos == tracepointPath.npos
        ? EINVAL
        : DisableTracePoint(tracepointPath.substr(0, splitPos), tracepointPath.substr(splitPos + 1));
}

_Success_(return == 0) int
TracingSession::DisableTracePoint(
    std::string_view systemName,
    std::string_view eventName) noexcept
{
    int error;

    PerfEventMetadata const* metadata;
    error = m_cache.FindOrAddFromSystem(systemName, eventName, &metadata);
    if (error == 0)
    {
        auto const it = m_tracepointInfoById.find(metadata->Id());
        if (it != m_tracepointInfoById.end() &&
            it->second.EnableState != TracepointEnableState::Disabled)
        {
            error = IoctlForEachFile(it->second.BufferFiles.get(), m_bufferCount, PERF_EVENT_IOC_DISABLE, nullptr);
            it->second.EnableState = error
                ? TracepointEnableState::Unknown
                : TracepointEnableState::Disabled;
        }
    }

    return error;
}

_Success_(return == 0) int
TracingSession::EnableTracePoint(std::string_view tracepointPath) noexcept
{
    auto const splitPos = tracepointPath.find_first_of(":/"sv);
    return splitPos == tracepointPath.npos
        ? EINVAL
        : EnableTracePoint(tracepointPath.substr(0, splitPos), tracepointPath.substr(splitPos + 1));
}

_Success_(return == 0) int
TracingSession::EnableTracePoint(
    std::string_view systemName,
    std::string_view eventName) noexcept
{
    int error;
    try
    {
        PerfEventMetadata const* metadata;
        error = m_cache.FindOrAddFromSystem(systemName, eventName, &metadata);
        if (error != 0)
        {
            goto Done;
        }

        // Create a new tpi if not already there.
        // May throw bad_alloc.
        auto er = m_tracepointInfoById.try_emplace(metadata->Id(),
            std::make_unique<unique_fd[]>(m_bufferCount),
            *metadata);
        auto& tpi = er.first->second;
        if (!er.second)
        {
            // Event already in list. Make sure it's enabled.
            if (tpi.EnableState != TracepointEnableState::Enabled)
            {
                error = IoctlForEachFile(tpi.BufferFiles.get(), m_bufferCount, PERF_EVENT_IOC_ENABLE, nullptr);
                tpi.EnableState = error
                    ? TracepointEnableState::Unknown
                    : TracepointEnableState::Enabled;
            }

            goto Done;
        }

        // Starting from here, if there is an error then we must erase(metadata.Id).

        perf_event_attr eventAttribs = {};
        eventAttribs.type = PERF_TYPE_TRACEPOINT;
        eventAttribs.size = sizeof(perf_event_attr);
        eventAttribs.config = metadata->Id();
        eventAttribs.sample_period = 1;
        eventAttribs.sample_type = m_sampleType;
        eventAttribs.watermark = m_wakeupUseWatermark;
        eventAttribs.use_clockid = 1;
        eventAttribs.write_backward = !IsRealtime();
        eventAttribs.wakeup_events = m_wakeupValue;
        eventAttribs.clockid = CLOCK_MONOTONIC_RAW;

        for (unsigned bufferIndex = 0; bufferIndex != m_bufferCount; bufferIndex += 1)
        {
            errno = 0;
            tpi.BufferFiles[bufferIndex].reset(perf_event_open(&eventAttribs, -1, bufferIndex, -1, PERF_FLAG_FD_CLOEXEC));
            if (!tpi.BufferFiles[bufferIndex])
            {
                error = errno;
                if (error == 0)
                {
                    error = ENODEV;
                }

                m_tracepointInfoById.erase(metadata->Id());
                goto Done;
            }
        }

        if (m_bufferLeaderFiles)
        {
            // Leader already exists. Add this event to the leader's mmaps.
            error = IoctlForEachFile(tpi.BufferFiles.get(), m_bufferCount, PERF_EVENT_IOC_SET_OUTPUT, m_bufferLeaderFiles);
            if (error)
            {
                m_tracepointInfoById.erase(metadata->Id());
                goto Done;
            }
        }
        else
        {
            auto const mmapSize = MmapSize();
            auto const prot = IsRealtime()
                ? PROT_READ | PROT_WRITE
                : PROT_READ;

            // This is the first event. Make it the "leader".
            for (unsigned bufferIndex = 0; bufferIndex != m_bufferCount; bufferIndex += 1)
            {
                errno = 0;
                auto cpuMap = mmap(nullptr, mmapSize, prot, MAP_SHARED, tpi.BufferFiles[bufferIndex].get(), 0);
                if (MAP_FAILED == cpuMap)
                {
                    error = errno;
                    if (error == 0)
                    {
                        error = ENODEV;
                    }

                    // Clean up any mmaps that we opened.
                    for (unsigned bufferIndex2 = 0; bufferIndex2 != bufferIndex; bufferIndex2 += 1)
                    {
                        m_bufferMmaps[bufferIndex2].reset();
                    }

                    m_tracepointInfoById.erase(metadata->Id());
                    goto Done;
                }

                m_bufferMmaps[bufferIndex].reset(cpuMap, mmapSize);
            }

            m_bufferLeaderFiles = tpi.BufferFiles.get(); // Commit this event as the leader.
        }

        tpi.EnableState = TracepointEnableState::Enabled;
    }
    catch (...)
    {
        error = ENOMEM;
    }

Done:

    return error;
}

_Success_(return == 0) int
TracingSession::WaitForWakeup(
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
TracingSession::GetBufferFiles(
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
TracingSession::IoctlForEachFile(
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

size_t
TracingSession::MmapSize() const noexcept
{
    return m_pageSize + m_bufferSize;
}

TracingSession::TracepointInfo::~TracepointInfo()
{
    return;
}

TracingSession::TracepointInfo::TracepointInfo(
    std::unique_ptr<unique_fd[]> bufferFiles,
    tracepoint_decode::PerfEventMetadata const& metadata) noexcept
    : BufferFiles(std::move(bufferFiles))
    , Metadata(metadata)
    , EnableState(TracepointEnableState::Unknown)
{
    return;
}

TracingSession::~TracingSession()
{
    return;
}

TracingSession::BufferEnumerator::~BufferEnumerator()
{
    auto const bufferHeader = static_cast<perf_event_mmap_page*>(
        m_session.m_bufferMmaps[m_bufferIndex].get());

    if (!m_session.IsRealtime())
    {
        // Should not change while collection paused.
        assert(m_bufferDataHead64 == __atomic_load_n(&bufferHeader->data_head, __ATOMIC_RELAXED));

        int error = ioctl(m_session.m_bufferLeaderFiles[m_bufferIndex].get(), PERF_EVENT_IOC_PAUSE_OUTPUT, 0);
        if (error != 0)
        {
            DEBUG_PRINTF("CPU%u unpause error %u\n",
                m_bufferIndex, error);
        }
    }
    else if (m_bufferDataPos != m_bufferDataTail)
    {
        // Create a new 64-bit tail value.
        uint64_t newTail64;
        static_assert(sizeof(m_bufferDataPos) == 8 || sizeof(m_bufferDataPos) == 4);
        if constexpr (sizeof(m_bufferDataPos) == 8)
        {
            newTail64 = m_bufferDataPos;
        }
        else
        {
            // Convert m_bufferDataPos to a 64-bit value relative to m_bufferDataHead64.
            // Order of operations needs to be careful about 64-bit wrapping, e.g.
            // - DataHead64 = 0x600000000
            // - DataHead32 = 0x000000000
            // - DataPos32  = 0x0FFFFFFF8
            // Correct newTail64 is 0x5FFFFFFF8, not 0x6FFFFFFF8
            newTail64 = m_bufferDataHead64 - (static_cast<size_t>(m_bufferDataHead64) - m_bufferDataPos);
        }
        
        assert(m_bufferDataHead64 - newTail64 <= m_bufferDataPosMask + 1);

        // ATOMIC_RELEASE: perf_events.h recommends smp_mb() here.
        __atomic_store_n(&bufferHeader->data_tail, newTail64, __ATOMIC_RELEASE);
    }

    return;
}

TracingSession::BufferEnumerator::BufferEnumerator(
    TracingSession& session,
    unsigned bufferIndex) noexcept
    : m_session(session)
    , m_bufferIndex(bufferIndex)
    , m_current()
{
    auto const realtime = m_session.IsRealtime();
    if (!realtime)
    {
        int error = ioctl(m_session.m_bufferLeaderFiles[m_bufferIndex].get(), PERF_EVENT_IOC_PAUSE_OUTPUT, 1);
        if (error != 0)
        {
            DEBUG_PRINTF("CPU%u pause error %u\n",
                m_bufferIndex, error);
        }
    }

    auto const bufferMmap = session.m_bufferMmaps[bufferIndex].get();
    auto const bufferHeader = static_cast<perf_event_mmap_page const*>(bufferMmap);

    // ATOMIC_ACQUIRE: perf_events.h recommends smp_rmb() here.
    m_bufferDataHead64 = __atomic_load_n(&bufferHeader->data_head, __ATOMIC_ACQUIRE);

    auto const bufferDataOffset = static_cast<size_t>(bufferHeader->data_offset);
    auto const bufferDataSize = static_cast<size_t>(bufferHeader->data_size);

    m_bufferData = static_cast<uint8_t const*>(bufferMmap) + bufferDataOffset;
    m_bufferDataPosMask = bufferDataSize - 1;

    if (0 != (m_bufferDataHead64 & 7) ||
        0 != (reinterpret_cast<uintptr_t>(m_bufferData) & 7) ||
        0 == bufferDataSize ||
        0 != (bufferDataSize & m_bufferDataPosMask))
    {
        // Unexpected - corrupt trace buffer.
        DEBUG_PRINTF("CPU%u bad perf_event_mmap_page: head=%llx offset=%lx size=%lx\n",
            bufferIndex,
            (unsigned long long)m_bufferDataHead64,
            (unsigned long)bufferDataOffset,
            (unsigned long)bufferDataSize);
        m_bufferDataTail = static_cast<size_t>(m_bufferDataHead64) - bufferDataSize;
        m_bufferDataPos = static_cast<size_t>(m_bufferDataHead64);
        m_session.m_corruptBufferCount += 1;
    }
    else if (!realtime)
    {
        // Circular: write_backward == 1
        m_bufferDataTail = static_cast<size_t>(m_bufferDataHead64) - bufferDataSize;
        m_bufferDataPos = m_bufferDataTail;
    }
    else
    {
        // Realtime: write_backward == 0
        auto const bufferDataTail64 = bufferHeader->data_tail;
        m_bufferDataTail = static_cast<size_t>(bufferDataTail64);
        if (m_bufferDataHead64 - bufferDataTail64 > bufferDataSize)
        {
            // Unexpected - assume bad tail pointer.
            DEBUG_PRINTF("CPU%u bad data_tail: head=%llx tail=%llx offset=%lx size=%lx\n",
                bufferIndex,
                (unsigned long long)m_bufferDataHead64,
                (unsigned long long)bufferDataTail64,
                (unsigned long)bufferDataOffset,
                (unsigned long)bufferDataSize);
            m_bufferDataTail = static_cast<size_t>(m_bufferDataHead64) - bufferDataSize; // Ensure tail gets updated.
            m_bufferDataPos = static_cast<size_t>(m_bufferDataHead64);
            m_session.m_corruptBufferCount += 1;
        }
        else
        {
            m_bufferDataPos = m_bufferDataTail;
        }
    }
}

bool
TracingSession::BufferEnumerator::MoveNext() noexcept
{
    for (;;)
    {
        auto const remaining = static_cast<size_t>(m_bufferDataHead64) - m_bufferDataPos;
        if (remaining == 0)
        {
            break;
        }

        auto const eventHeaderBufferPos = m_bufferDataPos & m_bufferDataPosMask;
        auto const eventHeader = *reinterpret_cast<perf_event_header const*>(m_bufferData + eventHeaderBufferPos);

        if (eventHeader.size == 0 ||
            eventHeader.size > remaining)
        {
            // - Circular: this is probably not a real problem - it's probably
            //   unused buffer space or partially-overwritten event.
            // - Realtime: The buffer is corrupt.
            m_session.m_corruptBufferCount += m_session.IsRealtime();

            // In either case, mark the buffer's events as consumed.
            m_bufferDataPos = static_cast<size_t>(m_bufferDataHead64);
            break;
        }

        if (0 != (eventHeader.size & 7))
        {
            // Unexpected - corrupt event header.
            DEBUG_PRINTF("CPU%u unaligned eventHeader.Size at pos %lx: %u\n",
                m_bufferIndex, (unsigned long)m_bufferDataPos, eventHeader.size);

            // The event is corrupt. Mark the buffer's events as consumed.
            m_session.m_corruptBufferCount += 1;
            m_bufferDataPos = static_cast<size_t>(m_bufferDataHead64);
            break;
        }

        m_bufferDataPos += eventHeader.size;

        if (eventHeader.type == PERF_RECORD_SAMPLE)
        {
            if (ParseSample(
                eventHeader.size - sizeof(perf_event_header),
                (eventHeaderBufferPos + sizeof(perf_event_header)) & m_bufferDataPosMask))
            {
                return true;
            }
        }
        else if (eventHeader.type == PERF_RECORD_LOST)
        {
            auto const newEventsLost64 = *reinterpret_cast<uint64_t const*>(
                m_bufferData + ((eventHeaderBufferPos + sizeof(perf_event_header) + sizeof(uint64_t)) & m_bufferDataPosMask));
            m_session.m_lostEventCount += newEventsLost64;
            if (m_session.m_lostEventCount < newEventsLost64)
            {
                m_session.m_lostEventCount = UINT64_MAX;
            }
        }
    }

    return false;
}

tracepoint_decode::PerfSampleEventInfo const&
TracingSession::BufferEnumerator::Current() const noexcept
{
    return m_current;
}

bool
TracingSession::BufferEnumerator::ParseSample(
    size_t sampleDataSize,
    size_t sampleDataBufferPos) noexcept
{
    assert(0 == (sampleDataSize & 7));
    assert(0 == (sampleDataBufferPos & 7));

    uint8_t const* p;

    auto const bufferDataSize = m_bufferDataPosMask + 1;
    if (auto const unmaskedDataPosEnd = sampleDataBufferPos + sampleDataSize;
        unmaskedDataPosEnd <= bufferDataSize)
    {
        // Event does not wrap.
        p = m_bufferData + sampleDataBufferPos;
    }
    else
    {
        // Event wraps. We need to double-buffer it.

        if (m_session.m_eventDataBuffer.size() < sampleDataSize)
        {
            try
            {
                m_session.m_eventDataBuffer.resize(sampleDataSize);
            }
            catch (...)
            {
                m_session.m_lostEventCount += 1;
                if (m_session.m_lostEventCount == 0)
                {
                    m_session.m_lostEventCount -= 1;
                }

                return false; // out of memory
            }
        }

        auto const afterWrap = unmaskedDataPosEnd - bufferDataSize;
        auto const beforeWrap = sampleDataSize - afterWrap;
        auto const buffer = m_session.m_eventDataBuffer.data();
        memcpy(buffer, m_bufferData + sampleDataBufferPos, beforeWrap);
        memcpy(buffer + beforeWrap, m_bufferData, afterWrap);
        p = buffer;
    }

    auto const pEnd = p + sampleDataSize;
    uint64_t infoId = -1;
    auto const infoSampleTypes = m_session.m_sampleType;
    PerfEventMetadata const* infoRawMeta = nullptr;
    char const* infoRawData = nullptr;
    uint32_t infoRawDataSize = 0;

    m_nameBuffer[0] = 0;

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
        SampleTypeSupported == TracingSessionOptions::SampleTypeSupported,
        "SampleTypeSupported out of sync");

    auto const SampleTypeDefault = 0u
        | PERF_SAMPLE_TID
        | PERF_SAMPLE_TIME
        | PERF_SAMPLE_CPU
        | PERF_SAMPLE_RAW;
    static_assert(
        SampleTypeDefault == TracingSessionOptions::SampleTypeDefault,
        "SampleTypeDefault out of sync");

    // Fast path for default sample type.
    if (infoSampleTypes == SampleTypeDefault)
    {
        if (static_cast<size_t>(pEnd - p) <
            sizeof(uint64_t) + // PERF_SAMPLE_TID
            sizeof(uint64_t) + // PERF_SAMPLE_TIME
            sizeof(uint64_t) + // PERF_SAMPLE_CPU
            sizeof(uint64_t)) // PERF_SAMPLE_RAW
        {
            goto Error;
        }

        // PERF_SAMPLE_TID
        auto const pTid = reinterpret_cast<uint32_t const*>(p);
        m_current.pid = pTid[0];
        m_current.tid = pTid[1];
        p += sizeof(uint64_t);

        // PERF_SAMPLE_TIME
        m_current.time = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);

        // PERF_SAMPLE_CPU
        auto const pCpu = reinterpret_cast<uint32_t const*>(p);
        m_current.cpu = pCpu[0];
        m_current.cpu_reserved = pCpu[1];
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
        m_current.ip = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_TID)
    {
        if (p == pEnd) goto Error;
        auto const pTid = reinterpret_cast<uint32_t const*>(p);
        m_current.pid = pTid[0];
        m_current.tid = pTid[1];
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_TIME)
    {
        if (p == pEnd) goto Error;
        m_current.time = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_ADDR)
    {
        if (p == pEnd) goto Error;
        m_current.addr = *reinterpret_cast<uint64_t const*>(p);
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
        m_current.stream_id = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_CPU)
    {
        if (p == pEnd) goto Error;
        auto const pCpu = reinterpret_cast<uint32_t const*>(p);
        m_current.cpu = pCpu[0];
        m_current.cpu_reserved = pCpu[1];
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_PERIOD)
    {
        if (p == pEnd) goto Error;
        m_current.period = *reinterpret_cast<uint64_t const*>(p);
        p += sizeof(uint64_t);
    }

    if (infoSampleTypes & PERF_SAMPLE_CALLCHAIN)
    {
        if (p == pEnd) goto Error;
        auto const infoCallchain = reinterpret_cast<uint64_t const*>(p);
        m_current.callchain = infoCallchain;
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

        infoRawMeta = m_session.m_cache.FindByRawData({ infoRawData, infoRawDataSize });
        if (infoRawMeta)
        {
            auto const systemName = infoRawMeta->SystemName();
            auto const eventName = infoRawMeta->Name();
            if (systemName.size() + eventName.size() + 2 <= sizeof(m_nameBuffer))
            {
                auto p = m_nameBuffer;

                memcpy(p, systemName.data(), systemName.size());
                p += systemName.size();

                *p = ':';
                p += 1;

                memcpy(p, eventName.data(), eventName.size());
                p += eventName.size();

                *p = '\0';
            }
        }

        assert(p + sizeof(uint32_t) + infoRawDataSize <= pEnd);
    }
    else
    {
        assert(p <= pEnd);
    }

    m_current.id = infoId;
    m_current.attr = nullptr;
    m_current.name = m_nameBuffer;
    m_current.sample_type = infoSampleTypes;
    m_current.raw_meta = infoRawMeta;
    m_current.raw_data = infoRawData;
    m_current.raw_data_size = infoRawDataSize;

    m_session.m_sampleEventCount += 1;
    return true;

Error:

    assert(p <= pEnd);

    m_current.id = {};
    m_current.attr = {};
    m_current.name = {};
    m_current.sample_type = {};
    m_current.raw_meta = {};
    m_current.raw_data = {};
    m_current.raw_data_size = {};

    m_session.m_corruptEventCount += 1;
    return false;
}
