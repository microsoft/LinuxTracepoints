#include <eventheader/EventHeaderDynamic.h>
#include <string>

struct EncodingName
{
    event_field_encoding encoding;
    char const* name;
};

static EncodingName const UniversalEncodings[] = {
    { event_field_encoding_string_length16_char8, "string"},
    { event_field_encoding_binary_length16_char8, "binary"},
};

struct FormatName
{
    event_field_format format;
    char const* name;
};

static FormatName const Formats[] = {
#define FORMAT(suffix) { event_field_format_##suffix, #suffix }
    FORMAT(default),
    FORMAT(unsigned_int),
    FORMAT(signed_int),
    FORMAT(hex_int),
    FORMAT(errno),
    FORMAT(pid),
    FORMAT(time),
    FORMAT(boolean),
    FORMAT(float),
    FORMAT(hex_bytes),
    FORMAT(string8),
    FORMAT(string_utf),
    FORMAT(string_utf_bom),
    FORMAT(string_xml),
    FORMAT(string_json),
    FORMAT(uuid),
    FORMAT(port),
    FORMAT(ip_address),
    FORMAT(ip_address_obsolete),
#undef FORMAT
};

uint8_t  const bigInt1 = 0xF0;
uint16_t const bigInt2 = 0xF0F1;
uint32_t const bigInt4 = 0xF0F1F2F3;
uint64_t const bigInt8 = 0xF0F1F2F3F4F5F6F7;
std::string_view const big0 = {};
std::string_view const big1 = { reinterpret_cast<char const*>(&bigInt1), sizeof(bigInt1) };
std::string_view const big2 = { reinterpret_cast<char const*>(&bigInt2), sizeof(bigInt2) };
std::string_view const big4 = { reinterpret_cast<char const*>(&bigInt4), sizeof(bigInt4) };
std::string_view const big8 = { reinterpret_cast<char const*>(&bigInt8), sizeof(bigInt8) };
std::string_view const big16 = "\xF0\xF1\xF2\xF3\xF4\xF5\xF6\xF7\xF8\xF9\xFA\xFB\xFC\xFD\xFE\xFF";

uint8_t  const oneInt1 = 1;
uint16_t const oneInt2 = 1;
uint32_t const oneInt4 = 1;
uint64_t const oneInt8 = 1;
std::string_view const one0 = {};
std::string_view const one1 = { reinterpret_cast<char const*>(&oneInt1), sizeof(oneInt1) };
std::string_view const one2 = { reinterpret_cast<char const*>(&oneInt2), sizeof(oneInt2) };
std::string_view const one4 = { reinterpret_cast<char const*>(&oneInt4), sizeof(oneInt4) };
std::string_view const one8 = { reinterpret_cast<char const*>(&oneInt8), sizeof(oneInt8) };
std::string_view const one16 = { "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", 16 };

bool
TestDynamic()
{
    ehd::EventBuilder b;
    ehd::Provider prov{ "TestProviderDyn" };
    auto const e = prov.RegisterSet(event_level_verbose, 1);
    if (!e)
    {
        return false;
    }

    for (auto fmt : Formats)
    {
        b.Reset(fmt.name)
            .AddBinary<char>("b0one", one0, fmt.format)
            .AddString<char>("s0one", one0, fmt.format)
            .AddBinary<char>("b0big", big0, fmt.format)
            .AddString<char>("s0big", big0, fmt.format)
            .Write(*e);
        b.Reset(fmt.name)
            .AddBinary<char>("b1one", one1, fmt.format)
            .AddString<char>("s1one", one1, fmt.format)
            .AddBinary<char>("b1big", big1, fmt.format)
            .AddString<char>("s1big", big1, fmt.format)
            .Write(*e);
        b.Reset(fmt.name)
            .AddBinary<char>("b2one", one2, fmt.format)
            .AddString<char>("s2one", one2, fmt.format)
            .AddBinary<char>("b2big", big2, fmt.format)
            .AddString<char>("s2big", big2, fmt.format)
            .Write(*e);
        b.Reset(fmt.name)
            .AddBinary<char>("b4one", one4, fmt.format)
            .AddString<char>("s4one", one4, fmt.format)
            .AddBinary<char>("b4big", big4, fmt.format)
            .AddString<char>("s4big", big4, fmt.format)
            .Write(*e);
        b.Reset(fmt.name)
            .AddBinary<char>("b8one", one8, fmt.format)
            .AddString<char>("s8one", one8, fmt.format)
            .AddBinary<char>("b8big", big8, fmt.format)
            .AddString<char>("s8big", big8, fmt.format)
            .Write(*e);
        b.Reset(fmt.name)
            .AddBinary<char>("b16one", one16, fmt.format)
            .AddString<char>("s16one", one16, fmt.format)
            .AddBinary<char>("b16big", big16, fmt.format)
            .AddString<char>("s16big", big16, fmt.format)
            .Write(*e);
    }

    return true;
}
