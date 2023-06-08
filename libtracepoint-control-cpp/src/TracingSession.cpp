#include <tracepoint/TracingSession.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <asm/unistd.h>
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

static uint64_t
AtomicLoad64(__u64 const* src, int order = __ATOMIC_RELAXED) noexcept
{
    return __atomic_load_n(src, order);
}

static size_t
AtomicLoad64AsSize(__u64 const* src, int order = __ATOMIC_RELAXED) noexcept
{
    return static_cast<size_t>(__atomic_load_n(src, order));
}

// Return the smallest power of 2 that is >= pageSize and >= bufferSize.
// Assumes pageSize is a power of 2.
static size_t
RoundUpBufferSize(uint32_t pageSize, size_t bufferSize) noexcept
{
    static constexpr auto BufferSizeMax =
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
    , m_cpuCount(sysconf(_SC_NPROCESSORS_ONLN))
    , m_pageSize(sysconf(_SC_PAGESIZE))
    , m_bufferSize(RoundUpBufferSize(m_pageSize, options.m_bufferSize))
    , m_cpuMmaps(std::make_unique<unique_mmap[]>(m_cpuCount)) // may throw bad_alloc.
    , m_tracepointInfoById() // may throw bad_alloc.
    , m_eventDataBuffer()
    , m_pollfd(nullptr)
    , m_leaderCpuFiles(nullptr)
    , m_sampleEventCount(0)
    , m_lostEventCount(0)
{
    // Keep in sync with comments for SampleType() method.
    static_assert(TracingSessionOptions::SampleTypeDefault ==
        PERF_SAMPLE_TID + PERF_SAMPLE_TIME + PERF_SAMPLE_CPU + PERF_SAMPLE_RAW,
        "SampleFormatDefault out of sync with header and comments");
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
    return m_cpuCount;
}

void
TracingSession::Clear() noexcept
{
    m_tracepointInfoById.clear();
    m_leaderCpuFiles = nullptr;
    for (unsigned cpuIndex = 0; cpuIndex != m_cpuCount; cpuIndex += 1)
    {
        m_cpuMmaps[cpuIndex].reset();
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
            error = IoctlForEachFile(it->second.CpuFiles.get(), m_cpuCount, PERF_EVENT_IOC_DISABLE, nullptr);
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
            std::make_unique<unique_fd[]>(m_cpuCount),
            *metadata);
        auto& tpi = er.first->second;
        if (!er.second)
        {
            // Event already in list. Make sure it's enabled.
            if (tpi.EnableState != TracepointEnableState::Enabled)
            {
                error = IoctlForEachFile(tpi.CpuFiles.get(), m_cpuCount, PERF_EVENT_IOC_ENABLE, nullptr);
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
        eventAttribs.write_backward = IsRealtime() ? 0 : 1;
        eventAttribs.wakeup_events = m_wakeupValue;
        eventAttribs.clockid = CLOCK_MONOTONIC_RAW;

        for (unsigned cpuIndex = 0; cpuIndex != m_cpuCount; cpuIndex += 1)
        {
            errno = 0;
            tpi.CpuFiles[cpuIndex].reset(perf_event_open(&eventAttribs, -1, cpuIndex, -1, PERF_FLAG_FD_CLOEXEC));
            if (!tpi.CpuFiles[cpuIndex])
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

        if (m_leaderCpuFiles)
        {
            // Leader already exists. Add this event to the leader's mmaps.
            error = IoctlForEachFile(tpi.CpuFiles.get(), m_cpuCount, PERF_EVENT_IOC_SET_OUTPUT, m_leaderCpuFiles);
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
            for (unsigned cpuIndex = 0; cpuIndex != m_cpuCount; cpuIndex += 1)
            {
                errno = 0;
                auto cpuMap = mmap(nullptr, mmapSize, prot, MAP_SHARED, tpi.CpuFiles[cpuIndex].get(), 0);
                if (MAP_FAILED == cpuMap)
                {
                    error = errno;
                    if (error == 0)
                    {
                        error = ENODEV;
                    }

                    // Clean up any mmaps that we opened.
                    for (unsigned cpuIndex2 = 0; cpuIndex2 != cpuIndex; cpuIndex2 += 1)
                    {
                        m_cpuMmaps[cpuIndex2].reset();
                    }

                    m_tracepointInfoById.erase(metadata->Id());
                    goto Done;
                }

                m_cpuMmaps[cpuIndex].reset(cpuMap, mmapSize);
            }

            m_leaderCpuFiles = tpi.CpuFiles.get(); // Commit this event as the leader.
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
    _Out_ int* pActiveCount) noexcept
{
    int error;
    int activeCount;

    if (!IsRealtime() || m_leaderCpuFiles == nullptr)
    {
        activeCount = 0;
        error = EPERM;
    }
    else try
    {
        if (m_pollfd == nullptr)
        {
            m_pollfd = std::make_unique<pollfd[]>(m_cpuCount);
        }

        for (unsigned i = 0; i != m_cpuCount; i += 1)
        {
            m_pollfd[i] = { m_leaderCpuFiles[i].get(), POLLIN, 0 };
        }

        activeCount = ppoll(m_pollfd.get(), m_cpuCount, timeout, sigmask);
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

    *pActiveCount = activeCount;
    return error;
}

_Success_(return == 0) int
TracingSession::GetBufferFiles(
    _Out_writes_(BufferCount()) int* pBufferFiles) const noexcept
{
    int error;

    if (m_leaderCpuFiles == nullptr)
    {
        memset(pBufferFiles, 0, m_cpuCount * sizeof(pBufferFiles[0]));
        error = EPERM;
    }
    else
    {
        for (unsigned i = 0; i != m_cpuCount; i += 1)
        {
            pBufferFiles[i] = m_leaderCpuFiles[i].get();
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

TracingSession::PerfEventHeader::PerfEventHeader(
    uint32_t type,
    uint16_t misc,
    uint16_t size) noexcept
    : Type(type)
    , Misc(misc)
    , Size(size)
{
    return;
}

TracingSession::TracepointInfo::~TracepointInfo()
{
    return;
}

TracingSession::TracepointInfo::TracepointInfo(
    std::unique_ptr<unique_fd[]> cpuFiles,
    tracepoint_decode::PerfEventMetadata const& metadata) noexcept
    : CpuFiles(std::move(cpuFiles))
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
    auto const bufferHeader = static_cast<perf_event_mmap_page*>(m_bufferHeader);

    if (!m_session.IsRealtime())
    {
        // Should not change while collection paused.
        assert(m_bufferDataHead == AtomicLoad64AsSize(&bufferHeader->data_head));

        int error = ioctl(m_session.m_leaderCpuFiles[m_cpuIndex].get(), PERF_EVENT_IOC_PAUSE_OUTPUT, 0);
        if (error != 0)
        {
            DEBUG_PRINTF("CPU%u unpause error %u\n",
                m_cpuIndex, error);
        }
    }
    else if (m_bufferUnmaskedPos != m_bufferDataHead)
    {
        // RELEASE: perf_events.h recommends smp_mb() here.
        __atomic_store_n(
            &bufferHeader->data_tail,
            m_bufferDataTail + (m_bufferUnmaskedPos - m_bufferDataHead),
            __ATOMIC_RELEASE);
    }

    return;
}

TracingSession::BufferEnumerator::BufferEnumerator(
    TracingSession& session,
    unsigned cpuIndex) noexcept
    : m_session(session)
    , m_bufferHeader(session.m_cpuMmaps[cpuIndex].get())
    , m_cpuIndex(cpuIndex)
    , m_current()
{
    if (!m_session.IsRealtime())
    {
        int error = ioctl(m_session.m_leaderCpuFiles[m_cpuIndex].get(), PERF_EVENT_IOC_PAUSE_OUTPUT, 1);
        if (error != 0)
        {
            DEBUG_PRINTF("CPU%u pause error %u\n",
                m_cpuIndex, error);
        }
    }

    auto const bufferHeader = static_cast<perf_event_mmap_page const*>(m_bufferHeader);

    // ACQUIRE: perf_events.h recommends smp_rmb() here.
    m_bufferDataHead = AtomicLoad64AsSize(&bufferHeader->data_head, __ATOMIC_ACQUIRE);

    m_bufferDataTail = AtomicLoad64(&bufferHeader->data_tail);
    auto const bufferDataOffset = AtomicLoad64AsSize(&bufferHeader->data_offset);
    auto const bufferDataSize = AtomicLoad64AsSize(&bufferHeader->data_size);

    m_bufferData = static_cast<uint8_t const*>(m_bufferHeader) + bufferDataOffset;
    m_bufferPosMask = bufferDataSize - 1;
    m_bufferUnmaskedPosEnd = m_bufferDataHead + bufferDataSize;

    if (0 != (m_bufferDataHead & 7) ||
        0 != (bufferDataOffset & 7) ||
        8 > bufferDataSize ||
        0 != (bufferDataSize & (bufferDataSize - 1)))
    {
        // Unexpected - corrupt trace buffer.
        DEBUG_PRINTF("CPU%u bad perf_event_mmap_page: head=%lx offs=%lx size=%lx\n",
            cpuIndex, (unsigned long)m_bufferDataHead, (unsigned long)bufferDataOffset, (unsigned long)bufferDataSize);
        m_bufferUnmaskedPosEnd = m_bufferDataHead; // Causes MoveNext() to immediately return false.
    }

    m_bufferUnmaskedPos = m_bufferDataHead;
}

bool
TracingSession::BufferEnumerator::MoveNext() noexcept
{
    while (m_bufferUnmaskedPos < m_bufferUnmaskedPosEnd)
    {
        auto const eventHeaderBufferPos = m_bufferUnmaskedPos & m_bufferPosMask;
        auto const pEventHeader = reinterpret_cast<perf_event_header const*>(m_bufferData + eventHeaderBufferPos);
        auto const eventHeader = PerfEventHeader(pEventHeader->type, pEventHeader->misc, pEventHeader->size);
        if (eventHeader.Size == 0 || // Probably unused buffer space.
            eventHeader.Size > m_bufferUnmaskedPosEnd - m_bufferUnmaskedPos) // Probably a partially-overwritten event.
        {
            break;
        }

        if (0 != (eventHeader.Size & 7))
        {
            // Unexpected - corrupt event header.
            DEBUG_PRINTF("CPU%u unaligned eventHeaderSize at pos %lx: %u\n",
                m_cpuIndex, (unsigned long)m_bufferUnmaskedPos, eventHeader.Size);
            break;
        }

        m_bufferUnmaskedPos += eventHeader.Size;

        if (eventHeader.Type == PERF_RECORD_SAMPLE)
        {
            if (ParseSample(eventHeader, eventHeaderBufferPos))
            {
                return true;
            }
        }
        else if (eventHeader.Type == PERF_RECORD_LOST)
        {
            auto const newEventsLost = *(uint64_t const volatile*)(
                m_bufferData + ((eventHeaderBufferPos + sizeof(perf_event_header) + sizeof(uint64_t)) & m_bufferPosMask));
            m_session.m_lostEventCount += newEventsLost;
            if (m_session.m_lostEventCount < newEventsLost)
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
    PerfEventHeader eventHeader,
    size_t eventHeaderBufferPos) noexcept
{
    auto const SampleTypeSupported = 0
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

    auto const infoSampleTypes = m_session.m_sampleType;
    PerfEventMetadata const* infoRawMeta = nullptr;
    char const* infoRawData = nullptr;
    uint32_t infoRawDataSize = 0;

    assert(8 <= eventHeader.Size);
    assert(0 == (eventHeader.Size & 7));
    assert(0 == (eventHeaderBufferPos & 7));

    uint8_t const* p;
    uint8_t const* pEnd;
    if (auto const unmaskedPosEnd = eventHeaderBufferPos + eventHeader.Size;
        0 == (unmaskedPosEnd & ~m_bufferPosMask))
    {
        // Event does not wrap.
        p = m_bufferData + eventHeaderBufferPos;
    }
    else try
    {
        // Event wraps. We need to double-buffer it.

        if (m_session.m_eventDataBuffer.size() < eventHeader.Size)
        {
            m_session.m_eventDataBuffer.resize(eventHeader.Size);
        }

        auto const afterWrap = unmaskedPosEnd - m_bufferPosMask - 1;
        auto const beforeWrap = eventHeader.Size - afterWrap;
        auto const buffer = m_session.m_eventDataBuffer.data();
        memcpy(buffer, m_bufferData + eventHeaderBufferPos, beforeWrap);
        memcpy(buffer + beforeWrap, m_bufferData, afterWrap);
        p = buffer;
    }
    catch (...)
    {
        goto Error; // out of memory
    }

    pEnd = p + eventHeader.Size;

    m_nameBuffer[0] = 0;
    m_current.id = -1;

    if (infoSampleTypes & PERF_SAMPLE_IDENTIFIER)
    {
        if (p == pEnd) goto Error;
        m_current.id = *reinterpret_cast<uint64_t const*>(p);
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
        auto const p32 = reinterpret_cast<uint32_t const*>(p);
        m_current.pid = p32[0];
        m_current.tid = p32[1];
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
        m_current.id = *reinterpret_cast<uint64_t const*>(p);
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
        auto const p32 = reinterpret_cast<uint32_t const*>(p);
        m_current.cpu = p32[0];
        m_current.cpu_reserved = p32[1];
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
        auto const* p32 = reinterpret_cast<uint32_t const*>(p);
        infoRawDataSize = p32[0];
        infoRawData = reinterpret_cast<char const*>(p32 + 1);
        if ((pEnd - p) - sizeof(uint32_t) < infoRawDataSize)
        {
            goto Error;
        }

        auto& cache = m_session.m_cache;
        if (m_commonTypeOffset >= 0 &&
            infoRawDataSize >= static_cast<uint32_t>(m_commonTypeOffset) + m_commonTypeSize)
        {
            uint32_t type = m_byteReader.ReadAsDynU32(infoRawData + m_commonTypeOffset, m_commonTypeSize);
            infoRawMeta = cache.FindById(type);
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
        }

        p += sizeof(uint32_t) + infoRawDataSize + sizeof(uint64_t) - 1;
    }

    assert(p <= pEnd);
    m_current.attr = nullptr;
    m_current.name = m_nameBuffer;
    m_current.sample_type = infoSampleTypes;
    m_current.raw_meta = infoRawMeta;
    m_current.raw_data = infoRawData;
    m_current.raw_data_size = infoRawDataSize;
    return true;

Error:

    return false;
}
