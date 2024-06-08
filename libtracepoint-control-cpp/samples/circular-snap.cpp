// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*
Demonstrates creating a circular session, adding events to it, and when the
session needs to be flushed:

- Writing ALL events into a perf.dat file.
- Writing NEW events to stdout (formatted as JSON).

This shows how to start and configure a session, flush a session to a file with
precise control over the events (when you need more precise control than
`SavePerfDataFile` or `FlushToWriter` allow), filter new events from the
circular buffer, and format events to JSON for stdout.
*/

#include <tracepoint/PerfDataFileWriter.h>
#include <tracepoint/PerfEventAbi.h>
#include <tracepoint/TracepointSession.h>
#include <tracepoint/TracepointSpec.h>
#include <eventheader/EventFormatter.h>

#include <stdio.h>
#include <assert.h>
#include <fstream>
#include <string>
#include <vector>

using namespace eventheader_decode;
using namespace tracepoint_control;
using namespace tracepoint_decode;

class CircularSession
{
public:

    explicit
    CircularSession(unsigned perCpuBufferSize)
        : _tracepointCache()
        , _tracepointSession(_tracepointCache, TracepointSessionMode::Circular, perCpuBufferSize)
        , _lastWritten0()
        , _lastWritten1()
        , _lastWrittenSelect()
    {
        fprintf(stderr,
            "TracepointSessionOpen BufferSize=0x%X\n",
            perCpuBufferSize);
        return;
    }

    void
    AddEvent(std::string_view tracepointSpecText)
    {
        int error;

        // Use standard tracepoint-spec parsing for the text.
        // For details on the syntax, see the help text for the tracepoint-collect tool.
        TracepointSpec spec(tracepointSpecText); // Parse the text.
        switch (spec.Kind)
        {
        default:

            // line is a syntax error. Specific syntax violation is given by Kind.
            fprintf(stderr,
                "TracepointSpecError warning Kind=%u Spec=\"%.*s\"\n",
                static_cast<unsigned>(spec.Kind),
                (unsigned)spec.Trimmed.size(), spec.Trimmed.data());
            return;

        case TracepointSpecKind::Empty:

            // line is empty or a comment.
            return;

        case TracepointSpecKind::Identifier:

            // line is the ID of a previously-registered tracepoint.
            // Make sure we can find it. (EnableTracepoint will do this automatically, but doing it
            // separately helps us know which step failed if something goes wrong.)
            error = _tracepointCache.AddFromSystem(TracepointName(spec.SystemName, spec.EventName));
            if (error != 0 && error != EEXIST)
            {
                fprintf(stderr,
                    "AddFromSystemError warning errno=%u Spec=\"%.*s\"\n",
                    error,
                    (unsigned)spec.Trimmed.size(), spec.Trimmed.data());
                return;
            }
            break;

        case TracepointSpecKind::Definition:
        case TracepointSpecKind::EventHeaderDefinition:

            // line is a tracepoint definition.
            // Pre-register it so we can enable it even if it isn't registered yet.
            error = _tracepointCache.PreregisterTracepointDefinition(spec);
            if (error != 0 && error != EEXIST)
            {
                fprintf(stderr,
                    "PreregisterError warning errno=%u Spec=\"%.*s\"\n",
                    error,
                    (unsigned)spec.Trimmed.size(), spec.Trimmed.data());
                return;
            }
            break;
        }

        error = _tracepointSession.EnableTracepoint(TracepointName(spec.SystemName, spec.EventName));
        if (error != 0)
        {
            fprintf(stderr,
                "EnableTracepointError warning errno=%u Spec=\"%.*s\"\n",
                error,
                (unsigned)spec.Trimmed.size(), spec.Trimmed.data());
            return;
        }

        fprintf(stderr,
            "EnableTracepoint Spec=\"%.*s\"\n",
            (unsigned)spec.Trimmed.size(), spec.Trimmed.data());
    }

    void
    AddEventsFromFile(char const* tracepointListFilePath)
    {
        std::ifstream tracepointListFile(tracepointListFilePath);
        if (!tracepointListFile)
        {
            fprintf(stderr,
                "TracepointListFileOpenFailed warning Path=\"%s\"\n",
                tracepointListFilePath);
            return;
        }

        std::string line;
        while (std::getline(tracepointListFile, line))
        {
            AddEvent(line);
        }

        if (!tracepointListFile)
        {
            fprintf(stderr,
                "TracepointListFileReadFailed Path=\"%s\"\n",
                tracepointListFilePath);
        }
    }

