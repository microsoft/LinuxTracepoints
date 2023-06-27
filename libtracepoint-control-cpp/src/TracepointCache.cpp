// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <tracepoint/TracepointCache.h>
#include <tracepoint/TracepointPath.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <linux/types.h>
#include <sys/ioctl.h>

//#include <linux/user_events.h>
struct user_reg63 {

    /* Input: Size of the user_reg structure being used */
    __u32 size;

    /* Input: Bit in enable address to use */
    __u8 enable_bit;

    /* Input: Enable size in bytes at address */
    __u8 enable_size;

    /* Input: Flags for future use, set to 0 */
    __u16 flags;

    /* Input: Address to update when enabled */
    __u64 enable_addr;

    /* Input: Pointer to string with event name, description and flags */
    __u64 name_args;

    /* Output: Index of the event to use when writing data */
    __u32 write_index;
} __attribute__((__packed__));

/*
 * Describes an event unregister, callers must set the size, address and bit.
 * This structure is passed to the DIAG_IOCSUNREG ioctl to disable bit updates.
 */
struct user_unreg63 {
    /* Input: Size of the user_unreg structure being used */
    __u32 size;

    /* Input: Bit to unregister */
    __u8 disable_bit;

    /* Input: Reserved, set to 0 */
    __u8 __reserved;

    /* Input: Reserved, set to 0 */
    __u16 __reserved2;

    /* Input: Address to unregister */
    __u64 disable_addr;
} __attribute__((__packed__));

#define DIAG_IOC_MAGIC '*'
#define DIAG_IOCSREG _IOWR(DIAG_IOC_MAGIC, 0, struct user_reg63*)
#define DIAG_IOCSDEL _IOW(DIAG_IOC_MAGIC, 1, char*)
#define DIAG_IOCSUNREG _IOW(DIAG_IOC_MAGIC, 2, struct user_unreg63*)

//#include <eventheader.h>
#define EVENTHEADER_COMMAND_TYPES "u8 eventheader_flags;u8 version;u16 id;u16 tag;u8 opcode;u8 level"
enum {
    // Maximum length of a Tracepoint name "ProviderName_Attributes", including nul termination.
    EVENTHEADER_NAME_MAX = 256,

    // Maximum length needed for a DIAG_IOCSREG command "ProviderName_Attributes CommandTypes".
    EVENTHEADER_COMMAND_MAX = EVENTHEADER_NAME_MAX + sizeof(EVENTHEADER_COMMAND_TYPES)
};

using namespace std::string_view_literals;
using namespace tracepoint_control;
using namespace tracepoint_decode;

static constexpr int8_t CommonTypeOffsetInit = -1;
static constexpr uint8_t CommonTypeSizeInit = 0;

static bool
IsLowercaseHex(char ch)
{
    return ('0' <= ch && ch <= '9') || ('a' <= ch && ch <= 'f');
}

TracepointCache::TracepointRegistration::~TracepointRegistration()
{
    if (WriteIndex >= 0)
    {
        user_unreg63 unreg = {};
        unreg.size = sizeof(user_unreg63);
        unreg.disable_bit = 0;
        unreg.disable_addr = (uintptr_t)&StatusWord;
        ioctl(DataFile, DIAG_IOCSUNREG, &unreg);
    }
}

TracepointCache::TracepointRegistration::TracepointRegistration() noexcept
    : DataFile(-1)
    , WriteIndex(-1)
    , StatusWord(0)
{
    return;
}

TracepointCache::CacheVal::~CacheVal()
{
    return;
}

TracepointCache::CacheVal::CacheVal(
    std::vector<char>&& systemAndFormat,
    PerfEventMetadata&& metadata,
    std::unique_ptr<TracepointRegistration> registration) noexcept
    : SystemAndFormat(std::move(systemAndFormat))
    , Metadata(metadata)
    , Registration(std::move(registration))
{
    return;
}

