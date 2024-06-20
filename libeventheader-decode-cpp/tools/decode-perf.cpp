// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <tracepoint/PerfEventInfo.h>
#include <tracepoint/PerfDataFile.h>
#include <tracepoint/PerfEventAbi.h>
#include <eventheader/EventFormatter.h>

#include <string.h>
#include <map>
#include <memory>
#include <vector>

#ifdef _WIN32

#include <io.h>
#define strerror_r(errnum, buf, buflen) (strerror_s(buf, buflen, errnum), buf)
#define fopen(filename, mode)           _fsopen(filename, mode, _SH_DENYWR)
#define isatty(fd)                      _isatty(fd)
#define fileno(file)                    _fileno(file)

#else // !_WIN32

#include <unistd.h>

#endif // _WIN32

#define PROGRAM_NAME "decode-perf"

using namespace eventheader_decode;
using namespace tracepoint_decode;

static char const* const UsageCommon = R"(
Usage: )" PROGRAM_NAME R"( [options...] PerfDataFiles...
)";

// Usage error: stderr += UsageCommon + UsageShort.
static char const* const UsageShort = R"(
Try ")" PROGRAM_NAME R"( --help" for more information.
)";

// -h or --help: stdout += UsageCommon + UsageLong.
static char const* const UsageLong = R"(
Converts perf.data files to JSON.

Options:

-o, --output <file> Set the output filename. The default is stdout.

-h, --help          Show this help message and exit.
)";

struct fclose_deleter
{
    void operator()(FILE* f) const noexcept
    {
        if (f != stdout)
        {
            fclose(f);
        }
    }
};

static bool
FlushEvents(
    FILE* output,
    std::multimap<uint64_t, std::string>& events,
    bool comma) noexcept
{
    for (auto const& pair : events)
    {
        fputs(comma ? ",\n " : "\n ", output);
        comma = true;
        fputs(pair.second.c_str(), output);
    }

    events.clear();
    return comma;
}

int main(int argc, char* argv[])
{
    int err;

    try
    {
        std::unique_ptr<FILE, fclose_deleter> output;
        std::vector<char const*> inputNames;
        char const* outputName = nullptr;
        bool showHelp = false;
        bool usageError = false;

        for (int argi = 1; argi < argc; argi += 1)
        {
            auto const* const arg = argv[argi];
            if (arg[0] != '-')
            {
                inputNames.push_back(arg);
            }
            else if (arg[1] != '-')
            {
                auto const flags = &arg[1];
                for (unsigned flagsPos = 0; flags[flagsPos] != '\0'; flagsPos += 1)
                {
                    auto const flag = flags[flagsPos];
                    switch (flag)
                    {
                    case 'o':
                        argi += 1;
                        if (argi < argc)
                        {
                            outputName = argv[argi];
                        }
                        else
                        {
                            fprintf(stderr, "error: missing filename for flag -o.\n");
                            usageError = true;
                        }
                        break;
                    case 'h':
                        showHelp = true;
                        break;
                    default:
                        fprintf(stderr, "error: invalid flag -%c.\n",
                            flag);
                        usageError = true;
                        break;
                    }
                }
            }
            else
            {
                auto const flag = &arg[2];
                if (0 == strcmp(flag, "output"))
                {
                    argi += 1;
                    if (argi < argc)
                    {
                        outputName = argv[argi];
                    }
                    else
                    {
                        fprintf(stderr, "error: missing filename for flag --output.\n");
                        usageError = true;
                    }
                }
                else if (0 == strcmp(flag, "help"))
                {
                    showHelp = true;
                }
                else
                {
                    fprintf(stderr, "error: invalid flag \"--%s\".\n",
                        flag);
                    usageError = true;
                }
            }
        }

        if (showHelp || usageError)
        {
            auto helpOut = showHelp ? stdout : stderr;
            fputs(UsageCommon, helpOut);
            fputs(showHelp ? UsageLong : UsageShort, helpOut);
            err = EINVAL;
            goto Done;
        }
        else if (inputNames.empty())
        {
            fprintf(stderr, "error: no input files specified, exiting.\n");
            err = EINVAL;
            goto Done;
        }

        if (outputName == nullptr)
        {
            output.reset(stdout);
        }
        else
        {
            errno = 0;
            output.reset(fopen(outputName, "w"));
            if (output == nullptr)
            {
                err = errno;
                if (err == 0)
                {
                    err = -1;
                }
                fprintf(stderr, "error: unable to open output file \"%s\".\n", outputName);
                goto Done;
            }
        }

        if (!isatty(fileno(output.get())))
        {
            // Output is UTF-8. Emit a BOM.
            fputs("\xEF\xBB\xBF", output.get());
        }

        fputs("{\n", output.get());

        std::string filenameJson;
        std::multimap<uint64_t, std::string> events;
        EventFormatter formatter;
        PerfDataFile file;
        bool comma = false;

        for (auto inputName : inputNames)
        {
            bool const isStdin = inputName[0] == '\0';
            char const* const filename = isStdin ? "stdin" : inputName;

            filenameJson.clear();
            formatter.AppendValueAsJson(
                filenameJson,
                filename,
                static_cast<unsigned>(strlen(filename)),
                event_field_encoding_zstring_char8,
                event_field_format_default, false);

            fprintf(output.get(), "%s%s: [",
                comma ? ",\n" : "",
                filenameJson.c_str());
            comma = false;

            // CodeQL [SM01937] Users should be able to specify the output file path.
            err = isStdin ? file.OpenStdin() : file.Open(filename);
            if (err != 0)
            {
                char errBuf[80];
                fprintf(stderr, "\n- Open(\"%s\") error %d: \"%s\"\n",
                    filename,
                    err,
                    strerror_r(err, errBuf, sizeof(errBuf)));
            }
            else for (;;)
            {
                perf_event_header const* pHeader;
                err = file.ReadEvent(&pHeader);
                if (!pHeader)
                {
                    if (err)
                    {
                        fprintf(stderr, "\n- ReadEvent error %d.\n", err);
                    }
                    break;
                }

                if (pHeader->type != PERF_RECORD_SAMPLE)
                {
                    if (pHeader->type == PERF_RECORD_FINISHED_ROUND)
                    {
                        comma = FlushEvents(output.get(), events, comma);
                    }

                    continue; // Only interested in sample events for now.
                }

                PerfSampleEventInfo sampleEventInfo;
                err = file.GetSampleEventInfo(pHeader, &sampleEventInfo);
                if (err)
                {
                    fprintf(stderr, "\n- GetSampleEventInfo error %d.\n", err);
                    continue;
                }

                // Events are returned out-of-order and need to be sorted. Use a map to
                // put them into timestamp order. Flush the map at the end of each round.
                auto it = events.emplace(
                    (sampleEventInfo.SampleType() & PERF_SAMPLE_TIME) ? sampleEventInfo.time : 0u,
                    std::string());
                err = formatter.AppendSampleAsJson(
                    it->second,
                    sampleEventInfo,
                    file.FileBigEndian(),
                    static_cast<EventFormatterJsonFlags>(
                        EventFormatterJsonFlags_Space |
                        EventFormatterJsonFlags_FieldTag));
                if (err)
                {
                    fprintf(stderr, "\n- Format error %d.\n", err);
                }
            }

            comma = FlushEvents(output.get(), events, comma);

            fputs(" ]", output.get());
            comma = true;
        }

        fprintf(output.get(), "\n}\n");
        err = 0;
    }
    catch (std::exception const& ex)
    {
        fprintf(stderr, "\nException: %s\n", ex.what());
        err = 1;
    }

Done:

    return err;
}
