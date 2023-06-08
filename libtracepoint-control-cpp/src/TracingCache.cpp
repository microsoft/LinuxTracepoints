#include <tracepoint/TracingCache.h>
#include <tracepoint/TracingPath.h>
#include <assert.h>
#include <errno.h>

using namespace tracepoint_control;

TracingCache::CacheVal::~CacheVal()
{
    return;
}

TracingCache::CacheVal::CacheVal(
    std::vector<char>&& systemAndFormat,
    tracepoint_decode::PerfEventMetadata&& metadata) noexcept
    : SystemAndFormat(std::move(systemAndFormat))
    , Metadata(metadata)
{
    return;
}

size_t
TracingCache::EventNameHashOps::operator()(
    EventName const& a) const noexcept
{
    std::hash<std::string_view> hasher;
    return hasher(a.Event) ^ hasher(a.System);
}

size_t
TracingCache::EventNameHashOps::operator()(
    EventName const& a,
    EventName const& b) const noexcept
{
    return a.Event == b.Event && a.System == b.System;
}

TracingCache::EventName::EventName(
    std::string_view system,
    std::string_view event) noexcept
    : System(system)
    , Event(event)
{
    return;
}

TracingCache::~TracingCache() noexcept
{
    return;
}

TracingCache::TracingCache() noexcept(false)
    : m_byId() // may throw bad_alloc (in theory).
    , m_byName() // may throw bad_alloc (in theory).
    , m_commonTypeOffset(-1)
    , m_commonTypeSize(0)
{
    return;
}

tracepoint_decode::PerfEventMetadata const*
TracingCache::FindById(uint32_t id) const noexcept
{
    auto it = m_byId.find(id);
    return it == m_byId.end()
        ? nullptr
        : &it->second.Metadata;
}

tracepoint_decode::PerfEventMetadata const*
TracingCache::FindByName(
    std::string_view systemName,
    std::string_view eventName) const noexcept
{
    auto it = m_byName.find(EventName(systemName, eventName));
    return it == m_byName.end()
        ? nullptr
        : &it->second.Metadata;
}

_Success_(return == 0) int
TracingCache::AddFromFormat(
    std::string_view systemName,
    std::string_view formatFileContents,
    bool longSize64) noexcept
{
    int error;

    try
    {
        std::vector<char> systemAndFormat;
        systemAndFormat.reserve(systemName.size() + 1 + formatFileContents.size());
        systemAndFormat.assign(systemName.begin(), systemName.end());
        systemAndFormat.push_back('\n'); // Just for convenience for debugging.
        systemAndFormat.insert(systemAndFormat.end(), formatFileContents.begin(), formatFileContents.end());
        error = Add(std::move(systemAndFormat), systemName.size(), longSize64);
    }
    catch (...)
    {
        error = ENOMEM;
    }

    return error;
}

_Success_(return == 0) int
TracingCache::AddFromSystem(
    std::string_view systemName,
    std::string_view eventName) noexcept
{
    int error;

    try
    {
        std::vector<char> systemAndFormat;
        systemAndFormat.reserve(systemName.size() + 512);
        systemAndFormat.assign(systemName.begin(), systemName.end());
        systemAndFormat.push_back('\n'); // Just for convenience for debugging.
        error = AppendTracingFormatFile(systemAndFormat, systemName, eventName);
        if (error == 0)
        {
            error = Add(std::move(systemAndFormat), systemName.size(), sizeof(long) == 8);
        }
    }
    catch (...)
    {
        error = ENOMEM;
    }

    return error;
}

_Success_(return == 0) int
TracingCache::Add(
    std::vector<char>&& systemAndFormat,
    size_t systemNameSize,
    bool longSize64) noexcept
{
    int error;
    uint32_t id = 0;
    bool idAdded = false;

    try
    {
        assert(systemNameSize < systemAndFormat.size());
        auto systemName = std::string_view(systemAndFormat.data(), systemNameSize);
        auto formatFile = std::string_view(
            systemAndFormat.data() + systemNameSize + 1,
            systemAndFormat.size() - systemNameSize - 1);

        tracepoint_decode::PerfEventMetadata metadata;
        if (!metadata.Parse(longSize64, systemName, formatFile))
        {
            error = EINVAL;
        }
        else if (
            m_byId.end() != m_byId.find(metadata.Id()) ||
            m_byName.end() != m_byName.find(EventName(metadata.SystemName(), metadata.Name())))
        {
            error = EEXIST;
        }
        else
        {
            // TODO: m_commonTypeOffset, m_commonTypeSize.

            id = metadata.Id();

            auto er = m_byId.try_emplace(id, std::move(systemAndFormat), std::move(metadata));
            assert(er.second);
            idAdded = er.second;

            auto const& newMeta = er.first->second.Metadata;
            m_byName.try_emplace(EventName(newMeta.SystemName(), newMeta.Name()), er.first->second);

            error = 0;
        }
    }
    catch (...)
    {
        if (idAdded)
        {
            m_byId.erase(id);
        }

        error = ENOMEM;
    }

    return error;
}