    std::error_code
    SnapTrace(char const* perfDataPath)
    {
        int err;

        _lastWritten0.resize(_tracepointSession.BufferCount());
        _lastWritten1.resize(_tracepointSession.BufferCount());

        struct EnumState
        {
            PerfDataFileWriter writer;
            TracepointTimestampRange writtenRange{}; // Start with an invalid range.
            std::string eventText;
            EventEnumerator enumerator;
            EventFormatter formatter;
        } state;

        // Create the file.

        err = state.writer.Create(perfDataPath);
        if (err != 0)
        {
            fprintf(stderr,
                "SnapTraceOpenError %u Path=\"%s\"\n",
                err,
                perfDataPath);
            return std::error_code(err, std::system_category());
        }

        // Mark the end of the "synthetic events" section of the perf.dat file (currently empty).

        err = state.writer.WriteFinishedInit();
        if (err != 0)
        {
            fprintf(stderr,
                "WriteFinishedInit Error=%u\n",
                err);
            return std::error_code(err, std::system_category());
        }

        // Process events from the circular buffer.
        // For efficiency, use the Unordered enumerator. This pauses the trace
        // for a shorter amount of time, and it saves CPU/memory during the processing.
        // perf.dat file is not guaranteed to be in order, and we'll assume the printed
        // log is similar.

        err = _tracepointSession.EnumerateSampleEventsUnordered(
            [this, &state](PerfSampleEventInfo const& eventInfo)
            {
                int error;

                auto const metadata = eventInfo.event_desc->metadata;
                if (metadata == nullptr)
                {
                    // Unexpected - event with no metadata.
                    assert(false);
                    return 0;
                }

                if (_lastWritten0.size() <= eventInfo.cpu)
                {
                    // Unexpected - CPU count != buffer count.
                    assert(false);
                    _lastWritten0.resize(eventInfo.cpu + 1);
                    _lastWritten1.resize(eventInfo.cpu + 1);
                }

                // Update the range of time covered by the events in this snap.

                LastWritten const& prevSnapLast = (_lastWrittenSelect ? _lastWritten0 : _lastWritten1)[eventInfo.cpu];
                LastWritten& currSnapLast = (_lastWrittenSelect ? _lastWritten1 : _lastWritten0)[eventInfo.cpu];

                state.writtenRange.First = std::min(state.writtenRange.First, eventInfo.time);
                state.writtenRange.Last = std::max(state.writtenRange.Last, eventInfo.time);
                if (currSnapLast.time < eventInfo.time)
                {
                    currSnapLast.time = eventInfo.time;
                    currSnapLast.header = eventInfo.header;
                }

                // If we didn't print this event in a previous snap, print it now.

                if (prevSnapLast.time < eventInfo.time ||
                    (prevSnapLast.time == eventInfo.time && prevSnapLast.header != eventInfo.header))
                {
                    auto timeSpec = eventInfo.session_info->TimeToRealTime(eventInfo.time);
                    char timestamp[sizeof("-2000000000-06-08T00:49:55.000000000Z")];
                    struct tm tm;
                    if (!gmtime_r(&timeSpec.tv_sec, &tm))
                    {
                        // Time out of range (unexpected). Format as "seconds.nanoseconds".
                        snprintf(timestamp, sizeof(timestamp), "%lld.%09u",
                            (long long)timeSpec.tv_sec,
                            timeSpec.tv_nsec);
                    }
                    else
                    {
                        // Time in range (expected). Format as "yyyy-mm-ddThh-mm-ss.nanosecondsZ".
                        unsigned timestampLen = snprintf(timestamp, sizeof(timestamp), "%04d-%02u-%02uT%02u:%02u:%02u.%09u",
                            tm.tm_year + 1900,
                            tm.tm_mon + 1,
                            tm.tm_mday,
                            tm.tm_hour,
                            tm.tm_min,
                            tm.tm_sec,
                            timeSpec.tv_nsec);
                        if (timestampLen > sizeof(timestamp) - 2)
                        {
                            // Unexpected - format error or truncation.
                            assert(false);
                            timestampLen = sizeof(timestamp) - 2;
                        }

                        // Remove trailing zeros.
                        // Stop before the last digit, i.e. write "12:59:59.0Z" instead of "12:59:59.Z".
                        unsigned lastSignificantDigit = timestampLen;
                        while (lastSignificantDigit > sizeof("9999-99-99T99:99:99.") &&
                            timestamp[lastSignificantDigit - 1] == '0')
                        {
                            lastSignificantDigit -= 1;
                        }

                        timestamp[lastSignificantDigit] = 'Z';
                        timestamp[lastSignificantDigit + 1] = '\0';
                    }

                    auto const BigEndian = __BYTE_ORDER == __BIG_ENDIAN;
                    auto const JsonFlags = EventFormatterJsonFlags_FieldTag;
                    auto const MetaFlags = static_cast<EventFormatterMetaFlags>(
                        (EventFormatterMetaFlags_Default | EventFormatterMetaFlags_options)
                        & ~(EventFormatterMetaFlags_n | EventFormatterMetaFlags_time));
                    std::string_view namePart1;
                    std::string_view namePart2;

                    state.eventText.clear();
                    auto const metadataName = metadata->Name();
                    auto const commonFieldsSize = metadata->CommonFieldsSize();
                    if (metadata->Kind() == PerfEventKind::EventHeader &&
                        eventInfo.raw_data_size > commonFieldsSize &&
                        state.enumerator.StartEvent(
                            metadataName.data(),
                            metadataName.size(),
                            static_cast<char const*>(eventInfo.raw_data) + metadata->CommonFieldsSize(),
                            eventInfo.raw_data_size - commonFieldsSize))
                    {
                        auto ei = state.enumerator.GetEventInfo();
                        namePart1 = { ei.TracepointName, ei.ProviderNameLength };
                        namePart2 = ei.Name;
                        error = state.formatter.AppendEventAsJsonAndMoveToEnd(state.eventText, state.enumerator, JsonFlags, MetaFlags);
                    }
                    else
                    {
                        namePart1 = metadata->SystemName();
                        namePart2 = metadataName;
                        error = state.formatter.AppendSampleAsJson(state.eventText, eventInfo, BigEndian, JsonFlags, MetaFlags);
                    }
                    if (error == 0)
                    {
                        printf("NAME=%.*s:%.*s\n",
                            (unsigned)namePart1.size(), namePart1.data(),
                            (unsigned)namePart2.size(), namePart2.data());
                        printf("TIME=%s\n", timestamp);
                        printf("TEXT=%s\n\n", state.eventText.c_str());
                    }
                }

                // Write the event to the file.

                error = state.writer.WriteEventData(eventInfo.header, eventInfo.header->size);
                if (error != 0)
                {
                    return error;
                }

                // Track event's metadata in the file if not already there.

                error = state.writer.AddTracepointEventDesc(*eventInfo.event_desc);
                if (error != EEXIST && error != 0)
                {
                    return error;
                }

                return 0;
            });
        if (err != 0)
        {
            fprintf(stderr,
                "Enumerate Error=%u\n",
                err);
            return std::error_code(err, std::system_category());
        }

        bool timesValid = state.writtenRange.First <= state.writtenRange.Last;

        // Write system information headers:

        err = _tracepointSession.SetWriterHeaders(state.writer, timesValid ? &state.writtenRange : nullptr);
        if (err != 0)
        {
            fprintf(stderr,
                "SetWriterHeaders Error=%u\n",
                err);
            return std::error_code(err, std::system_category());
        }

        // Flush to disk:

        err = state.writer.FinalizeAndClose();
        if (err != 0)
        {
            fprintf(stderr,
                "FinalizeAndClose Error=%u\n",
                err);
            return std::error_code(err, std::system_category());
        }

        _lastWrittenSelect = !_lastWrittenSelect;

        fprintf(stderr, "Snap succeeded\n");
        return std::error_code{};
    }

private:

