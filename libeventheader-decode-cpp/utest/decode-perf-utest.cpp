// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*
Generates a .json.actual file for the .perf file.
Verifies that the resulting .json.actual file is the same as the .json.expected file.
*/

#include <tracepoint/PerfDataFile.h>
#include <tracepoint/PerfEventAbi.h>
#include <tracepoint/PerfEventInfo.h>
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

#define PERF_NAME "EventHeaderPerf.data"

using namespace eventheader_decode;
using namespace tracepoint_decode;

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
MakeJsonName(char const* perfName, char const* suffix)
{
#ifdef _WIN32
#define PLATFORM "windows"
#else
#define PLATFORM "linux"
#endif

    std::string result = perfName;
    result += "." PLATFORM ".json";
    result += suffix;
    return result;
}

int
main(int argc, char* argv[])
{
    if (argc <= 1)
    {
        fprintf(stdout, "Usage: %s <perf-file>\n", argv[0]);
        return 1;
    }

    try
    {
        int err;

        auto const perfName = argv[1];
        std::string const actualName = MakeJsonName(perfName, ".actual");
        std::string const expectedName = MakeJsonName(perfName, ".expected");

        PerfDataFile reader;
        PerfSampleEventInfo sampleEventInfo;

        err = reader.Open(perfName);
        if (err != 0)
        {
            fprintf(stdout, "Failed to open file %u: %s\n", err, perfName);
            throw std::exception();
        }

        EventEnumerator enumerator;
        EventFormatter formatter;
        bool comma = false;

        std::string actualJson;
        actualJson += "\xEF\xBB\xBF\n\"" PERF_NAME "\": [";

        for (;;)
        {
            perf_event_header const* header;
            err = reader.ReadEvent(&header);
            if (header == nullptr)
            {
                if (err != 0)
                {
                    fprintf(stdout, "\n- ReadEvent error %d.", err);
                    throw std::exception();
                }
                break;
            }

            if (header->type != PERF_RECORD_SAMPLE)
            {
                continue;
            }

            err = reader.GetSampleEventInfo(header, &sampleEventInfo);
            if (err != 0)
            {
                fprintf(stdout, "\n- GetSampleEventInfo error %d.", err);
                throw std::exception();
            }

            actualJson += comma ? ",\n " : "\n ";
            comma = true;

            err = formatter.AppendSampleAsJson(actualJson, sampleEventInfo, reader.FileBigEndian());
            if (err != 0)
            {
                fprintf(stdout, "\n- AppendSampleAsJson error %d.", err);
                throw std::exception();
            }
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