size_t
TracepointCache::NameHashOps::operator()(
    TracepointName const& a) const noexcept
{
    std::hash<std::string_view> const hasher;
    return hasher(a.EventName) ^ hasher(a.SystemName);
}

size_t
TracepointCache::NameHashOps::operator()(
    TracepointName const& a,
    TracepointName const& b) const noexcept
{
    return a.EventName == b.EventName && a.SystemName == b.SystemName;
}

TracepointCache::~TracepointCache() noexcept
{
    return;
}

TracepointCache::TracepointCache() noexcept(false)
    : m_byId() // may throw bad_alloc (but probably doesn't).
    , m_byName() // may throw bad_alloc (but probably doesn't).
    , m_commonTypeOffset(CommonTypeOffsetInit)
    , m_commonTypeSize(CommonTypeSizeInit)
{
    return;
}

int8_t
TracepointCache::CommonTypeOffset() const noexcept
{
    return m_commonTypeOffset;
}

uint8_t
TracepointCache::CommonTypeSize() const noexcept
{
    return m_commonTypeSize;
}

PerfEventMetadata const*
TracepointCache::FindById(uint32_t id) const noexcept
{
    auto it = m_byId.find(id);
    return it == m_byId.end()
        ? nullptr
        : &it->second.Metadata;
}

PerfEventMetadata const*
TracepointCache::FindByName(TracepointName name) const noexcept
{
    auto it = m_byName.find(name);
    return it == m_byName.end()
        ? nullptr
        : &it->second.Metadata;
}

PerfEventMetadata const*
TracepointCache::FindByRawData(std::string_view rawData) const noexcept
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
TracepointCache::AddFromFormat(
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
        error = Add(std::move(systemAndFormat), systemName.size(), longSize64, nullptr);
    }
    catch (...)
    {
        error = ENOMEM;
    }

    return error;
}

_Success_(return == 0) int
TracepointCache::AddFromSystem(TracepointName name) noexcept
{
    int error;

    try
    {
        std::vector<char> systemAndFormat;
        systemAndFormat.reserve(name.SystemName.size() + 512); // may throw
        systemAndFormat.assign(name.SystemName.begin(), name.SystemName.end());
        systemAndFormat.push_back('\n'); // For readability when debugging.
        error = AppendTracingFormatFile(systemAndFormat, name.SystemName, name.EventName);
        if (error == 0)
        {
            error = Add(std::move(systemAndFormat), name.SystemName.size(), sizeof(long) == 8, nullptr);
        }
    }
    catch (...)
    {
        error = ENOMEM;
    }

    return error;
}

_Success_(return == 0) int
TracepointCache::FindOrAddFromSystem(
    TracepointName name,
    _Out_ PerfEventMetadata const** ppMetadata) noexcept
{
    int error;
    PerfEventMetadata const* metadata;

    if (auto it = m_byName.find(name);
        it != m_byName.end())
    {
        error = 0;
        metadata = &it->second.Metadata;
    }
    else
    {
        error = AddFromSystem(name);
        metadata = error ? nullptr : FindByName(name);
    }

    *ppMetadata = metadata;
    return error;
}

