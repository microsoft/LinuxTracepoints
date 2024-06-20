// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#ifndef _TIME_BITS
#define _TIME_BITS 64
#endif

#include <eventheader/EventFormatter.h>
#include <tracepoint/PerfEventMetadata.h>
#include <tracepoint/PerfEventSessionInfo.h>
#include <tracepoint/PerfEventInfo.h>
#include <tracepoint/PerfByteReader.h>
#include <tracepoint/PerfEventAbi.h>

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <stdint.h>
#include <charconv>
#include <type_traits>

#ifdef _WIN32

#include <windows.h>
#include <ip2string.h>
#include <sal.h>
#define bswap_16(u16) _byteswap_ushort(u16)
#define bswap_32(u32) _byteswap_ulong(u32)
#define bswap_64(u64) _byteswap_uint64(u64)
#define be16toh(u16) _byteswap_ushort(u16)

#else // _WIN32

#include <byteswap.h>
#include <arpa/inet.h>

#endif // _WIN32

#ifndef UNALIGNED
#define UNALIGNED __attribute__((aligned(1)))
#endif

#ifdef _Printf_format_string_
#define _Printf_format_func_(formatIndex, argIndex)
#else
#define _Printf_format_string_
#define _Printf_format_func_(formatIndex, argIndex) \
    __attribute__((__format__(__printf__, formatIndex, argIndex)))
#endif

#ifndef __fallthrough
#define __fallthrough __attribute__((__fallthrough__))
#endif

static std::string_view const ErrnoStrings[] = {
    "ERRNO(0)",
    "EPERM(1)",
    "ENOENT(2)",
    "ESRCH(3)",
    "EINTR(4)",
    "EIO(5)",
    "ENXIO(6)",
    "E2BIG(7)",
    "ENOEXEC(8)",
    "EBADF(9)",
    "ECHILD(10)",
    "EAGAIN(11)",
    "ENOMEM(12)",
    "EACCES(13)",
    "EFAULT(14)",
    "ENOTBLK(15)",
    "EBUSY(16)",
    "EEXIST(17)",
    "EXDEV(18)",
    "ENODEV(19)",
    "ENOTDIR(20)",
    "EISDIR(21)",
    "EINVAL(22)",
    "ENFILE(23)",
    "EMFILE(24)",
    "ENOTTY(25)",
    "ETXTBSY(26)",
    "EFBIG(27)",
    "ENOSPC(28)",
    "ESPIPE(29)",
    "EROFS(30)",
    "EMLINK(31)",
    "EPIPE(32)",
    "EDOM(33)",
    "ERANGE(34)",
    "EDEADLK(35)",
    "ENAMETOOLONG(36)",
    "ENOLCK(37)",
    "ENOSYS(38)",
    "ENOTEMPTY(39)",
    "ELOOP(40)",
    "ERRNO(41)", // ???
    "ENOMSG(42)",
    "EIDRM(43)",
    "ECHRNG(44)",
    "EL2NSYNC(45)",
    "EL3HLT(46)",
    "EL3RST(47)",
    "ELNRNG(48)",
    "EUNATCH(49)",
    "ENOCSI(50)",
    "EL2HLT(51)",
    "EBADE(52)",
    "EBADR(53)",
    "EXFULL(54)",
    "ENOANO(55)",
    "EBADRQC(56)",
    "EBADSLT(57)",
    "ERRNO(58)", // ???
    "EBFONT(59)",
    "ENOSTR(60)",
    "ENODATA(61)",
    "ETIME(62)",
    "ENOSR(63)",
    "ENONET(64)",
    "ENOPKG(65)",
    "EREMOTE(66)",
    "ENOLINK(67)",
    "EADV(68)",
    "ESRMNT(69)",
    "ECOMM(70)",
    "EPROTO(71)",
    "EMULTIHOP(72)",
    "EDOTDOT(73)",
    "EBADMSG(74)",
    "EOVERFLOW(75)",
    "ENOTUNIQ(76)",
    "EBADFD(77)",
    "EREMCHG(78)",
    "ELIBACC(79)",
    "ELIBBAD(80)",
    "ELIBSCN(81)",
    "ELIBMAX(82)",
    "ELIBEXEC(83)",
    "EILSEQ(84)",
    "ERESTART(85)",
    "ESTRPIPE(86)",
    "EUSERS(87)",
    "ENOTSOCK(88)",
    "EDESTADDRREQ(89)",
    "EMSGSIZE(90)",
    "EPROTOTYPE(91)",
    "ENOPROTOOPT(92)",
    "EPROTONOSUPPORT(93)",
    "ESOCKTNOSUPPORT(94)",
    "EOPNOTSUPP(95)",
    "EPFNOSUPPORT(96)",
    "EAFNOSUPPORT(97)",
    "EADDRINUSE(98)",
    "EADDRNOTAVAIL(99)",
    "ENETDOWN(100)",
    "ENETUNREACH(101)",
    "ENETRESET(102)",
    "ECONNABORTED(103)",
    "ECONNRESET(104)",
    "ENOBUFS(105)",
    "EISCONN(106)",
    "ENOTCONN(107)",
    "ESHUTDOWN(108)",
    "ETOOMANYREFS(109)",
    "ETIMEDOUT(110)",
    "ECONNREFUSED(111)",
    "EHOSTDOWN(112)",
    "EHOSTUNREACH(113)",
    "EALREADY(114)",
    "EINPROGRESS(115)",
    "ESTALE(116)",
    "EUCLEAN(117)",
    "ENOTNAM(118)",
    "ENAVAIL(119)",
    "EISNAM(120)",
    "EREMOTEIO(121)",
    "EDQUOT(122)",
    "ENOMEDIUM(123)",
    "EMEDIUMTYPE(124)",
    "ECANCELED(125)",
    "ENOKEY(126)",
    "EKEYEXPIRED(127)",
    "EKEYREVOKED(128)",
    "EKEYREJECTED(129)",
    "EOWNERDEAD(130)",
    "ENOTRECOVERABLE(131)",
    "ERFKILL(132)",
    "EHWPOISON(133)",
};
static constexpr unsigned ErrnoStringsCount = sizeof(ErrnoStrings) / sizeof(ErrnoStrings[0]);

template<class T>
static T
bswap16_if(bool needsByteSwap, void const* valData)
{
    static_assert(sizeof(T) == 2, "Expected 16-bit return type");
    auto const val = *static_cast<uint16_t const UNALIGNED*>(valData);
    return static_cast<T>(needsByteSwap ? bswap_16(val) : val);
}

template<class T>
static T
bswap32_if(bool needsByteSwap, void const* valData)
{
    static_assert(sizeof(T) == 4, "Expected 32-bit return type");
    auto const val = *static_cast<uint32_t const UNALIGNED*>(valData);
    return static_cast<T>(needsByteSwap ? bswap_32(val) : val);
}

template<class T>
static T
bswap64_if(bool needsByteSwap, void const* valData)
{
    static_assert(sizeof(T) == 8, "Expected 64-bit return type");
    auto const val = *static_cast<uint64_t const UNALIGNED*>(valData);
    return static_cast<T>(needsByteSwap ? bswap_64(val) : val);
}

using namespace std::string_view_literals;
using namespace eventheader_decode;
using namespace tracepoint_decode;

struct SwapNo
{
    uint8_t operator()(uint8_t val) const { return val; }
    uint16_t operator()(uint16_t val) const { return val; }
    uint32_t operator()(uint32_t val) const { return val; }
};

struct SwapYes
{
    uint16_t operator()(uint16_t val) const { return bswap_16(val); }
    uint32_t operator()(uint32_t val) const { return bswap_32(val); }
};

class StringBuilder
{
    char* m_pDest;
    char const* m_pDestEnd;
    std::string& m_dest;
    size_t m_destCommitSize;
    bool const m_wantJsonSpace;
    bool const m_wantFieldTag;
    bool m_needJsonComma;

#ifdef NDEBUG

#define WriteBegin(cchWorstCase) ((void)0)
#define WriteEnd() ((void)0)

#else // NDEBUG

#define WriteBegin(cchWorstCase) \
    char const* const _pLimit = m_pDest + (cchWorstCase); \
    assert(m_pDest <= _pLimit); \
    assert(_pLimit <= m_pDestEnd)
#define WriteEnd() \
    assert(m_pDest <= _pLimit); \
    assert(_pLimit <= m_pDestEnd)

#endif // NDEBUG

public:

    StringBuilder(StringBuilder const&) = delete;
    void operator=(StringBuilder const&) = delete;

    ~StringBuilder()
    {
        AssertInvariants();
        m_dest.erase(m_destCommitSize);
    }

    StringBuilder(std::string& dest, EventFormatterJsonFlags jsonFlags) noexcept
        : m_pDest(dest.data() + dest.size())
        , m_pDestEnd(dest.data() + dest.size())
        , m_dest(dest)
        , m_destCommitSize(dest.size())
        , m_wantJsonSpace(jsonFlags & EventFormatterJsonFlags_Space)
        , m_wantFieldTag(jsonFlags & EventFormatterJsonFlags_FieldTag)
        , m_needJsonComma(false)
    {
        AssertInvariants();
    }

    bool
    WantFieldTag() const noexcept
    {
        return m_wantFieldTag;
    }

    size_t Room() const noexcept
    {
        return m_pDestEnd - m_pDest;
    }

    void
    EnsureRoom(size_t roomNeeded) noexcept(false)
    {
        if (static_cast<size_t>(m_pDestEnd - m_pDest) < roomNeeded)
        {
            GrowRoom(roomNeeded);
        }
    }

    void
    Commit() noexcept
    {
        AssertInvariants();
        m_destCommitSize = m_pDest - m_dest.data();
    }

    // Requires: there is room for utf8.size() chars.
    // Assumes: utf8 is valid UTF-8.
    void
    WriteUtf8Unchecked(std::string_view utf8) noexcept
    {
        WriteBegin(utf8.size());
        memcpy(m_pDest, utf8.data(), utf8.size());
        m_pDest += utf8.size();
        WriteEnd();
    }

    // Requires: there is room for 1 char.
    // Assumes: utf8Byte is part of a valid UTF-8 sequence.
    void
    WriteUtf8ByteUnchecked(uint8_t utf8Byte) noexcept
    {
        assert(m_pDest < m_pDestEnd);
        *m_pDest++ = utf8Byte;
    }

    // Requires: there is room for 1 char.
    // Writes 0..1 chars, either [] or ["].
    void
    WriteQuoteIf(bool condition) noexcept
    {
        assert(m_pDest < m_pDestEnd);
        *m_pDest = '"';
        m_pDest += condition;
    }

    // Requires: there is room for 7 chars.
    // Writes: 1..7 UTF-8 bytes, e.g. [a].
    void
    WriteUcsChar(uint32_t ucs4) noexcept
    {
        WriteBegin(7);

        if (ucs4 >= 0x80)
        {
            WriteUcsNonAsciiChar(ucs4);
        }
        else
        {
            *m_pDest++ = static_cast<uint8_t>(ucs4);
        }

        WriteEnd();
    }