    struct LastWritten
    {
        uint64_t time;

        // Heuristic: If we see the same timestamp again, it's probably the same
        // event, but it could be a different event. If it has both the same timestamp
        // and is at the same address in the buffer, it's almost certainly the same
        // event.
        perf_event_header const* header;
    };

    TracepointCache _tracepointCache;
    TracepointSession _tracepointSession;
    std::vector<LastWritten> _lastWritten0;
    std::vector<LastWritten> _lastWritten1;
    bool _lastWrittenSelect; // determines which _lastWritten we read and which we write.
};

static int
Usage()
{
    fprintf(stdout,
        "Usage: circular-snap [TracepointSpec | -f TracepointSpecFile.txt]...\n");
    return 1;
}

int
main(int argc, char* argv[])
{
    if (argc <= 1)
    {
        return Usage();
    }

    try
    {
        CircularSession session(0x1000);
        for (int argi = 1; argi < argc; argi += 1)
        {
            auto const arg = argv[argi];
            if (arg[0] == '-')
            {
                switch (arg[1])
                {
                case 'f':

                    argi += 1;
                    if (argi >= argc)
                    {
                        return Usage();
                    }

                    session.AddEventsFromFile(argv[argi]);
                    break;

                default:

                    return Usage();
                }
            }
            else
            {
                session.AddEvent(arg);
            }
        }

        for (unsigned i = 0;; i += 1)
        {
            fprintf(stderr, "\nPress enter to snap, x + enter to exit...\n");
            char ch = (char)getchar();
            if (ch == 'x' || ch == 'X')
            {
                break;
            }

            while (ch != '\n')
            {
                ch = (char)getchar();
            }

            char outFileName[256];
            snprintf(outFileName, sizeof(outFileName), "perf.%u.dat", i);

            auto error = session.SnapTrace(outFileName);
            fprintf(stderr, "SnapTrace(%s) = %u\n", outFileName, error.value());
        }
    }
    catch (std::exception const& ex)
    {
        fprintf(stderr, "\nException: %s\n", ex.what());
        return 1;
    }

    return 0;
}
