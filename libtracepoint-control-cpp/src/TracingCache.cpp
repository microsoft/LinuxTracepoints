// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <tracepoint/TracingCache.h>
#include <tracepoint/TracingPath.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

using namespace std::string_view_literals;
using namespace tracepoint_control;
using namespace tracepoint_decode;

static constexpr int8_t CommonTypeOffsetInit = -1;
static constexpr uint8_t CommonTypeSizeInit = 0;

TracingCache::CacheVal::~CacheVal()
{
    return;
}

TracingCache::CacheVal::CacheVal(
    std::vector<char>&& systemAndFormat,
    PerfEventMetadata&& metadata) noexcept
    : SystemAndFormat(std::move(systemAndFormat))
    , Metadata(metadata)
{
    return;
}

size_t
TracingCache::EventNameHashOps::operator()(
    EventName const& a) const noexcept
{
    std::hash<std::string_view> const hasher;
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
    : m_byId() // may throw bad_alloc (but probably doesn't).
    , m_byName() // may throw bad_alloc (but probably doesn't).
    , m_commonTypeOffset(CommonTypeOffsetInit)
    , m_commonTypeSize(CommonTypeSizeInit)
{
    return;
}

int8_t
TracingCache::CommonTypeOffset() const noexcept
{
    return m_commonTypeOffset;
}

uint8_t
TracingCache::CommonTypeSize() const noexcept
{
    return m_commonTypeSize;
}

PerfEventMetadata const*
TracingCache::FindById(uint32_t id) const noexcept
{
    auto it = m_byId.find(id);
    return it == m_byId.end()
        ? nullptr
        : &it->second.Metadata;
}

PerfEventMetadata const*
TracingCache::FindByName(
    std::string_view systemName,
    std::string_view eventName) const noexcept
{
    auto it = m_byName.find(EventName(systemName, eventName));
    return it == m_byName.end()
        ? nullptr
        : &it->second.Metadata;
}

PerfEventMetadata const*
TracingCache::FindByRawData(std::string_view rawData) const noexcept
{
    PerfEventMetadata const* metadata;

    auto const offset = static_cast<size_t>(m_commonTypeOffset);
    auto const commonTypeSize = m_commonTypeSize;
    auto const rawDataSize = rawData.size();
    if (rawDataSize <= offset ||
        rawDataSize - offset <= commonTypeSize)
    {
        metadata = nullptr;
    }
    else if (commonTypeSize == sizeof(uint16_t))
    {
        uint16_t commonType;
        memcpy(&commonType, rawData.data() + offset, sizeof(commonType));
        metadata = FindById(commonType);
    }
    else if (commonTypeSize == sizeof(uint32_t))
    {
        uint32_t commonType;
        memcpy(&commonType, rawData.data() + offset, sizeof(commonType));
        metadata = FindById(commonType);
    }
    else
    {
        assert(commonTypeSize == 1);
        uint8_t commonType;
        memcpy(&commonType, rawData.data() + offset, sizeof(commonType));
        metadata = FindById(commonType);
    }

    return metadata;
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
        systemAndFormat.reserve(systemName.size() + 1 + formatFileContents.size()); // may throw
        systemAndFormat.assign(systemName.begin(), systemName.end());
        systemAndFormat.push_back('\n'); // For readability when debugging.
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
        systemAndFormat.reserve(systemName.size() + 512); // may throw
        systemAndFormat.assign(systemName.begin(), systemName.end());
        systemAndFormat.push_back('\n'); // For readability when debugging.
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
TracingCache::FindOrAddFromSystem(
    std::string_view systemName,
    std::string_view eventName,
    _Out_ PerfEventMetadata const** ppMetadata) noexcept
{
    int error;
    PerfEventMetadata const* metadata;

    if (auto it = m_byName.find(EventName(systemName, eventName));
        it != m_byName.end())
    {
        metadata = &it->second.Metadata;
        error = 0;
    }
    else
    {
        error = AddFromSystem(systemName, eventName);
        if (error == 0)
        {
            metadata = FindByName(systemName, eventName);
        }
    }

    *ppMetadata = metadata;
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

        PerfEventMetadata metadata;
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
            int8_t commonTypeOffset = CommonTypeOffsetInit;
            uint8_t commonTypeSize = CommonTypeSizeInit;
            for (unsigned i = 0; i != metadata.CommonFieldCount(); i += 1)
            {
                auto const& field = metadata.Fields()[i];
                if (field.Name() == "common_type"sv)
                {
                    if (field.Offset() < 128 &&
                        (field.Size() == 1 || field.Size() == 2 || field.Size() == 4) &&
                        field.Array() == PerfFieldArrayNone)
                    {
                        commonTypeOffset = static_cast<int8_t>(field.Offset());
                        commonTypeSize = static_cast<uint8_t>(field.Size());

                        if (m_commonTypeOffset == CommonTypeOffsetInit)
                        {
                            // First event to be parsed. Use its "common_type" field.
                            assert(m_commonTypeSize == CommonTypeSizeInit);
                            m_commonTypeOffset = commonTypeOffset;
                            m_commonTypeSize = commonTypeSize;
                        }
                    }
                    break;
                }
            }

            if (commonTypeOffset == CommonTypeOffsetInit)
            {
                // Did not find a usable "common_type" field.
                error = EINVAL;
            }
            else if (
                m_commonTypeOffset != commonTypeOffset ||
                m_commonTypeSize != commonTypeSize)
            {
                // Unexpected: found a different "common_type" field.
                error = EINVAL;
            }
            else
            {
                id = metadata.Id();

                auto er = m_byId.try_emplace(id, std::move(systemAndFormat), std::move(metadata));
                assert(er.second);
                idAdded = er.second;

                auto const& newMeta = er.first->second.Metadata;
                m_byName.try_emplace(EventName(newMeta.SystemName(), newMeta.Name()), er.first->second);

                error = 0;
            }
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