_Success_(return == 0) int
TracepointCache::PreregisterEventHeaderTracepoint(std::string_view eventName) noexcept
{
    if (eventName.size() >= EVENTHEADER_NAME_MAX ||
        eventName.size() <= 5 || // "_L1K1"
        eventName.find(' ') != eventName.npos ||
        eventName.find(':') != eventName.npos)
    {
        // Too long, or too short, or contains space, or contains colon.
        return EINVAL;
    }

    auto i = eventName.rfind('_');
    if (i > eventName.size() - 5 ||
        eventName[i + 1] != 'L' ||
        !IsLowercaseHex(eventName[i + 2]))
    {
        // Does not end with "_Ln...".
        return EINVAL;
    }

    i += 3; // Skip "_Ln".
    while (i != eventName.size() && IsLowercaseHex(eventName[i]))
    {
        // Skip additional digits of level.
        i += 1;
    }

    if (i >= eventName.size() - 1 ||
        eventName[i] != 'K' ||
        !IsLowercaseHex(eventName[i + 1]))
    {
        // Does not end with "_LnKn..."
        return EINVAL;
    }

    i += 2; // Skip "Kn"

    // Skip additional digits of Keyword and additional attributes.
    for (; i != eventName.size(); i += 1)
    {
        auto const ch = eventName[i];
        if ((ch < '0' || '9' < ch) &&
            (ch < 'A' || 'Z' < ch) &&
            (ch < 'a' || 'z' < ch))
        {
            // Invalid attribute character.
            return EINVAL;
        }
    }

    char nameArgs[EVENTHEADER_COMMAND_MAX];
    snprintf(nameArgs, sizeof(nameArgs), "%.*s " EVENTHEADER_COMMAND_TYPES,
        (unsigned)eventName.size(), eventName.data());
    return PreregisterTracepoint(nameArgs);
}

_Success_(return == 0) int
TracepointCache::PreregisterTracepoint(_In_z_ char const* registerCommand) noexcept
{
    int error;

    size_t nameEnd;
    for (nameEnd = 0;; nameEnd += 1)
    {
        auto ch = registerCommand[nameEnd];
        if (ch == 0 || ch == ' ' || ch == ':')
        {
            break;
        }
    }

    TracepointName name("user_events"sv, std::string_view(registerCommand, nameEnd));

    if (nameEnd == 0 || nameEnd >= EVENTHEADER_NAME_MAX)
    {
        error = EINVAL;
        goto Done;
    }

    if (m_byName.find(name) != m_byName.end())
    {
        error = EALREADY;
        goto Done;
    }

    try
    {
        auto registration = std::make_unique<TracepointRegistration>();

        auto const dataFile = GetUserEventsDataFile();
        if (dataFile < 0)
        {
            error = -dataFile;
            goto Done;
        }

        user_reg63 reg = {};
        reg.size = sizeof(reg);
        reg.enable_bit = 0;
        reg.enable_size = sizeof(registration->StatusWord);
        reg.enable_addr = (uintptr_t)&registration->StatusWord;
        reg.name_args = (uintptr_t)registerCommand;

        if (0 > ioctl(registration->DataFile, DIAG_IOCSREG, &reg))
        {
            error = errno;
            goto Done;
        }

        assert(reg.write_index <= 0x7fffffff);
        registration->DataFile = dataFile;
        registration->WriteIndex = static_cast<int>(reg.write_index);

        std::vector<char> systemAndFormat;
        systemAndFormat.reserve(name.SystemName.size() + 512); // may throw
        systemAndFormat.assign(name.SystemName.begin(), name.SystemName.end());
        systemAndFormat.push_back('\n'); // For readability when debugging.
        error = AppendTracingFormatFile(systemAndFormat, name.SystemName, name.EventName);
        if (error == 0)
        {
            error = Add(
                std::move(systemAndFormat),
                name.SystemName.size(),
                sizeof(long) == 8,
                std::move(registration));
        }
    }
    catch (...)
    {
        error = ENOMEM;
    }

Done:

    return error;
}

_Success_(return == 0) int
TracepointCache::Add(
    std::vector<char>&& systemAndFormat,
    size_t systemNameSize,
    bool longSize64,
    std::unique_ptr<TracepointRegistration> registration) noexcept
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
            m_byName.end() != m_byName.find(TracepointName(metadata.SystemName(), metadata.Name())))
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

                auto er = m_byId.try_emplace(
                    id,
                    std::move(systemAndFormat),
                    std::move(metadata),
                    std::move(registration));
                assert(er.second);
                idAdded = er.second;

                auto const& newMeta = er.first->second.Metadata;
                m_byName.try_emplace(TracepointName(newMeta.SystemName(), newMeta.Name()), er.first->second);

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