    // Requires: there is room for 7 chars.
    // Requires: nonAsciiUcs4 >= 0x80.
    // Writes: 2..7 byte UTF-8-encoded sequence.
    //
    // Unicode (non)conformance:
    // - Accepts code points in the surrogate range (generating 3-byte UTF-8 sequences
    //   encoding the surrogate).
    // - Accepts code points above 0x10FFFF (generating 4..7-byte sequences).
    void
    WriteUcsNonAsciiChar(uint32_t nonAsciiUcs4) noexcept
    {
        WriteBegin(7);
        assert(nonAsciiUcs4 >= 0x80);

        // Note that this algorithm intentionally accepts non-compliant data
        // (surrogates and values above 0x10FFFF). We want to faithfully reflect
        // what we found in the event.

        if (nonAsciiUcs4 < 0x800)
        {
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 6)) | 0xc0);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4) & 0x3f) | 0x80);
        }
        else if (nonAsciiUcs4 < 0x10000)
        {
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 12)) | 0xe0);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 6) & 0x3f) | 0x80);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4) & 0x3f) | 0x80);
        }
        else if (nonAsciiUcs4 < 0x200000)
        {
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 18)) | 0xf0);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 12) & 0x3f) | 0x80);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 6) & 0x3f) | 0x80);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4) & 0x3f) | 0x80);
        }
        else if (nonAsciiUcs4 < 0x4000000)
        {
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 24)) | 0xf8);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 18) & 0x3f) | 0x80);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 12) & 0x3f) | 0x80);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 6) & 0x3f) | 0x80);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4) & 0x3f) | 0x80);
        }
        else if (nonAsciiUcs4 < 0x80000000)
        {
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 30)) | 0xfc);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 24) & 0x3f) | 0x80);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 18) & 0x3f) | 0x80);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 12) & 0x3f) | 0x80);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 6) & 0x3f) | 0x80);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4) & 0x3f) | 0x80);
        }
        else
        {
            *m_pDest++ = static_cast<uint8_t>(0xfe);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 30) & 0x3f) | 0x80);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 24) & 0x3f) | 0x80);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 18) & 0x3f) | 0x80);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 12) & 0x3f) | 0x80);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4 >> 6) & 0x3f) | 0x80);
            *m_pDest++ = static_cast<uint8_t>(((nonAsciiUcs4) & 0x3f) | 0x80);
        }

        WriteEnd();
    }

    // Requires: there is room for 2 chars.
    // Writes 2 chars, e.g. [7f].
    void
    WriteHexByte(uint8_t val) noexcept
    {
        WriteBegin(2);
        static char const* const digits = "0123456789abcdef";
        *m_pDest++ = digits[val >> 4];
        *m_pDest++ = digits[val & 0xf];
        WriteEnd();
    }

    // Requires: there is room for cchWorstCase chars.
    // Requires: the printf format can generate no more than cchWorstCase chars.
    // Writes up to cchWorstCase plus NUL.
    void
    WritePrintf(
        size_t cchWorstCase,
        _Printf_format_string_ char const* format,
        ...) noexcept _Printf_format_func_(3, 4)
    {
        WriteBegin(cchWorstCase);

        va_list args;
        va_start(args, format);
        unsigned const cchNeeded = vsnprintf(m_pDest, cchWorstCase + 1, format, args);
        va_end(args);

        assert(cchNeeded <= cchWorstCase);
        m_pDest += (cchNeeded <= cchWorstCase ? cchNeeded : cchWorstCase);
        WriteEnd();
    }

    // Requires: there is room for cchWorstCase chars (float = 16, double = 25).
    // Requires: std::to_chars generates no more than cchWorstCase chars for value.
    // Calls std::to_chars and puts the result into the output.
    // T can be: float, double, or signed/unsigned char/short/int/long/longlong.
    template<class T>
    void
    WriteNumber(
        size_t cchWorstCase,
        T value) noexcept
    {
        WriteBegin(cchWorstCase);

        auto result = std::to_chars(m_pDest, m_pDest + cchWorstCase, value);
        assert(result.ec == std::errc{});
        m_pDest = result.ptr;

        WriteEnd();
    }

    // Requires: there is room for valSize * 3 - 1 chars.
    // Requires: valSize != 0.
    // Writes: valSize * 3 - 1, e.g. [00 11 22].
    void
    WriteHexBytes(void const* val, size_t valSize) noexcept
    {
        assert(valSize != 0);
        WriteBegin(valSize * 3 - 1);

        auto const pbVal = static_cast<uint8_t const*>(val);
        WriteHexByte(pbVal[0]);
        for (size_t i = 1; i != valSize; i += 1)
        {
            *m_pDest++ = ' ';
            WriteHexByte(pbVal[i]);
        }

        WriteEnd();
    }

    // Requires: there is room for 15 chars.
    // Reads 4 bytes.
    // Writes 7..15 chars, e.g. [0.0.0.0] or [255.255.255.255].
    void
    WriteIPv4(void const* val) noexcept
    {
        WriteBegin(15u);

        auto const p = static_cast<uint8_t const*>(val);
        WriteNumber(3, p[0]);
        *m_pDest++ = '.';
        WriteNumber(3, p[1]);
        *m_pDest++ = '.';
        WriteNumber(3, p[2]);
        *m_pDest++ = '.';
        WriteNumber(3, p[3]);

        WriteEnd();
    }

    // Requires: there is room for 45 chars.
    // Reads 16 bytes.
    // Writes 15..45 chars, e.g. [0:0:0:0:0:0:0:0] or [ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255].
    void
    WriteIPv6(void const* val) noexcept
    {
        auto constexpr DestWriteMax = 45u;
        WriteBegin(DestWriteMax);

#if _WIN32

        using PfnRtlIpv6AddressToStringA = decltype(&RtlIpv6AddressToStringA);
        static const auto pfnNotFound = reinterpret_cast<PfnRtlIpv6AddressToStringA>(static_cast<INT_PTR>(-1));
        static PfnRtlIpv6AddressToStringA pfnStatic = nullptr;

        auto pfn = pfnStatic;
        if (pfn == nullptr)
        {
            auto const ntdll = GetModuleHandleW(L"ntdll.dll");
            pfn = ntdll
                ? reinterpret_cast<PfnRtlIpv6AddressToStringA>(GetProcAddress(ntdll, "RtlIpv6AddressToStringA"))
                : nullptr;
            if (pfn == nullptr)
            {
                pfn = pfnNotFound;
            }

            pfnStatic = pfn;
        }

        if (pfn == pfnNotFound)
        {
            auto const p = static_cast<uint16_t const*>(val);
            WritePrintf(DestWriteMax,
                "%x:%x:%x:%x:%x:%x:%x:%x",
                bswap_16(p[0]), bswap_16(p[1]), bswap_16(p[2]), bswap_16(p[3]),
                bswap_16(p[4]), bswap_16(p[5]), bswap_16(p[6]), bswap_16(p[7]));
        }
        else
        {
            char ipv6[46];
            auto ipv6End = pfn(static_cast<in6_addr const*>(val), ipv6);
            WriteUtf8Unchecked({ ipv6, static_cast<size_t>(ipv6End - ipv6) });
        }

#else // _WIN32

        // INET6_ADDRSTRLEN includes 1 nul.
        static_assert(INET6_ADDRSTRLEN - 1 == DestWriteMax, "WriteIPv6Val length");
        inet_ntop(AF_INET6, val, m_pDest, DestWriteMax + 1);
        m_pDest += strnlen(m_pDest, DestWriteMax);

#endif // _WIN32

        WriteEnd();
    }

    // Requires: there is room for 36 chars.
    // Reads 16 bytes.
    // Writes 36 chars, e.g. [00000000-0000-0000-0000-000000000000].
    void
    WriteUuid(void const* val) noexcept
    {
        WriteBegin(36);

        uint8_t const* const pVal = static_cast<uint8_t const*>(val);
        WriteHexByte(pVal[0]);
        WriteHexByte(pVal[1]);
        WriteHexByte(pVal[2]);
        WriteHexByte(pVal[3]);
        *m_pDest++ = '-';
        WriteHexByte(pVal[4]);
        WriteHexByte(pVal[5]);
        *m_pDest++ = '-';
        WriteHexByte(pVal[6]);
        WriteHexByte(pVal[7]);
        *m_pDest++ = '-';
        WriteHexByte(pVal[8]);
        WriteHexByte(pVal[9]);
        *m_pDest++ = '-';
        WriteHexByte(pVal[10]);
        WriteHexByte(pVal[11]);
        WriteHexByte(pVal[12]);
        WriteHexByte(pVal[13]);
        WriteHexByte(pVal[14]);
        WriteHexByte(pVal[15]);

        WriteEnd();
    }

    // Requires: there is room for 26 chars.
    // Writes 19..26 chars, e.g. [2022-01-01T01:01:01] or [TIME(18000000000000000000)].
    void
    WriteDateTime(int64_t val) noexcept
    {
        auto const DestWriteMax = 26u;
        WriteBegin(DestWriteMax);

#if _WIN32
        if (-11644473600 <= val && val <= 910692730085)
        {
            int64_t ft = (val + 11644473600) * 10000000;
            SYSTEMTIME st;
            FileTimeToSystemTime(reinterpret_cast<FILETIME const*>(&ft), &st);
            WritePrintf(DestWriteMax, "%04u-%02u-%02uT%02u:%02u:%02u",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        }
#else // _WIN32
        struct tm tm;
        if (gmtime_r(&val, &tm))
        {
            WritePrintf(DestWriteMax, "%04d-%02u-%02uT%02u:%02u:%02u",
                1900 + tm.tm_year, 1 + tm.tm_mon, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
        }
#endif // _WIN32
        else
        {
            WritePrintf(DestWriteMax, "TIME(%" PRId64 ")", val);
        }

        WriteEnd();
    }

    // Requires: there is room for 20 chars.
    // Writes 6..20 chars, e.g. [EIO(5)] or [ENOTRECOVERABLE(131)].
    void
    WriteErrno(uint32_t val) noexcept
    {
        static_assert(ErrnoStringsCount == 134, "WriteErrno: need to update worst case (DestWriteMax)");

        auto const DestWriteMax = 20u;
        if (val < ErrnoStringsCount)
        {
            static_assert(DestWriteMax >= sizeof("ENOTRECOVERABLE(131)") - 1);
            assert(DestWriteMax >= ErrnoStrings[val].size());
            WriteUtf8Unchecked(ErrnoStrings[val]);
        }
        else
        {
            static_assert(DestWriteMax >= sizeof("ERRNO(-2000000000)") - 1);
            WritePrintf(DestWriteMax, "ERRNO(%d)", static_cast<int32_t>(val));
        }
    }

    // Requires: there is room for 11 chars.
    // Writes 1..11 chars, e.g. [false] or [-2000000000].
    // Note: the boolVal parameter is uint32 because u8/u16 should not be sign-extended.
    // However, values other than 0/1 will be formatted as i32, not u32.
    void
    WriteBoolean(uint32_t boolVal) noexcept
    {
        auto const DestWriteMax = 11u;
        WriteBegin(DestWriteMax);

        switch (boolVal)
        {
        case 0:
            WriteUtf8Unchecked("false"sv);
            break;
        case 1:
            WriteUtf8Unchecked("true"sv);
            break;
        default:
            WriteNumber(DestWriteMax, static_cast<int32_t>(boolVal));
            break;
        }

        WriteEnd();
    }

    // Returns true for ASCII control chars, double-quote, and backslash.
    static constexpr bool
    NeedsJsonEscape(uint8_t utf8Byte) noexcept
    {
        return utf8Byte < ' ' || utf8Byte == '"' || utf8Byte == '\\';
    }

    bool
    WantJsonSpace() const noexcept
    {
        return m_wantJsonSpace;
    }

    bool
    NeedJsonComma() const noexcept
    {
        return m_needJsonComma;
    }

    void
    SetNeedJsonComma(bool value) noexcept
    {
        m_needJsonComma = value;
    }

    // Requires: there is room for 1 char.
    // WriteUtf8ByteUnchecked('['); SetNeedJsonComma(false);
    void
    WriteJsonArrayBegin() noexcept
    {
        assert(m_pDest < m_pDestEnd);
        *m_pDest++ = '[';
        m_needJsonComma = false;
    }

    // Requires: there is room for 1 char.
    // WriteUtf8ByteUnchecked(']'); SetNeedJsonComma(true);
    void
    WriteJsonArrayEnd() noexcept
    {
        assert(m_pDest < m_pDestEnd);
        *m_pDest++ = ']';
        m_needJsonComma = true;
    }

    // Requires: there is room for 1 char.
    // WriteUtf8ByteUnchecked('{'); SetNeedJsonComma(false);
    void
    WriteJsonStructBegin() noexcept
    {
        assert(m_pDest < m_pDestEnd);
        *m_pDest++ = '{';
        m_needJsonComma = false;
    }

    // Requires: there is room for 1 char.
    // WriteUtf8ByteUnchecked('}'); SetNeedJsonComma(true);
    void
    WriteJsonStructEnd() noexcept
    {
        assert(m_pDest < m_pDestEnd);
        *m_pDest++ = '}';
        m_needJsonComma = true;
    }

    // Requires: there is room for 1 char.
    // Writes 0..1 chars, [] or [ ].
    void
    WriteJsonSpaceIfWanted() noexcept
    {
        assert(m_pDest < m_pDestEnd);
        *m_pDest = ' ';
        m_pDest += m_wantJsonSpace;
    }

    // Requires: there is room for 2 chars.
    // If NeedJsonComma, writes ','.
    // If WantJsonSpace, writes ' '.
    // Sets NeedJsonComma = true.
    void
    WriteJsonCommaSpaceAsNeeded() noexcept
    {
        WriteBegin(2);
        *m_pDest = ',';
        m_pDest += m_needJsonComma;
        *m_pDest = ' ';
        m_pDest += m_wantJsonSpace;
        m_needJsonComma = true;
        WriteEnd();
    }

    // Requires: there is room for 6 chars.
    // Requires: NeedsJsonEscape(ch).
    // Writes e.g. [\\u001f].
    void
    WriteJsonEscapeChar(uint8_t utf8Byte) noexcept
    {
        WriteBegin(6);
        assert(utf8Byte < 0x80);

        *m_pDest++ = '\\';
        switch (utf8Byte)
        {
        case '\\': *m_pDest++ = '\\'; break;
        case '\"': *m_pDest++ = '"'; break;
        case '\b': *m_pDest++ = 'b'; break;
        case '\f': *m_pDest++ = 'f'; break;
        case '\n': *m_pDest++ = 'n'; break;
        case '\r': *m_pDest++ = 'r'; break;
        case '\t': *m_pDest++ = 't'; break;
        default:
            *m_pDest++ = 'u';
            *m_pDest++ = '0';
            *m_pDest++ = '0';
            WriteHexByte(utf8Byte);
            break;
        }

        WriteEnd();
    }

    // Requires: there is room for 7 chars.
    // Writes: 1..7 UTF-8 bytes, e.g. [a] or [\\u00ff].
    void
    WriteUcsCharJsonEscaped(uint32_t ucs4) noexcept
    {
        WriteBegin(7);
        if (ucs4 >= 0x80)
        {
            WriteUcsNonAsciiChar(ucs4);
        }
        else
        {
            char const ascii = static_cast<uint8_t>(ucs4);
            if (NeedsJsonEscape(ascii))
            {
                WriteJsonEscapeChar(ascii);
            }
            else
            {
                *m_pDest++ = ascii;
            }
        }
        WriteEnd();
    }

private:

    void
    AssertInvariants() noexcept
    {
        assert(m_pDest >= m_dest.data() + m_destCommitSize);
        assert(m_pDest <= m_dest.data() + m_dest.size());
        assert(m_pDestEnd == m_dest.data() + m_dest.size());
        assert(m_destCommitSize <= m_dest.size());
    }

    void
    GrowRoom(size_t roomNeeded)
    {
        AssertInvariants();
        size_t const curSize = m_pDest - m_dest.data();
        size_t const totalSize = curSize + roomNeeded;
        size_t const newSize = totalSize < roomNeeded // Did it overflow?
            ? ~static_cast<size_t>(0) // Yes: trigger exception from resize.
            : totalSize;
        assert(m_dest.size() < newSize);
        m_dest.resize(newSize);
        m_pDest = m_dest.data() + curSize;
        m_pDestEnd = m_dest.data() + m_dest.size();
        AssertInvariants();
    }
};

// Requires: there is room for 9 chars.
static void
WriteUcsVal(
    StringBuilder& sb,
    uint32_t ucs4,
    bool json) noexcept
{
    assert(9u <= sb.Room());
    if (json)
    {
        sb.WriteUtf8ByteUnchecked('"');
        sb.WriteUcsCharJsonEscaped(ucs4); // UCS1
        sb.WriteUtf8ByteUnchecked('"');
    }
    else
    {
        sb.WriteUcsChar(ucs4); // UCS1
    }
}

// Detect whether std::to_chars supports a given number type.
template<class T, class = void>
struct ToCharsDetect : std::false_type {};
template<class T>
struct ToCharsDetect<T, std::void_t<decltype(std::to_chars(nullptr, nullptr, T()))> > : std::true_type {};

// Requires: there is room for 18 chars.
static void
WriteFloat32(
    StringBuilder& sb,
    uint32_t valSwapped,
    bool json)
{
    unsigned const DestWriteMax = 18 - 2; // Remove 2 for JSON quotes.

    float valFloat;
    static_assert(sizeof(valFloat) == sizeof(valSwapped), "Expected 32-bit float");
    memcpy(&valFloat, &valSwapped, sizeof(valSwapped));
    bool const needQuote = json && !isfinite(valFloat);
    sb.WriteQuoteIf(needQuote);

    // Use std::to_chars<float> if available.
    if constexpr (ToCharsDetect<float>::value)
    {
        sb.WriteNumber(DestWriteMax, valFloat);
    }
    else
    {
        sb.WritePrintf(DestWriteMax, "%.9g", valFloat);
    }

    sb.WriteQuoteIf(needQuote);
}

// Requires: there is room for 27 chars.
static void
WriteFloat64(
    StringBuilder& sb,
    uint64_t valSwapped,
    bool json)
{
    unsigned const DestWriteMax = 27 - 2; // Remove 2 for JSON quotes.

    double valFloat;
    static_assert(sizeof(valFloat) == sizeof(valSwapped), "Expected 64-bit double");
    memcpy(&valFloat, &valSwapped, sizeof(valSwapped));
    bool const needQuote = json && !isfinite(valFloat);
    sb.WriteQuoteIf(needQuote);

    // Use std::to_chars<double> if available.
    if constexpr (ToCharsDetect<double>::value)
    {
        sb.WriteNumber(DestWriteMax, valFloat);
    }
    else
    {
        sb.WritePrintf(DestWriteMax, "%.17g", valFloat);
    }

    sb.WriteQuoteIf(needQuote);
}

static void
AppendUtf8Unchecked(
    StringBuilder& sb,
    std::string_view utf8)
{
    sb.EnsureRoom(utf8.size());
    sb.WriteUtf8Unchecked(utf8);
}

// Requires: utf8.size() <= roomReserved <= sb.Room().
// Postcondition: sb.Room() >= roomReserved - utf8.size(), i.e. maintains "extra" space.
//
// If Json is true, performs Json escape.
//
// Unicode (non)conformance:
// - Accepts (passes-through) 3-byte sequences that decode to code points in the
//   surrogate range.
// - Other invalid UTF-8 input sequences are interpreted as Latin-1 sequences and are
//   converted into valid UTF-8 instead of being treated as errors.
template<bool Json>
static void
AppendUtf8WithRoomReserved(
    StringBuilder& sb,
    std::string_view utf8,
    size_t roomReserved)
{
    assert(roomReserved <= sb.Room());
    assert(roomReserved >= utf8.size());
    auto const pb = utf8.data();
    auto const cb = utf8.size();

    // Note: As long as consumed byte count == output byte count, we can skip EnsureRoom.
    // We only need to call it if we're about to output more than we consume.
    for (size_t ib = 0; ib < cb; ib += 1)
    {
        uint8_t const b0 = pb[ib];

        if (Json)
        {
            if (b0 <= 0x1F)
            {
                sb.WriteUtf8ByteUnchecked('\\');
                sb.EnsureRoom(roomReserved - ib + 4);
                switch (b0)
                {
                case '\b': sb.WriteUtf8ByteUnchecked('b'); break;
                case '\f': sb.WriteUtf8ByteUnchecked('f'); break;
                case '\n': sb.WriteUtf8ByteUnchecked('n'); break;
                case '\r': sb.WriteUtf8ByteUnchecked('r'); break;
                case '\t': sb.WriteUtf8ByteUnchecked('t'); break;
                default:
                    sb.WriteUtf8ByteUnchecked('u');
                    sb.WriteUtf8ByteUnchecked('0');
                    sb.WriteUtf8ByteUnchecked('0');
                    sb.WriteHexByte(b0);
                    break;
                }

                continue;
            }
        }

        if (b0 <= 0x7F)
        {
            if (Json)
            {
                if (b0 == '\\' || b0 == '"')
                {
                    sb.WriteUtf8ByteUnchecked('\\');
                    sb.EnsureRoom(roomReserved - ib);
                }
            }

            sb.WriteUtf8ByteUnchecked(b0);
            continue;
        }
        else if (b0 <= 0xBF)
        {
            // Invalid lead byte. Fall-through.
        }
        else if (b0 <= 0xDF)
        {
            if (cb - ib >= 2)
            {
                uint8_t const b1 = pb[ib + 1];
                if (0x80 == (b1 & 0xC0))
                {
                    auto ch = (b0 & 0x1F) << 6
                        | (b1 & 0x3F);
                    if (ch >= 0x80)
                    {
                        sb.WriteUtf8ByteUnchecked(b0);
                        sb.WriteUtf8ByteUnchecked(b1);
                        ib += 1;
                        continue; // Valid 2-byte sequence.
                    }
                }
            }
            // Invalid 2-byte sequence. Fall-through.
        }
        else if (b0 <= 0xEF)
        {
            if (cb - ib >= 3)
            {
                uint8_t const b1 = pb[ib + 1];
                uint8_t const b2 = pb[ib + 2];
                if (0x80 == (b1 & 0xC0) && 0x80 == (b2 & 0xC0))
                {
                    auto ch = (b0 & 0x0F) << 12
                        | (b1 & 0x3F) << 6
                        | (b2 & 0x3F);
                    if (ch >= 0x800) // Note: Allow surrogates to pass through.
                    {
                        sb.WriteUtf8ByteUnchecked(b0);
                        sb.WriteUtf8ByteUnchecked(b1);
                        sb.WriteUtf8ByteUnchecked(b2);
                        ib += 2;
                        continue; // Valid 3-byte sequence (or a surrogate).
                    }
                }
            }
            // Invalid 3-byte sequence. Fall-through.
        }
        else if (b0 <= 0xF4)
        {
            if (cb - ib >= 4)
            {
                uint8_t const b1 = pb[ib + 1];
                uint8_t const b2 = pb[ib + 2];
                uint8_t const b3 = pb[ib + 3];
                if (0x80 == (b1 & 0xC0) && 0x80 == (b2 & 0xC0) && 0x80 == (b3 & 0xC0))
                {
                    auto ch = (b0 & 0x07) << 18
                        | (b1 & 0x3F) << 12
                        | (b2 & 0x3F) << 6
                        | (b3 & 0x3F);
                    if (ch >= 0x010000 && ch <= 0x10FFFF)
                    {
                        sb.WriteUtf8ByteUnchecked(b0);
                        sb.WriteUtf8ByteUnchecked(b1);
                        sb.WriteUtf8ByteUnchecked(b2);
                        sb.WriteUtf8ByteUnchecked(b3);
                        ib += 3;
                        continue; // Valid 4-byte sequence.
                    }
                }
            }
            // Invalid 4-byte sequence. Fall-through.
        }

        // Invalid UTF-8 byte sequence.
        // Treat this byte as Latin-1. Expand it to a 2-byte UTF-8 sequence.
        sb.WriteUtf8ByteUnchecked(0xC0 | (b0 >> 6));
        sb.EnsureRoom(roomReserved - ib);
        sb.WriteUtf8ByteUnchecked(0x80 | (b0 & 0x3F));
    }

    assert(sb.Room() >= roomReserved - utf8.size());
}

static void
AppendUtf8JsonEscaped(
    StringBuilder& sb,
    std::string_view utf8,
    size_t extraRoomNeeded)
{
    size_t const roomNeeded = utf8.size() + extraRoomNeeded;
    sb.EnsureRoom(roomNeeded);
    AppendUtf8WithRoomReserved<true>(sb, utf8, roomNeeded);
    assert(sb.Room() >= extraRoomNeeded);
}

static void
AppendUtf8Val(
    StringBuilder& sb,
    std::string_view utf8,
    bool json)
{
    if (json)
    {
        sb.EnsureRoom(utf8.size() + 2);
        sb.WriteUtf8ByteUnchecked('"');
        AppendUtf8WithRoomReserved<true>(sb, utf8, utf8.size() + 1);
        sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
    }
    else
    {
        sb.EnsureRoom(utf8.size());
        AppendUtf8WithRoomReserved<false>(sb, utf8, utf8.size());
    }
}

template<class Swapper, class CH>
static void
AppendUcs(
    StringBuilder& sb,
    CH const* pchUcs,
    size_t cchUcs)
{
    Swapper swapper;
    auto const pchEnd = pchUcs + cchUcs;
    sb.EnsureRoom(pchEnd - pchUcs);
    for (auto pch = pchUcs; pch != pchEnd; pch += 1)
    {
        uint32_t ucs4 = swapper(*pch);
        if (ucs4 >= 0x80)
        {
            sb.EnsureRoom(pchEnd - pch + 6);
            sb.WriteUcsNonAsciiChar(ucs4);
        }
        else
        {
            sb.WriteUtf8ByteUnchecked(static_cast<uint8_t>(ucs4));
        }
    }
}

// Guaranteed to reserve at least one byte more than necessary.
template<class Swapper, class CH>
static void
AppendUcsJsonEscaped(
    StringBuilder& sb,
    CH const* pchUcs,
    size_t cchUcs,
    size_t extraRoomNeeded)
{
    Swapper swapper;
    auto const pchEnd = pchUcs + cchUcs;
    sb.EnsureRoom(pchEnd - pchUcs + extraRoomNeeded);
    for (auto pch = pchUcs; pch != pchEnd; pch += 1)
    {
        uint32_t ucs4 = swapper(*pch);
        if (ucs4 >= 0x80)
        {
            sb.EnsureRoom(pchEnd - pch + extraRoomNeeded + 6);
            sb.WriteUcsNonAsciiChar(ucs4);
        }
        else if (auto const ascii = static_cast<uint8_t>(ucs4);
            sb.NeedsJsonEscape(ascii))
        {
            sb.EnsureRoom(pchEnd - pch + extraRoomNeeded + 5);
            sb.WriteJsonEscapeChar(ascii);
        }
        else
        {
            sb.WriteUtf8ByteUnchecked(ascii);
        }
    }
}

template<class Swapper, class CH>
static void
AppendUcsVal(
    StringBuilder& sb,
    CH const* pchUcs,
    size_t cchUcs,
    bool json)
{
    if (json)
    {
        sb.EnsureRoom(cchUcs + 2);
        sb.WriteUtf8ByteUnchecked('"');
        AppendUcsJsonEscaped<Swapper>(sb, pchUcs, cchUcs, 1);
        sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
    }
    else
    {
        AppendUcs<Swapper>(sb, pchUcs, cchUcs);
    }
}

// Unicode (non)conformance:
// - Accepts unpaired surrogates (generating 3-byte UTF-8 sequences encoding the
//   surrogate).
template<class Swapper, bool Json>
static void
AppendUtf16ValImpl(
    StringBuilder& sb,
    uint16_t const* pchUtf,
    size_t cchUtf)
{
    Swapper swapper;

    // Optimistic: assume input is ASCII.
    // After this, we'll only need to call EnsureRoom if we output more than
    // one byte per uint16 consumed.
    if (Json)
    {
        sb.EnsureRoom(cchUtf + 2); // Include room for quotation marks.
        sb.WriteUtf8ByteUnchecked('"');
    }
    else
    {
        sb.EnsureRoom(cchUtf);
    }

    for (size_t ich = 0; ich != cchUtf; ich += 1)
    {
        uint16_t const w0 = swapper(pchUtf[ich]);
        if (w0 <= 0x7F)
        {
            auto const ascii = static_cast<uint8_t>(w0);
            if (Json)
            {
                if (sb.NeedsJsonEscape(ascii))
                {
                    sb.EnsureRoom(cchUtf - ich + 6);
                    sb.WriteJsonEscapeChar(ascii);
                    continue;
                }
            }

            sb.WriteUtf8ByteUnchecked(ascii);
            continue;
        }

        uint32_t ucs4;

        // Note: The following will pass-through unmatched surrogate pairs. This may
        // result in output that is technically invalid utf-8, but it preserves more
        // information from the event and hopefully helps in tracking down issues.
        if (w0 <= 0xD7FF || w0 >= 0xDC00 || cchUtf - ich <= 1)
        {
            // w0 is one of the following:
            // - non-surrogate (OK).
            // - unmatched low surrogate (invalid, pass through anyway).
            // - high surrogate at end of string (invalid, pass through anyway).
            ucs4 = w0;
        }
        else
        {
            // High surrogate and not the end of the string.
            uint16_t const w1 = swapper(pchUtf[ich + 1]);
            if (w1 <= 0xDBFF || w1 >= 0xE000)
            {
                // w0 is an unmatched high surrogate (invalid, pass through anyway).
                ucs4 = w0;
            }
            else
            {
                // w0 is a valid high surrogate.
                // w1 is a valid low surrogate.
                ucs4 = (w0 - 0xD800) << 10 | (w1 - 0xDC00) | 0x10000;
                ich += 1; // Consume w0.
            }
        }

        sb.EnsureRoom(cchUtf - ich + 7);
        sb.WriteUcsNonAsciiChar(ucs4);
    }

    if (Json)
    {
        sb.WriteUtf8ByteUnchecked('"'); // Above code guarantees room for this.
    }
}

template<class Swapper>
static void
AppendUtf16Val(
    StringBuilder& sb,
    uint16_t const* pchUtf,
    size_t cchUtf,
    bool json)
{
    if (json)
    {
        AppendUtf16ValImpl<Swapper, true>(sb, pchUtf, cchUtf);
    }
    else
    {
        AppendUtf16ValImpl<Swapper, false>(sb, pchUtf, cchUtf);
    }
}

static bool
TryAppendUtfBomVal(
    StringBuilder& sb,
    void const* pUtf,
    size_t cbUtf,
    bool json)
{
    static uint32_t const Bom32SwapNo = 0x0000FEFF;
    static uint32_t const Bom32SwapYes = 0xFFFE0000;
    static uint16_t const Bom16SwapNo = 0xFEFF;
    static uint16_t const Bom16SwapYes = 0xFFFE;
    static uint8_t const Bom8[] = { 0xEF, 0xBB, 0xBF };

    bool matchedBom;
    if (cbUtf >= 4 && 0 == memcmp(pUtf, &Bom32SwapNo, 4))
    {
        AppendUcsVal<SwapNo>(sb,
            static_cast<uint32_t const*>(pUtf) + 1,
            cbUtf / sizeof(uint32_t) - 1,
            json);
        matchedBom = true;
    }
    else if (cbUtf >= 4 && 0 == memcmp(pUtf, &Bom32SwapYes, 4))
    {
        AppendUcsVal<SwapYes>(sb,
            static_cast<uint32_t const*>(pUtf) + 1,
            cbUtf / sizeof(uint32_t) - 1,
            json);
        matchedBom = true;
    }
    else if (cbUtf >= 2 && 0 == memcmp(pUtf, &Bom16SwapNo, 2))
    {
        AppendUtf16Val<SwapNo>(sb,
            static_cast<uint16_t const*>(pUtf) + 1,
            cbUtf / sizeof(uint16_t) - 1,
            json);
        matchedBom = true;
    }
    else if (cbUtf >= 2 && 0 == memcmp(pUtf, &Bom16SwapYes, 2))
    {
        AppendUtf16Val<SwapYes>(sb,
            static_cast<uint16_t const*>(pUtf) + 1,
            cbUtf / sizeof(uint16_t) - 1,
            json);
        matchedBom = true;
    }
    else if (cbUtf >= 3 && 0 == memcmp(pUtf, Bom8, 3))
    {
        AppendUtf8Val(sb,
            { static_cast<char const*>(pUtf) + 3, cbUtf - 3 },
            json);
        matchedBom = true;
    }
    else
    {
        matchedBom = false;
    }

    return matchedBom;
}

static void
AppendHexBytesVal(
    StringBuilder& sb,
    void const* val,
    size_t valSize,
    bool json)
{
    size_t const roomNeeded = (valSize * 3) + (json * 2u);
    sb.EnsureRoom(roomNeeded);

    sb.WriteQuoteIf(json);
    if (valSize != 0)
    {
        sb.WriteHexBytes(val, valSize);
    }
    sb.WriteQuoteIf(json);
}

// e.g. [, ].
static void
AppendJsonValueBegin(
    StringBuilder& sb,
    size_t extraRoomNeeded)
{
    sb.EnsureRoom(2 + extraRoomNeeded);
    sb.WriteJsonCommaSpaceAsNeeded();
}

// e.g. [, "abc": ].
static void
AppendJsonMemberBegin(
    StringBuilder& sb,
    uint16_t fieldTag,
    std::string_view nameUtf8,
    size_t extraRoomNeeded)
{
    unsigned const MemberNeeded = 17; // [, ";tag=0xFFFF": ]
    size_t roomNeeded = MemberNeeded + nameUtf8.size() + extraRoomNeeded;
    sb.EnsureRoom(roomNeeded);

    sb.WriteJsonCommaSpaceAsNeeded();
    sb.WriteUtf8ByteUnchecked('"');

    AppendUtf8WithRoomReserved<true>(sb, nameUtf8, roomNeeded - 3);

    if (fieldTag != 0 && sb.WantFieldTag())
    {
        sb.WritePrintf(11, ";tag=0x%X", fieldTag);
    }

    sb.WriteUtf8ByteUnchecked('"');
    sb.WriteUtf8ByteUnchecked(':');
    sb.WriteJsonSpaceIfWanted();

    assert(sb.Room() >= extraRoomNeeded);
}

[[nodiscard]] static int
AppendValueImpl(
    StringBuilder& sb,
    void const* valData,
    size_t valSize,
    event_field_encoding encoding,
    event_field_format format,
    bool needsByteSwap,
    bool json)
{
    int err;

    switch (encoding)
    {
    default:
        err = ENOTSUP;
        break;
    case event_field_encoding_invalid:
    case event_field_encoding_struct:
        err = EINVAL;
        break;
    case event_field_encoding_value8:
        if (valSize != 1)
        {
            err = EINVAL;
        }
        else
        {
            unsigned const RoomNeeded = 11;
            sb.EnsureRoom(RoomNeeded);
            switch (format)
            {
            default:
            case event_field_format_unsigned_int:
                // [255] = 3
                sb.WriteNumber(RoomNeeded, *static_cast<uint8_t const*>(valData));
                break;
            case event_field_format_signed_int:
                // [-128] = 4
                sb.WriteNumber(RoomNeeded, *static_cast<int8_t const*>(valData));
                break;
            case event_field_format_hex_int:
                // ["0xFF"] = 6
                sb.WriteQuoteIf(json);
                sb.WritePrintf(RoomNeeded - 2, "0x%X", *static_cast<uint8_t const*>(valData));
                sb.WriteQuoteIf(json);
                break;
            case event_field_format_boolean:
                // [-2000000000] = 11
                sb.WriteBoolean(*static_cast<uint8_t const*>(valData));
                break;
            case event_field_format_hex_bytes:
                // ["00"] = 4
                sb.WriteQuoteIf(json);
                sb.WriteHexBytes(valData, valSize);
                sb.WriteQuoteIf(json);
                break;
            case event_field_format_string8:
                // ["\u0000"] = 9
                WriteUcsVal(sb, *static_cast<uint8_t const*>(valData), json); // UCS1
                break;
            }

            err = 0;
        }
        break;
    case event_field_encoding_value16:
        if (valSize != 2)
        {
            err = EINVAL;
        }
        else
        {
            unsigned const RoomNeeded = 9;
            sb.EnsureRoom(RoomNeeded);
            switch (format)
            {
            default:
            case event_field_format_unsigned_int:
                // [65535] = 5
                sb.WriteNumber(RoomNeeded, bswap16_if<uint16_t>(needsByteSwap, valData));
                break;
            case event_field_format_signed_int:
                // [-32768] = 6
                sb.WriteNumber(RoomNeeded, bswap16_if<int16_t>(needsByteSwap, valData));
                break;
            case event_field_format_hex_int:
                // ["0xFFFF"] = 8
                sb.WriteQuoteIf(json);
                sb.WritePrintf(RoomNeeded - 2, "0x%X", bswap16_if<uint16_t>(needsByteSwap, valData));
                sb.WriteQuoteIf(json);
                break;
            case event_field_format_boolean:
                // [-32768] = 6
                sb.WriteBoolean(bswap16_if<uint16_t>(needsByteSwap, valData));
                break;
            case event_field_format_hex_bytes:
                // ["00 00"] = 7
                sb.WriteQuoteIf(json);
                sb.WriteHexBytes(valData, valSize);
                sb.WriteQuoteIf(json);
                break;
            case event_field_format_string_utf:
                // ["\u0000"] = 9
                WriteUcsVal(sb, bswap16_if<uint16_t>(needsByteSwap, valData), json); // UCS2
                break;
            case event_field_format_port:
                // [65535] = 5
                sb.WriteNumber(RoomNeeded, be16toh(*static_cast<uint16_t const UNALIGNED*>(valData)));
                break;
            }

            err = 0;
        }
        break;
    case event_field_encoding_value32:
        if (valSize != 4)
        {
            err = EINVAL;
        }
        else
        {
            unsigned const RoomNeeded = 28;
            sb.EnsureRoom(RoomNeeded);
            switch (format)
            {
            default:
            case event_field_format_unsigned_int:
                // [4000000000] = 10
                sb.WriteNumber(RoomNeeded, bswap32_if<uint32_t>(needsByteSwap, valData));
                break;
            case event_field_format_signed_int:
            case event_field_format_pid:
                // [-2000000000] = 11
                sb.WriteNumber(RoomNeeded, bswap32_if<int32_t>(needsByteSwap, valData));
                break;
            case event_field_format_hex_int:
                // ["0xFFFFFFFF"] = 12
                sb.WriteQuoteIf(json);
                sb.WritePrintf(RoomNeeded - 2, "0x%X", bswap32_if<uint32_t>(needsByteSwap, valData));
                sb.WriteQuoteIf(json);
                break;
            case event_field_format_errno:
                // ["ENOTRECOVERABLE[131]"] = 22
                sb.WriteQuoteIf(json);
                sb.WriteErrno(bswap32_if<uint32_t>(needsByteSwap, valData));
                sb.WriteQuoteIf(json);
                break;
            case event_field_format_time:
                // ["TIME(18000000000000000000)"] = 28
                sb.WriteQuoteIf(json);
                sb.WriteDateTime(bswap32_if<int32_t>(needsByteSwap, valData));
                sb.WriteQuoteIf(json);
                break;
            case event_field_format_boolean:
                // [-2000000000] = 11
                sb.WriteBoolean(bswap32_if<uint32_t>(needsByteSwap, valData));
                break;
            case event_field_format_float:
                // ["-1.000000001e+38"] = 18
                WriteFloat32(sb, bswap32_if<uint32_t>(needsByteSwap, valData), json);
                break;
            case event_field_format_hex_bytes:
                // ["00 00 00 00"] = 13
                sb.WriteQuoteIf(json);
                sb.WriteHexBytes(valData, valSize);
                sb.WriteQuoteIf(json);
                break;
            case event_field_format_string_utf:
                // ["nnnnnnn"] = 9 (up to 7 utf-8 bytes)
                WriteUcsVal(sb, bswap32_if<uint32_t>(needsByteSwap, valData), json); // UCS4
                break;
            case event_field_format_ip_address:
            case event_field_format_ip_address_obsolete:
                // ["255.255.255.255"] = 17
                sb.WriteQuoteIf(json);
                sb.WriteIPv4(valData);
                sb.WriteQuoteIf(json);
                break;
            }

            err = 0;
        }
        break;
    case event_field_encoding_value64:
        if (valSize != 8)
        {
            err = EINVAL;
        }
        else
        {
            unsigned const RoomNeeded = 28;
            sb.EnsureRoom(RoomNeeded);
            switch (format)
            {
            default:
            case event_field_format_unsigned_int:
                // [18000000000000000000] = 20
                sb.WriteNumber(RoomNeeded, bswap64_if<uint64_t>(needsByteSwap, valData));
                break;
            case event_field_format_signed_int:
                // [-9000000000000000000] = 20
                sb.WriteNumber(RoomNeeded, bswap64_if<int64_t>(needsByteSwap, valData));
                break;
            case event_field_format_hex_int:
                // ["0xFFFFFFFFFFFFFFFF"] = 20
                sb.WriteQuoteIf(json);
                sb.WritePrintf(RoomNeeded - 2, "0x%" PRIX64, bswap64_if<uint64_t>(needsByteSwap, valData));
                sb.WriteQuoteIf(json);
                break;
            case event_field_format_time:
                // ["TIME(18000000000000000000)"] = 28
                sb.WriteQuoteIf(json);
                sb.WriteDateTime(bswap64_if<int64_t>(needsByteSwap, valData));
                sb.WriteQuoteIf(json);
                break;
            case event_field_format_float:
            {
                // ["-1.00000000000000001e+123"] = 27
                WriteFloat64(sb, bswap64_if<uint64_t>(needsByteSwap, valData), json);
                break;
            }
            case event_field_format_hex_bytes:
                // ["00 00 00 00 00 00 00 00"] = 25
                sb.WriteQuoteIf(json);
                sb.WriteHexBytes(valData, valSize);
                sb.WriteQuoteIf(json);
                break;
            }

            err = 0;
        }
        break;
    case event_field_encoding_value128:
        if (valSize != 16)
        {
            err = EINVAL;
        }
        else
        {
            unsigned const RoomNeeded = 49;
            sb.EnsureRoom(RoomNeeded);
            switch (format)
            {
            default:
            case event_field_format_hex_bytes:
                // ["00 00 00 00 ... 00 00 00 00"] = 49
                sb.WriteQuoteIf(json);
                sb.WriteHexBytes(valData, valSize);
                sb.WriteQuoteIf(json);
                break;
            case event_field_format_uuid:
                // ["00000000-0000-0000-0000-000000000000"] = 38
                sb.WriteQuoteIf(json);
                sb.WriteUuid(valData);
                sb.WriteQuoteIf(json);
                break;
            case event_field_format_ip_address:
            case event_field_format_ip_address_obsolete:
                // ["ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"] = 47
                sb.WriteQuoteIf(json);
                sb.WriteIPv6(valData);
                sb.WriteQuoteIf(json);
                break;
            }

            err = 0;
        }
        break;
    case event_field_encoding_zstring_char8:
        switch (format)
        {
        case event_field_format_hex_bytes:
            AppendHexBytesVal(sb, valData, valSize, json);
            break;
        case event_field_format_string8:
            AppendUcsVal<SwapNo>(sb,
                static_cast<uint8_t const*>(valData), valSize / sizeof(uint8_t),
                json);
            break;
        case event_field_format_string_utf_bom:
        case event_field_format_string_xml:
        case event_field_format_string_json:
            if (TryAppendUtfBomVal(sb, valData, valSize, json))
            {
                break;
            }
            __fallthrough;
        default:
        case event_field_format_string_utf:
            AppendUtf8Val(sb, { static_cast<char const*>(valData), valSize }, json);
            break;
        }

        err = 0;
        break;
    case event_field_encoding_string_length16_char8:
    case event_field_encoding_binary_length16_char8:
        switch (format)
        {
        case event_field_format_unsigned_int:
        {
            unsigned const RoomNeeded = 20;
            sb.EnsureRoom(RoomNeeded);
            switch (valSize)
            {
            case 0:
                sb.WriteUtf8Unchecked("null");
                break;
            case 1:
                // [255] = 3
                sb.WriteNumber(RoomNeeded, *static_cast<uint8_t const*>(valData));
                break;
            case 2:
                // [65535] = 5
                sb.WriteNumber(RoomNeeded, bswap16_if<uint16_t>(needsByteSwap, valData));
                break;
            case 4:
                // [4000000000] = 10
                sb.WriteNumber(RoomNeeded, bswap32_if<uint32_t>(needsByteSwap, valData));
                break;
            case 8:
                // [18000000000000000000] = 20
                sb.WriteNumber(RoomNeeded, bswap64_if<uint64_t>(needsByteSwap, valData));
                break;
            default:
                goto Char8Default;
            }
            break;
        }
        case event_field_format_signed_int:
        {
            unsigned const RoomNeeded = 20;
            sb.EnsureRoom(RoomNeeded);
            switch (valSize)
            {
            case 0:
                sb.WriteUtf8Unchecked("null");
                break;
            case 1:
                // [-128] = 4
                sb.WriteNumber(RoomNeeded, *static_cast<int8_t const*>(valData));
                break;
            case 2:
                // [-32768] = 6
                sb.WriteNumber(RoomNeeded, bswap16_if<int16_t>(needsByteSwap, valData));
                break;
            case 4:
                // [-2000000000] = 11
                sb.WriteNumber(RoomNeeded, bswap32_if<int32_t>(needsByteSwap, valData));
                break;
            case 8:
                // [-9000000000000000000] = 20
                sb.WriteNumber(RoomNeeded, bswap64_if<int64_t>(needsByteSwap, valData));
                break;
            default:
                goto Char8Default;
            }
            break;
        }
        case event_field_format_hex_int:
        {
            unsigned const RoomNeeded = 20;
            sb.EnsureRoom(RoomNeeded);
            switch (valSize)
            {
            case 0:
                sb.WriteUtf8Unchecked("null");
                break;
            case 1:
                // ["0xFF"] = 6
                sb.WriteQuoteIf(json);
                sb.WritePrintf(RoomNeeded - 2, "0x%X", *static_cast<uint8_t const*>(valData));
                sb.WriteQuoteIf(json);
                break;
            case 2:
                // ["0xFFFF"] = 8
                sb.WriteQuoteIf(json);
                sb.WritePrintf(RoomNeeded - 2, "0x%X", bswap16_if<uint16_t>(needsByteSwap, valData));
                sb.WriteQuoteIf(json);
                break;
            case 4:
                // ["0xFFFFFFFF"] = 12
                sb.WriteQuoteIf(json);
                sb.WritePrintf(RoomNeeded - 2, "0x%X", bswap32_if<uint32_t>(needsByteSwap, valData));
                sb.WriteQuoteIf(json);
                break;
            case 8:
                // ["0xFFFFFFFFFFFFFFFF"] = 20
                sb.WriteQuoteIf(json);
                sb.WritePrintf(RoomNeeded - 2, "0x%" PRIX64, bswap64_if<uint64_t>(needsByteSwap, valData));
                sb.WriteQuoteIf(json);
                break;
            default:
                goto Char8Default;
            }
            break;
        }
        case event_field_format_errno:
        {
            unsigned const RoomNeeded = 22;
            sb.EnsureRoom(RoomNeeded);
            switch (valSize)
            {
            case 0:
                sb.WriteUtf8Unchecked("null");
                break;
            case 4:
                // ["ENOTRECOVERABLE[131]"] = 22
                sb.WriteQuoteIf(json);
                sb.WriteErrno(bswap32_if<uint32_t>(needsByteSwap, valData));
                sb.WriteQuoteIf(json);
                break;
            default:
                goto Char8Default;
            }
            break;
        }
        case event_field_format_pid:
        {
            unsigned const RoomNeeded = 11;
            sb.EnsureRoom(RoomNeeded);
            switch (valSize)
            {
            case 0:
                sb.WriteUtf8Unchecked("null");
                break;
            case 4:
                // [-2000000000] = 11
                sb.WriteNumber(RoomNeeded, bswap32_if<int32_t>(needsByteSwap, valData));
                break;
            default:
                goto Char8Default;
            }
            break;
        }
        case event_field_format_time:
        {
            unsigned const RoomNeeded = 28;
            sb.EnsureRoom(RoomNeeded);
            switch (valSize)
            {
            case 0:
                sb.WriteUtf8Unchecked("null");
                break;
            case 4:
                // ["TIME(18000000000000000000)"] = 28
                sb.WriteQuoteIf(json);
                sb.WriteDateTime(bswap32_if<int32_t>(needsByteSwap, valData));
                sb.WriteQuoteIf(json);
                break;
            case 8:
                // ["TIME(18000000000000000000)"] = 28
                sb.WriteQuoteIf(json);
                sb.WriteDateTime(bswap64_if<int64_t>(needsByteSwap, valData));
                sb.WriteQuoteIf(json);
                break;
            default:
                goto Char8Default;
            }
            break;
        }
        case event_field_format_boolean:
        {
            unsigned const RoomNeeded = 11;
            sb.EnsureRoom(RoomNeeded);
            switch (valSize)
            {
            case 0:
                sb.WriteUtf8Unchecked("null");
                break;
            case 1:
                // [-2000000000] = 11
                sb.WriteBoolean(*static_cast<uint8_t const*>(valData));
                break;
            case 2:
                // [-32768] = 6
                sb.WriteBoolean(bswap16_if<uint16_t>(needsByteSwap, valData));
                break;
            case 4:
                // [-2000000000] = 11
                sb.WriteBoolean(bswap32_if<uint32_t>(needsByteSwap, valData));
                break;
            default:
                goto Char8Default;
            }
            break;
        }
        case event_field_format_float:
        {
            unsigned const RoomNeeded = 27;
            sb.EnsureRoom(RoomNeeded);
            switch (valSize)
            {
            case 0:
                sb.WriteUtf8Unchecked("null");
                break;
            case 4:
                // ["-1.000000001e+38"] = 18
                WriteFloat32(sb, bswap32_if<uint32_t>(needsByteSwap, valData), json);
                break;
            case 8:
                // ["-1.00000000000000001e+123"] = 27
                WriteFloat64(sb, bswap64_if<uint64_t>(needsByteSwap, valData), json);
                break;
            default:
                goto Char8Default;
            }
            break;
        }
        case event_field_format_hex_bytes:
        Char8HexBytes:
            AppendHexBytesVal(sb, valData, valSize, json);
            break;
        case event_field_format_string8:
            AppendUcsVal<SwapNo>(sb,
                static_cast<uint8_t const*>(valData), valSize / sizeof(uint8_t),
                json);
            break;
        case event_field_format_string_utf:
        Char8StringUtf:
            AppendUtf8Val(sb, { static_cast<char const*>(valData), valSize }, json);
            break;
        case event_field_format_string_utf_bom:
        case event_field_format_string_xml:
        case event_field_format_string_json:
            if (TryAppendUtfBomVal(sb, valData, valSize, json))
            {
                break;
            }
            goto Char8StringUtf;
        case event_field_format_uuid:
        {
            unsigned const RoomNeeded = 38;
            sb.EnsureRoom(RoomNeeded);
            switch (valSize)
            {
            case 0:
                sb.WriteUtf8Unchecked("null");
                break;
            case 16:
                // ["00000000-0000-0000-0000-000000000000"] = 38
                sb.WriteQuoteIf(json);
                sb.WriteUuid(valData);
                sb.WriteQuoteIf(json);
                break;
            default:
                goto Char8Default;
            }
            break;
        }
        case event_field_format_port:
        {
            unsigned const RoomNeeded = 5;
            sb.EnsureRoom(RoomNeeded);
            switch (valSize)
            {
            case 0:
                sb.WriteUtf8Unchecked("null");
                break;
            case 2:
                // [65535] = 5
                sb.WriteNumber(RoomNeeded, be16toh(*static_cast<uint16_t const UNALIGNED*>(valData)));
                break;
            default:
                goto Char8Default;
            }
            break;
        }
        case event_field_format_ip_address:
        case event_field_format_ip_address_obsolete:
        {
            unsigned const RoomNeeded = 47;
            sb.EnsureRoom(RoomNeeded);
            switch (valSize)
            {
            case 0:
                sb.WriteUtf8Unchecked("null");
                break;
            case 4:
                // ["255.255.255.255"] = 17
                sb.WriteQuoteIf(json);
                sb.WriteIPv4(valData);
                sb.WriteQuoteIf(json);
                break;
            case 16:
                // ["ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255"] = 47
                sb.WriteQuoteIf(json);
                sb.WriteIPv6(valData);
                sb.WriteQuoteIf(json);
                break;
            default:
                goto Char8Default;
            }
            break;
        }
        default:
        case event_field_format_default:
        Char8Default:
            if (encoding == event_field_encoding_binary_length16_char8)
            {
                goto Char8HexBytes;
            }
            else
            {
                goto Char8StringUtf;
            }
            break;
        }

        err = 0;
        break;
    case event_field_encoding_zstring_char16:
    case event_field_encoding_string_length16_char16:
        if (valSize & 1)
        {
            err = EINVAL;
        }
        else
        {
            switch (format)
            {
            case event_field_format_hex_bytes:
                AppendHexBytesVal(sb, valData, valSize, json);
                break;
            case event_field_format_string_utf_bom:
            case event_field_format_string_xml:
            case event_field_format_string_json:
                if (TryAppendUtfBomVal(sb, valData, valSize, json))
                {
                    break;
                }
                __fallthrough;
            default:
            case event_field_format_string_utf:
                if (needsByteSwap)
                {
                    AppendUtf16Val<SwapYes>(sb,
                        static_cast<uint16_t const*>(valData), valSize / sizeof(uint16_t),
                        json);
                }
                else
                {
                    AppendUtf16Val<SwapNo>(sb,
                        static_cast<uint16_t const*>(valData), valSize / sizeof(uint16_t),
                        json);
                }
                break;
            }

            err = 0;
        }
        break;
    case event_field_encoding_zstring_char32:
    case event_field_encoding_string_length16_char32:
        if (valSize & 3)
        {
            err = EINVAL;
        }
        else
        {
            switch (format)
            {
            case event_field_format_hex_bytes:
                AppendHexBytesVal(sb, valData, valSize, json);
                break;
            case event_field_format_string_utf_bom:
            case event_field_format_string_xml:
            case event_field_format_string_json:
                if (TryAppendUtfBomVal(sb, valData, valSize, json))
                {
                    break;
                }
                __fallthrough;
            default:
            case event_field_format_string_utf:
                if (needsByteSwap)
                {
                    AppendUcsVal<SwapYes>(sb,
                        static_cast<uint32_t const*>(valData), valSize / sizeof(uint32_t),
                        json);
                }
                else
                {
                    AppendUcsVal<SwapNo>(sb,
                        static_cast<uint32_t const*>(valData), valSize / sizeof(uint32_t),
                        json);
                }
                break;
            }

            err = 0;
        }
        break;
    }

    return err;
}

[[nodiscard]] static int
AppendItemAsJsonImpl(
    StringBuilder& sb,
    EventEnumerator& enumerator,
    bool wantName)
{
    int err;
    int depth = 0;

    do
    {
        EventItemInfo itemInfo;

        switch (enumerator.State())
        {
        default:
            assert(!"Enumerator in invalid state.");
            err = EINVAL;
            goto Done;

        case EventEnumeratorState_BeforeFirstItem:
            depth += 1;
            break;

        case EventEnumeratorState_Value:

            itemInfo = enumerator.GetItemInfo();
            wantName && !itemInfo.ArrayFlags
                ? AppendJsonMemberBegin(sb, itemInfo.FieldTag, itemInfo.Name, 0)
                : AppendJsonValueBegin(sb, 0);
            err = AppendValueImpl(
                sb, itemInfo.ValueData, itemInfo.ValueSize,
                itemInfo.Encoding, itemInfo.Format, itemInfo.NeedByteSwap,
                true);
            if (err != 0)
            {
                goto Done;
            }
            break;

        case EventEnumeratorState_ArrayBegin:

            itemInfo = enumerator.GetItemInfo();
            wantName
                ? AppendJsonMemberBegin(sb, itemInfo.FieldTag, itemInfo.Name, 1)
                : AppendJsonValueBegin(sb, 1);
            sb.WriteJsonArrayBegin(); // 1 extra byte reserved above.

            depth += 1;
            break;

        case EventEnumeratorState_ArrayEnd:

            sb.EnsureRoom(2);
            sb.WriteJsonSpaceIfWanted();
            sb.WriteJsonArrayEnd();

            depth -= 1;
            break;

        case EventEnumeratorState_StructBegin:

            itemInfo = enumerator.GetItemInfo();
            wantName && !itemInfo.ArrayFlags
                ? AppendJsonMemberBegin(sb, itemInfo.FieldTag, itemInfo.Name, 1)
                : AppendJsonValueBegin(sb, 1);
            sb.WriteJsonStructBegin(); // 1 extra byte reserved above.

            depth += 1;
            break;

        case EventEnumeratorState_StructEnd:

            sb.EnsureRoom(2);
            sb.WriteJsonSpaceIfWanted();
            sb.WriteJsonStructEnd();

            depth -= 1;
            break;
        }

        wantName = true;
    } while (enumerator.MoveNext() && depth > 0);

    err = enumerator.LastError();

Done:

    return err;
}

static void
AppendMetaN(
    StringBuilder& sb,
    EventInfo const& ei)
{
    uint8_t cchName = 0;
    while (ei.Name[cchName] != '\0' && ei.Name[cchName] != ';')
    {
        cchName += 1;
    }

    AppendJsonMemberBegin(sb, 0, "n"sv, 1);
    sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
    AppendUtf8JsonEscaped(sb, { ei.TracepointName, ei.ProviderNameLength }, 1);
    sb.WriteUtf8ByteUnchecked(':'); // 1 extra byte reserved above.
    AppendUtf8JsonEscaped(sb, { ei.Name, cchName }, 1);
    sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
}

static void
AppendMetaEventInfo(
    StringBuilder& sb,
    EventFormatterMetaFlags metaFlags,
    EventInfo const& ei)
{
    if (metaFlags & EventFormatterMetaFlags_provider)
    {
        AppendJsonMemberBegin(sb, 0, "provider"sv, 1);
        sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
        AppendUtf8JsonEscaped(sb, { ei.TracepointName, ei.ProviderNameLength }, 1);
        sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
    }

    if (metaFlags & EventFormatterMetaFlags_event)
    {
        AppendJsonMemberBegin(sb, 0, "event"sv, 1);
        sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
        AppendUtf8JsonEscaped(sb, ei.Name, 1);
        sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
    }

    if ((metaFlags & EventFormatterMetaFlags_id) && ei.Header.id != 0)
    {
        AppendJsonMemberBegin(sb, 0, "id"sv, 5);
        sb.WriteNumber(5, ei.Header.id);
    }

    if ((metaFlags & EventFormatterMetaFlags_version) && ei.Header.version != 0)
    {
        AppendJsonMemberBegin(sb, 0, "version"sv, 3);
        sb.WriteNumber(3, ei.Header.version);
    }

    if ((metaFlags & EventFormatterMetaFlags_level) && ei.Header.level != 0)
    {
        AppendJsonMemberBegin(sb, 0, "level"sv, 3);
        sb.WriteNumber(3, ei.Header.level);
    }

    if ((metaFlags & EventFormatterMetaFlags_keyword) && ei.Keyword != 0)
    {
        AppendJsonMemberBegin(sb, 0, "keyword"sv, 20);
        sb.WritePrintf(20, "\"0x%" PRIX64 "\"", ei.Keyword);
    }

    if ((metaFlags & EventFormatterMetaFlags_opcode) && ei.Header.opcode != 0)
    {
        AppendJsonMemberBegin(sb, 0, "opcode"sv, 3);
        sb.WriteNumber(3, ei.Header.opcode);
    }

    if ((metaFlags & EventFormatterMetaFlags_tag) && ei.Header.tag != 0)
    {
        AppendJsonMemberBegin(sb, 0, "tag"sv, 8);
        sb.WritePrintf(8, "\"0x%X\"", ei.Header.tag);
    }

    if ((metaFlags & EventFormatterMetaFlags_activity) && ei.ActivityId != nullptr)
    {
        AppendJsonMemberBegin(sb, 0, "activity"sv, 38);
        sb.WriteUtf8ByteUnchecked('"');
        sb.WriteUuid(ei.ActivityId);
        sb.WriteUtf8ByteUnchecked('"');
    }

    if ((metaFlags & EventFormatterMetaFlags_relatedActivity) && ei.RelatedActivityId != nullptr)
    {
        AppendJsonMemberBegin(sb, 0, "relatedActivity"sv, 38);
        sb.WriteUtf8ByteUnchecked('"');
        sb.WriteUuid(ei.RelatedActivityId);
        sb.WriteUtf8ByteUnchecked('"');
    }

    if ((metaFlags & EventFormatterMetaFlags_options) && ei.OptionsIndex < ei.TracepointNameLength)
    {
        AppendJsonMemberBegin(sb, 0, "options"sv, 1);
        sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
        std::string_view options = {
            ei.TracepointName + ei.OptionsIndex,
            (size_t)ei.TracepointNameLength - ei.OptionsIndex };
        AppendUtf8JsonEscaped(sb, options, 1);
        sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
    }

    if (metaFlags & EventFormatterMetaFlags_flags)
    {
        AppendJsonMemberBegin(sb, 0, "flags"sv, 6);
        sb.WritePrintf(6, "\"0x%X\"", ei.Header.flags);
    }
}

// Assumes that there is room for '[' when called.
static void
AppendIntegerSampleFieldAsJsonImpl(
    StringBuilder& sb,
    std::string_view fieldRawData,
    PerfFieldMetadata const& fieldMetadata,
    bool fileBigEndian,
    char const* format32,
    char const* format64)
{
    assert(sb.Room() > 0);
    PerfByteReader const byteReader(fileBigEndian);

    if (fieldMetadata.Array() == PerfFieldArrayNone)
    {
        switch (fieldMetadata.ElementSize())
        {
        case PerfFieldElementSize8:
            if (fieldRawData.size() < sizeof(uint8_t))
            {
                AppendUtf8Unchecked(sb, "null"sv);
            }
            else
            {
                unsigned const RoomNeeded = 6; // ["0xFF"]
                sb.EnsureRoom(RoomNeeded);
                auto val = byteReader.ReadAsU8(fieldRawData.data());
                sb.WritePrintf(RoomNeeded, format32, val);
            }
            break;
        case PerfFieldElementSize16:
            if (fieldRawData.size() < sizeof(uint16_t))
            {
                AppendUtf8Unchecked(sb, "null"sv);
            }
            else
            {
                unsigned const RoomNeeded = 8; // ["0xFFFF"]
                sb.EnsureRoom(RoomNeeded);
                auto val = byteReader.ReadAsU16(fieldRawData.data());
                sb.WritePrintf(RoomNeeded, format32, val);
            }
            break;
        case PerfFieldElementSize32:
            if (fieldRawData.size() < sizeof(uint32_t))
            {
                AppendUtf8Unchecked(sb, "null"sv);
            }
            else
            {
                unsigned const RoomNeeded = 12; // ["0xFFFFFFFF"]
                sb.EnsureRoom(RoomNeeded);
                auto val = byteReader.ReadAsU32(fieldRawData.data());
                sb.WritePrintf(RoomNeeded, format32, val);
            }
            break;
        case PerfFieldElementSize64:
            if (fieldRawData.size() < sizeof(uint64_t))
            {
                AppendUtf8Unchecked(sb, "null"sv);
            }
            else
            {
                unsigned const RoomNeeded = 20; // ["0xFFFFFFFFFFFFFFFF"]
                sb.EnsureRoom(RoomNeeded);
                auto val = byteReader.ReadAsU64(fieldRawData.data());
                sb.WritePrintf(RoomNeeded, format64, val);
            }
            break;
        }
    }
    else
    {
        void const* const pvData = fieldRawData.data();
        auto const cbData = fieldRawData.size();

        sb.WriteJsonArrayBegin(); // Caller is expected to give us room for 1 char.
        switch (fieldMetadata.ElementSize())
        {
        case PerfFieldElementSize8:
            for (auto p = static_cast<uint8_t const*>(pvData), pEnd = p + cbData / sizeof(p[0]); p != pEnd; p += 1)
            {
                unsigned const RoomNeeded = 6; // ["0xFF"]
                sb.EnsureRoom(RoomNeeded + 2);
                sb.WriteJsonCommaSpaceAsNeeded();
                sb.WritePrintf(RoomNeeded, format32, byteReader.Read(p));
            }
            break;
        case PerfFieldElementSize16:
            for (auto p = static_cast<uint16_t const*>(pvData), pEnd = p + cbData / sizeof(p[0]); p != pEnd; p += 1)
            {
                unsigned const RoomNeeded = 8; // ["0xFFFF"]
                sb.EnsureRoom(RoomNeeded + 2);
                sb.WriteJsonCommaSpaceAsNeeded();
                sb.WritePrintf(RoomNeeded, format32, byteReader.Read(p));
            }
            break;
        case PerfFieldElementSize32:
            for (auto p = static_cast<uint32_t const*>(pvData), pEnd = p + cbData / sizeof(p[0]); p != pEnd; p += 1)
            {
                unsigned const RoomNeeded = 12; // ["0xFFFFFFFF"]
                sb.EnsureRoom(RoomNeeded + 2);
                sb.WriteJsonCommaSpaceAsNeeded();
                sb.WritePrintf(RoomNeeded, format32, byteReader.Read(p));
            }
            break;
        case PerfFieldElementSize64:
            for (auto p = static_cast<uint64_t const*>(pvData), pEnd = p + cbData / sizeof(p[0]); p != pEnd; p += 1)
            {
                unsigned const RoomNeeded = 20; // ["0xFFFFFFFFFFFFFFFF"]
                sb.EnsureRoom(RoomNeeded + 2);
                sb.WriteJsonCommaSpaceAsNeeded();
                sb.WritePrintf(RoomNeeded, format64, byteReader.Read(p));
            }
            break;
        }

        sb.EnsureRoom(2);
        sb.WriteJsonSpaceIfWanted();
        sb.WriteJsonArrayEnd();
    }
}

static void
AppendSampleFieldAsJsonImpl(
    StringBuilder& sb,
    _In_reads_bytes_(fieldRawDataSize) void const* fieldRawData,
    size_t fieldRawDataSize,
    PerfFieldMetadata const& fieldMetadata,
    bool fileBigEndian,
    bool wantName)
{
    PerfByteReader const byteReader(fileBigEndian);
    auto const fieldRawDataChars = static_cast<char const*>(fieldRawData);

    // Note: AppendIntegerSampleFieldAsJsonImpl expects 1 byte reserved for '['.
    wantName
        ? AppendJsonMemberBegin(sb, 0, fieldMetadata.Name(), 1)
        : AppendJsonValueBegin(sb, 1);
    switch (fieldMetadata.Format())
    {
    default:
    case PerfFieldFormatNone:
        if (fieldMetadata.Array() == PerfFieldArrayNone ||
            fieldMetadata.ElementSize() == PerfFieldElementSize8)
        {
            // Single unknown item, or an array of 8-bit unknown items: Treat as one binary blob.
            AppendHexBytesVal(sb, fieldRawDataChars, fieldRawDataSize, true);
            break;
        }
        // Array of unknown items: Treat as hex integers.
        [[fallthrough]];
    case PerfFieldFormatHex:
        AppendIntegerSampleFieldAsJsonImpl(sb, { fieldRawDataChars, fieldRawDataSize },
            fieldMetadata, fileBigEndian, "\"0x%" PRIX32 "\"", "\"0x%" PRIX64 "\"");
        break;
    case PerfFieldFormatUnsigned:
        AppendIntegerSampleFieldAsJsonImpl(sb, { fieldRawDataChars, fieldRawDataSize },
            fieldMetadata, fileBigEndian, "%" PRIu32, "%" PRIu64);
        break;
    case PerfFieldFormatSigned:
        AppendIntegerSampleFieldAsJsonImpl(sb, { fieldRawDataChars, fieldRawDataSize },
            fieldMetadata, fileBigEndian, "%" PRId32, "%" PRId64);
        break;
    case PerfFieldFormatString:
        AppendUcsVal<SwapNo>(sb,
            reinterpret_cast<uint8_t const*>(fieldRawDataChars),
            strnlen(fieldRawDataChars, fieldRawDataSize),
            true);
        break;
    }
}

int
EventFormatter::AppendSampleAsJson(
    std::string& dest,
    PerfSampleEventInfo const& sampleEventInfo,
    bool fileBigEndian,
    EventFormatterJsonFlags jsonFlags,
    EventFormatterMetaFlags metaFlags,
    uint32_t moveNextLimit)
{
    StringBuilder sb(dest, jsonFlags);
    int err = 0;

    EventEnumerator enumerator;
    EventInfo eventInfo;
    bool eventInfoValid;
    std::string_view sampleEventName;
    std::string_view sampleProviderName;
    auto const sampleEventInfoSampleType = sampleEventInfo.SampleType();
    auto const sampleEventInfoMetadata = sampleEventInfo.Metadata();

    if (sampleEventInfoMetadata &&
        sampleEventInfoMetadata->Kind() == PerfEventKind::EventHeader)
    {
        // eventheader metadata.

        auto const& meta = *sampleEventInfoMetadata;
        auto const eventHeaderOffset = meta.Fields()[meta.CommonFieldCount()].Offset();
        if (eventHeaderOffset > sampleEventInfo.raw_data_size ||
            !enumerator.StartEvent(
                meta.Name().data(),
                meta.Name().size(),
                static_cast<char const*>(sampleEventInfo.raw_data) + eventHeaderOffset,
                sampleEventInfo.raw_data_size - eventHeaderOffset,
                moveNextLimit))
        {
            goto NotEventHeader;
        }

        eventInfo = enumerator.GetEventInfo();
        eventInfoValid = true;

        (jsonFlags & EventFormatterJsonFlags_Name)
            ? AppendJsonMemberBegin(sb, 0, eventInfo.Name, 1)
            : AppendJsonValueBegin(sb, 1);
        sb.WriteJsonStructBegin(); // top-level

        if (metaFlags & EventFormatterMetaFlags_n)
        {
            AppendMetaN(sb, eventInfo);
        }

        if (metaFlags & EventFormatterMetaFlags_common)
        {
            for (size_t iField = 0; iField != meta.CommonFieldCount(); iField += 1)
            {
                auto const fieldMeta = meta.Fields()[iField];
                auto const fieldData = fieldMeta.GetFieldBytes(
                    sampleEventInfo.raw_data,
                    sampleEventInfo.raw_data_size,
                    fileBigEndian);
                AppendSampleFieldAsJsonImpl(sb, fieldData.data(), fieldData.size(), fieldMeta, fileBigEndian, true);
            }
        }

        err = AppendItemAsJsonImpl(sb, enumerator, true);
        if (err != 0)
        {
            goto Done;
        }
    }
    else
    {
    NotEventHeader:

        auto const sampleEventInfoName = sampleEventInfo.Name();
        if (sampleEventInfoName[0] == 0 && sampleEventInfoMetadata != nullptr)
        {
            // No name from PERF_HEADER_EVENT_DESC, but metadata is present so use that.
            sampleProviderName = sampleEventInfoMetadata->SystemName();
            sampleEventName = sampleEventInfoMetadata->Name();
        }
        else
        {
            auto const sampleEventNameColon = strchr(sampleEventInfoName, ':');
            if (sampleEventNameColon == nullptr)
            {
                // No colon in name.
                // Put everything into provider name (probably "" anyway).
                sampleProviderName = sampleEventInfoName;
                sampleEventName = "";
            }
            else
            {
                // Name contained a colon.
                // Provider name is everything before colon, event name is everything after.
                sampleProviderName = { sampleEventInfoName, static_cast<size_t>(sampleEventNameColon - sampleEventInfoName) };
                sampleEventName = sampleEventNameColon + 1;
            }
        }

        PerfByteReader const byteReader(fileBigEndian);

        eventInfoValid = false;

        (jsonFlags & EventFormatterJsonFlags_Name)
            ? AppendJsonMemberBegin(sb, 0, sampleEventName, 1)
            : AppendJsonValueBegin(sb, 1);
        sb.WriteJsonStructBegin(); // top-level

        if (metaFlags & EventFormatterMetaFlags_n)
        {
            AppendJsonMemberBegin(sb, 0, "n"sv, 1);
            sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
            AppendUcsJsonEscaped<SwapNo>(sb,
                reinterpret_cast<uint8_t const*>(sampleProviderName.data()),
                sampleProviderName.size(),
                1);
            sb.WriteUtf8ByteUnchecked(':'); // 1 extra byte reserved above.
            AppendUcsJsonEscaped<SwapNo>(sb,
                reinterpret_cast<uint8_t const*>(sampleEventName.data()),
                sampleEventName.size(),
                1);
            sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
        }

        if (sampleEventInfoMetadata)
        {
            auto const& meta = *sampleEventInfoMetadata;
            size_t const firstField = (metaFlags & EventFormatterMetaFlags_common)
                ? 0u
                : meta.CommonFieldCount();
            for (size_t iField = firstField; iField < meta.Fields().size(); iField += 1)
            {
                auto const fieldMeta = meta.Fields()[iField];
                auto const fieldData = fieldMeta.GetFieldBytes(
                    sampleEventInfo.raw_data,
                    sampleEventInfo.raw_data_size,
                    fileBigEndian);
                AppendSampleFieldAsJsonImpl(sb, fieldData.data(), fieldData.size(), fieldMeta, fileBigEndian, true);
            }
        }
        else if (sampleEventInfoSampleType & PERF_SAMPLE_RAW)
        {
            AppendJsonMemberBegin(sb, 0, "raw"sv, 0);
            AppendHexBytesVal(sb,
                sampleEventInfo.raw_data, sampleEventInfo.raw_data_size,
                true);
        }
    }

    if (0 != (metaFlags & ~EventFormatterMetaFlags_n))
    {
        AppendJsonMemberBegin(sb, 0, "meta"sv, 1);
        sb.WriteJsonStructBegin(); // meta

        if ((metaFlags & EventFormatterMetaFlags_time) && (sampleEventInfoSampleType & PERF_SAMPLE_TIME))
        {
            AppendJsonMemberBegin(sb, 0, "time"sv, 39); // "DATETIME.nnnnnnnnnZ" = 1 + 26 + 12
            if (sampleEventInfo.session_info->ClockOffsetKnown())
            {
                auto timeSpec = sampleEventInfo.session_info->TimeToRealTime(sampleEventInfo.time);
                sb.WriteUtf8ByteUnchecked('\"');
                sb.WriteDateTime(timeSpec.tv_sec);
                sb.WritePrintf(12, ".%09uZ\"", timeSpec.tv_nsec);
            }
            else
            {
                sb.WritePrintf(22, "%" PRIu64 ".%09u",
                    sampleEventInfo.time / 1000000000,
                    static_cast<unsigned>(sampleEventInfo.time % 1000000000));
            }
        }

        if ((metaFlags & EventFormatterMetaFlags_cpu) && (sampleEventInfoSampleType & PERF_SAMPLE_CPU))
        {
            AppendJsonMemberBegin(sb, 0, "cpu"sv, 10);
            sb.WriteNumber(10, sampleEventInfo.cpu);
        }

        if ((metaFlags & EventFormatterMetaFlags_pid) && (sampleEventInfoSampleType & PERF_SAMPLE_TID))
        {
            AppendJsonMemberBegin(sb, 0, "pid"sv, 10);
            sb.WriteNumber(10, sampleEventInfo.pid);
        }

        if ((metaFlags & EventFormatterMetaFlags_tid) && (sampleEventInfoSampleType & PERF_SAMPLE_TID))
        {
            AppendJsonMemberBegin(sb, 0, "tid"sv, 10);
            sb.WriteNumber(10, sampleEventInfo.tid);
        }

        if (eventInfoValid)
        {
            AppendMetaEventInfo(sb, metaFlags, eventInfo);
        }
        else
        {
            if ((metaFlags & EventFormatterMetaFlags_provider) && !sampleProviderName.empty())
            {
                AppendJsonMemberBegin(sb, 0, "provider"sv, 1);
                sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
                AppendUtf8JsonEscaped(sb, sampleProviderName, 1);
                sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
            }

            if ((metaFlags & EventFormatterMetaFlags_event) && !sampleEventName.empty())
            {
                AppendJsonMemberBegin(sb, 0, "event"sv, 1);
                sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
                AppendUtf8JsonEscaped(sb, sampleEventName, 1);
                sb.WriteUtf8ByteUnchecked('"'); // 1 extra byte reserved above.
            }
        }

        sb.EnsureRoom(4); // Room to end meta and top-level.
        sb.WriteJsonSpaceIfWanted();
        sb.WriteJsonStructEnd(); // meta
    }
    else
    {
        sb.EnsureRoom(2); // Room to end top-level.
    }

    sb.WriteJsonSpaceIfWanted();
    sb.WriteJsonStructEnd(); // top-level

Done:

    if (err == 0)
    {
        sb.Commit();
    }

    return err;
}

int
EventFormatter::AppendSampleFieldAsJson(
    std::string& dest,
    _In_reads_bytes_(fieldRawDataSize) void const* fieldRawData,
    size_t fieldRawDataSize,
    PerfFieldMetadata const& fieldMetadata,
    bool fileBigEndian,
    EventFormatterJsonFlags jsonFlags)
{
    StringBuilder sb(dest, jsonFlags);
    AppendSampleFieldAsJsonImpl(sb, fieldRawData, fieldRawDataSize, fieldMetadata, fileBigEndian,
        (jsonFlags & EventFormatterJsonFlags_Name));
    sb.Commit();
    return 0;
}

int
EventFormatter::AppendEventAsJsonAndMoveToEnd(
    std::string& dest,
    EventEnumerator& enumerator,
    EventFormatterJsonFlags jsonFlags,
    EventFormatterMetaFlags metaFlags)
{
    assert(EventEnumeratorState_BeforeFirstItem == enumerator.State());

    StringBuilder sb(dest, jsonFlags);
    auto const ei = enumerator.GetEventInfo();

    int err;

    (jsonFlags & EventFormatterJsonFlags_Name)
        ? AppendJsonMemberBegin(sb, 0, ei.Name, 1)
        : AppendJsonValueBegin(sb, 1);
    sb.WriteJsonStructBegin(); // top-level.

    if (metaFlags & EventFormatterMetaFlags_n)
    {
        AppendMetaN(sb, ei);
    }

    err = AppendItemAsJsonImpl(sb, enumerator, true);
    if (err == 0)
    {
        if (0 != (metaFlags & ~EventFormatterMetaFlags_n))
        {
            AppendJsonMemberBegin(sb, 0, "meta"sv, 1);
            sb.WriteJsonStructBegin(); // meta.

            AppendMetaEventInfo(sb, metaFlags, ei);

            sb.EnsureRoom(4); // Room to end meta and top-level.
            sb.WriteJsonSpaceIfWanted();
            sb.WriteJsonStructEnd(); // meta
        }
        else
        {
            sb.EnsureRoom(2); // Room to end top-level.
        }

        sb.WriteJsonSpaceIfWanted();
        sb.WriteJsonStructEnd(); // top-level
    }

    if (err == 0)
    {
        sb.Commit();
    }

    return err;
}

int
EventFormatter::AppendItemAsJsonAndMoveNextSibling(
    std::string& dest,
    EventEnumerator& enumerator,
    EventFormatterJsonFlags jsonFlags)
{
    StringBuilder sb(dest, jsonFlags);
    int const err = AppendItemAsJsonImpl(sb, enumerator, (jsonFlags & EventFormatterJsonFlags_Name));
    if (err == 0)
    {
        sb.Commit();
    }
    return err;
}

int
EventFormatter::AppendValue(
    std::string& dest,
    EventEnumerator const& enumerator)
{
    return AppendValue(dest, enumerator.GetItemInfo());
}

int
EventFormatter::AppendValue(
    std::string& dest,
    EventItemInfo const& valueItemInfo)
{
    return AppendValue(dest, valueItemInfo.ValueData, valueItemInfo.ValueSize,
        valueItemInfo.Encoding, valueItemInfo.Format, valueItemInfo.NeedByteSwap);
}

int
EventFormatter::AppendValue(
    std::string& dest,
    _In_reads_bytes_(valueSize) void const* valueData,
    uint32_t valueSize,
    event_field_encoding encoding,
    event_field_format format,
    bool needsByteSwap)
{
    StringBuilder sb(dest, EventFormatterJsonFlags_None);
    int const err = AppendValueImpl(sb, valueData, valueSize,
        encoding, format, needsByteSwap, false);
    if (err == 0)
    {
        sb.Commit();
    }
    return err;
}

int
EventFormatter::AppendValueAsJson(
    std::string& dest,
    _In_reads_bytes_(valueSize) void const* valueData,
    uint32_t valueSize,
    event_field_encoding encoding,
    event_field_format format,
    bool needsByteSwap,
    EventFormatterJsonFlags jsonFlags)
{
    StringBuilder sb(dest, jsonFlags);
    int const err = AppendValueImpl(sb, valueData, valueSize,
        encoding, format, needsByteSwap, true);
    if (err == 0)
    {
        sb.Commit();
    }
    return err;
}

void
EventFormatter::AppendUuid(
    std::string& dest,
    _In_reads_bytes_(16) uint8_t const* uuid)
{
    StringBuilder sb(dest, EventFormatterJsonFlags_None);
    sb.EnsureRoom(36);
    sb.WriteUuid(uuid);
    sb.Commit();
}
