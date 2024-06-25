// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*
Generates a .json.actual file for the .dat file.
Verifies that the resulting .json.actual file is the same as the .json.expected file.
*/

#include <eventheader/EventEnumerator.h>
#include <eventheader/EventFormatter.h>
#include <stdio.h>
#include <string.h>
#include <exception>
#include <memory>
#include <string>

#ifdef _WIN32
#include <share.h>
#define fopen(filename, mode) _fsopen(filename, mode, _SH_DENYWR)
#define strerror_r(errnum, buf, buflen) (strerror_s(buf, buflen, errnum), buf)
#define le32toh(x) x
#endif // _WIN32

#define DAT_NAME "EventHeaderInterceptorLE64.dat"

using namespace eventheader_decode;

struct fcloseDelete
{
    void operator()(FILE* file) const noexcept
    {
        fclose(file);
    }
};

using unique_file = std::unique_ptr<FILE, fcloseDelete>;

static std::string
LoadFile(char const* filename)
{
    unique_file file{ fopen(filename, "rb") };
    if (!file)
    {
        fprintf(stdout, "Failed to open file: %s\n", filename);
        throw std::exception();
    }

    fseek(file.get(), 0, SEEK_END);
    size_t size = ftell(file.get());
    fseek(file.get(), 0, SEEK_SET);

    std::string result(size, '\0');
    if (fread(&result[0], 1, size, file.get()) != size)
    {
        fprintf(stdout, "Failed to read file: %s\n", filename);
        throw std::exception();
    }

    return result;
}

static std::string
MakeJsonName(char const* baseDir, char const* suffix)
{
#ifdef _WIN32
#define PLATFORM "windows"
#else
#define PLATFORM "linux"
#endif

    std::string result = baseDir;
    result += "/" DAT_NAME "." PLATFORM ".json";
    result += suffix;
    return result;
}

int
main(int argc, char* argv[])
{
    // If an argument is provided, use it as the base directory. Otherwise, use ".".
    auto const baseDir = argc > 1 ? argv[1] : ".";

    try
    {
        std::string const actualName = MakeJsonName(baseDir, ".actual");
        std::string const expectedName = MakeJsonName(baseDir, ".expected");
        std::string const datName = std::string(baseDir) + "/" DAT_NAME;

        std::string const dat = LoadFile(datName.c_str());
        size_t datPos = 0;

        EventEnumerator enumerator;
        EventFormatter formatter;
        bool comma = false;

        std::string actualJson;
        actualJson += "\xEF\xBB\xBF\n\"" DAT_NAME "\": [";

        for (;;)
        {
            uint32_t recordSize;
            if (dat.size() - datPos < sizeof(recordSize))
            {
                if (datPos != dat.size())
                {
                    fprintf(stdout, "\n- fread early eof (asked for %u, got %u)",
                        static_cast<unsigned>(sizeof(recordSize)),
                        static_cast<unsigned>(dat.size() - datPos));
                    throw std::exception();
                }
                break;
            }

            memcpy(&recordSize, dat.data() + datPos, sizeof(recordSize));
            recordSize = le32toh(recordSize);
            datPos += sizeof(recordSize);

            if (recordSize <= sizeof(recordSize))
            {
                fprintf(stdout, "\n- Unexpected recordSize %u", recordSize);
                throw std::exception();
            }

            recordSize -= sizeof(recordSize); // File's recordSize includes itself.

            if (dat.size() - datPos < recordSize)
            {
                fprintf(stdout, "\n- fread early eof (asked for %u, got %u)",
                    static_cast<unsigned>(sizeof(recordSize)),
                    static_cast<unsigned>(dat.size() - datPos));
                throw std::exception();
            }

            auto const nameSize = static_cast<uint32_t>(strnlen(dat.data() + datPos, recordSize));
            if (nameSize == recordSize)
            {
                fprintf(stdout, "\n- TracepointName not nul-terminated.");
                continue;
            }

            actualJson += comma ? ",\n " : "\n ";
            comma = true;

            if (!enumerator.StartEvent(
                dat.data() + datPos, // tracepoint name
                nameSize, // tracepoint name length
                dat.data() + datPos + nameSize + 1, // event data
                recordSize - nameSize - 1))   // event data length
            {
                fprintf(stdout, "\n- StartEvent error %d.", enumerator.LastError());
            }
            else
            {
                int err = formatter.AppendEventAsJsonAndMoveToEnd(
                    actualJson, enumerator, static_cast<EventFormatterJsonFlags>(
                        EventFormatterJsonFlags_Space |
                        EventFormatterJsonFlags_FieldTag));
                if (err != 0)
                {
                    fprintf(stdout, "\n- AppendEvent error.");
                }
            }

            datPos += recordSize;
        }

        actualJson += " ]\n";

        {
            unique_file actualFile{ fopen(actualName.c_str(), "w") };
            if (!actualFile)
            {
                fprintf(stdout, "Failed to open file: %s\n", actualName.c_str());
                throw std::exception();
            }

            size_t written = fwrite(actualJson.c_str(), 1, actualJson.size(), actualFile.get());
            if (written != actualJson.size())
            {
                fprintf(stdout, "Failed to write file: %s\n", actualName.c_str());
                throw std::exception();
            }
        }

        std::string expectedJson;
        for (char ch : LoadFile(expectedName.c_str()))
        {
            if (ch != '\r')
            {
                expectedJson.push_back(ch);
            }
        }

        if (actualJson != expectedJson)
        {
            fprintf(stdout, "ERROR: %s != %s, %u/%u\n",
                actualName.c_str(), expectedName.c_str(),
                (unsigned)actualJson.size(), (unsigned)expectedJson.size());
            return 1;
        }
    }
    catch (std::exception const& ex)
    {
        fprintf(stdout, "ERROR: %s\n", ex.what());
        return 1;
    }

    return 0;
}
