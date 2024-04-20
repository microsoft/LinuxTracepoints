// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*
Simple tool for collecting tracepoints into perf.data files.
*/

#include <tracepoint/TracepointSession.h>
#include <tracepoint/TracepointSpec.h>
#include <tracepoint/PerfDataFileWriter.h>
#include <assert.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <unistd.h>

#include <vector>

using namespace std::string_view_literals;
using namespace tracepoint_control;
using namespace tracepoint_decode;

struct Tracepoint
{
    std::vector<char> line;
    TracepointSpec spec;

    Tracepoint(std::vector<char>&& _line, TracepointSpec const& _spec)
        : line(std::move(_line))
        , spec(_spec)
    {
        return;
    }
};

struct Options
{
    bool verbose = false;
};

#define PROGRAM_NAME "tracepoint-collect"
#define EXIT_SIGNALS      SIGTERM, SIGINT
#define EXIT_SIGNALS_STR "SIGTERM, SIGINT"

static char const* const UsageCommon = R"(
Usage: )" PROGRAM_NAME R"( [options...] TracepointSpec...
)";

// Usage error: stderr += UsageCommon + UsageShort.
static char const* const UsageShort = R"(
Try ")" PROGRAM_NAME R"( --help" for more information.
)";

// -h or --help: stdout += UsageCommon + UsageLong.
static char const* const UsageLong = R"(
Collects tracepoint events and saves them to a perf.data file. Collection
runs until SIGTERM or SIGINT is received.

Requires privileges, typically the CAP_PERFMON capability plus read access to
/sys/kernel/tracing. Pre-registration of a tracepoint requires write access to
/sys/kernel/tracing/user_events_data.

Options:

-b, --buffersize <size>
                    Set the size of each buffer, in kilobytes. There will be
                    one buffer per CPU. The default size is 128.

-c, --circular      Use circular trace mode. Events will be collected in
                    circular buffers (new events overwrite old) until the
                    signal is received, at which point the output file will be
                    created and the buffer contents will be written to the
                    file.

-C, --realtime      Use realtime trace mode (default). File will be created
                    immediately and events will be written to the file as they
                    are received until the signal is received.

-i, --input <file>  Read additional TracepointSpecs from <file>. Each line in
                    the file is treated as a TracepointSpec. Empty lines and
                    lines starting with '#' are ignored.

-o, --output <file> Set the output filename. The default is "./perf.data".

-v, --verbose       Show diagnostic output.

-h, --help          Show this help message and exit.

A TracepointSpec is one of the following:

* If the tracepoint is a normal user_event that may not already exist, use the
  full user_event definition, "SystemName:EventName Fields...", e.g.
  "user_events:MyEvent u32 MyField1; struct MyStruct2 MyField2 20". If the
  tracepoint does not already exist, it will be registered so that it can be
  added to the trace session.

  You may omit the SystemName if it is "user_events", e.g.
  "MyEvent u32 MyField1;".

  For an event with no fields, use " ;" for the fields, e.g.
  "MySimpleEvent ;".

* If the tracepoint is an EventHeader user_event that may not already exist,
  use the EventHeader identity, "SystemName:ProviderName_Suffix", e.g.
  "user_events:MyProvider_L5K1". If the tracepoint does not already exist, it
  will be registered so that it can be added to the trace session.

  You may omit the SystemName if it is "user_events", e.g. "MyProvider_L5K1".

* If the tracepoint is known to already be registered (e.g. a kernel event),
  use the tracepoint identity with a leading colon, ":SystemName:EventName",
  e.g. ":sched:sched_switch". If the tracepoint does not already exist, it
  will not be added to the trace session.

  You may omit the SystemName if it is "user_events", e.g.
  ":MyUserEventThatIsAlreadyRegistered".

See https://docs.kernel.org/trace/user_events.html#command-format for details
on the user_events definition syntax.
)";

// fprintf(stderr, format, args...).
static void
PrintStderr(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    fputs(PROGRAM_NAME ": ", stderr);
    vfprintf(stderr, format, args);
    va_end(args);
}

// if (condition) fprintf(stderr, format, args...).
static void
PrintStderrIf(bool condition, const char* format, ...)
{
    if (condition)
    {
        va_list args;
        va_start(args, format);
        fputs(PROGRAM_NAME ": ", stderr);
        vfprintf(stderr, format, args);
        va_end(args);
    }
}

static void
PushFrontDef(Options const& o, std::vector<Tracepoint>& tracepoints, std::string_view line)
{
    std::vector<char> lineCopy(line.begin(), line.end());
    auto const spec = TracepointSpec({ lineCopy.data(), lineCopy.size() });
    switch (spec.Kind)
    {
    case TracepointSpecKind::Empty:
        break;
    case TracepointSpecKind::Identifier:
        PrintStderrIf(o.verbose, "verbose: add identifier \"%.*s:%.*s\"\n",
            (unsigned)spec.SystemName.size(), spec.SystemName.data(),
            (unsigned)spec.EventName.size(), spec.EventName.data());
        tracepoints.emplace_back(std::move(lineCopy), spec);
        break;
    case TracepointSpecKind::Definition:
        if (spec.SystemName != UserEventsSystemName)
        {
            PrintStderr("error: definition system name \"%.*s\" must be 'user_events': \"%.*s\"\n",
                (unsigned)spec.SystemName.size(), spec.SystemName.data(),
                (unsigned)line.size(), line.data());
        }
        else
        {
            PrintStderrIf(o.verbose, "verbose: add definition \"%.*s:%.*s%s%.*s%s%.*s\"\n",
                (unsigned)spec.SystemName.size(), spec.SystemName.data(),
                (unsigned)spec.EventName.size(), spec.EventName.data(),
                spec.Flags.empty() ? "" : ":",
                (unsigned)spec.Flags.size(), spec.Flags.data(),
                spec.Fields.empty() ? "" : " ",
                (unsigned)spec.Fields.size(), spec.Fields.data());
            tracepoints.emplace_back(std::move(lineCopy), spec);
        }
        break;
    case TracepointSpecKind::EventHeaderDefinition:
        if (spec.SystemName != UserEventsSystemName)
        {
            PrintStderr("error: eventheader system name \"%.*s\" must be 'user_events': \"%.*s\"\n",
                (unsigned)spec.SystemName.size(), spec.SystemName.data(),
                (unsigned)line.size(), line.data());
        }
        else
        {
            PrintStderrIf(o.verbose, "verbose: add eventheader \"%.*s:%.*s%s%.*s\"\n",
                (unsigned)spec.SystemName.size(), spec.SystemName.data(),
                (unsigned)spec.EventName.size(), spec.EventName.data(),
                spec.Flags.empty() ? "" : ":",
                (unsigned)spec.Flags.size(), spec.Flags.data());
            tracepoints.emplace_back(std::move(lineCopy), spec);
        }
        break;
    case TracepointSpecKind::ErrorIdentifierCannotHaveFields:
        PrintStderr("error: identifier cannot have fields: \"%.*s\"\n",
            (unsigned)line.size(), line.data());
        break;
    case TracepointSpecKind::ErrorIdentifierCannotHaveFlags:
        PrintStderr("error: identifier cannot have flags: \"%.*s\"\n",
            (unsigned)line.size(), line.data());
        break;
    case TracepointSpecKind::ErrorDefinitionCannotHaveColonAfterFlags:
        PrintStderr("error: definition cannot have colon after flags: \"%.*s\"\n",
            (unsigned)line.size(), line.data());
        break;
    case TracepointSpecKind::ErrorIdentifierEventNameEmpty:
        PrintStderr("error: identifier event name is empty: \"%.*s\"\n",
            (unsigned)line.size(), line.data());
        break;
    case TracepointSpecKind::ErrorDefinitionEventNameEmpty:
        PrintStderr("error: definition event name is empty: \"%.*s\"\n",
            (unsigned)line.size(), line.data());
        break;
    case TracepointSpecKind::ErrorIdentifierEventNameInvalid:
        PrintStderr("error: identifier event name \"%.*s\" is invalid: \"%.*s\"\n",
            (unsigned)spec.EventName.size(), spec.EventName.data(),
            (unsigned)line.size(), line.data());
        break;
    case TracepointSpecKind::ErrorDefinitionEventNameInvalid:
        PrintStderr("error: definition event name \"%.*s\" is invalid: \"%.*s\"\n",
            (unsigned)spec.EventName.size(), spec.EventName.data(),
            (unsigned)line.size(), line.data());
        break;
    case TracepointSpecKind::ErrorEventHeaderDefinitionEventNameInvalid:
        PrintStderr("error: eventheader event name \"%.*s\" is invalid: \"%.*s\"\n",
            (unsigned)spec.EventName.size(), spec.EventName.data(),
            (unsigned)line.size(), line.data());
        PrintStderr("(error) If this was meant to be the name of an existing non-eventheader event, add a leading ':'.\n");
        PrintStderr("(error) If this was meant to be the definition of a non-eventheader event, the fields must be specified.\n");
        PrintStderr("(error) If a non-eventheader event has no fields, add \" ;\", e.g. \"MyEvent ;\".\n");
        break;
    case TracepointSpecKind::ErrorIdentifierSystemNameEmpty:
        PrintStderr("error: identifier system name is empty: \"%.*s\"\n",
            (unsigned)line.size(), line.data());
        break;
    case TracepointSpecKind::ErrorDefinitionSystemNameEmpty:
        PrintStderr("error: definition system name is empty: \"%.*s\"\n",
            (unsigned)line.size(), line.data());
        break;
    case TracepointSpecKind::ErrorIdentifierSystemNameInvalid:
        PrintStderr("error: identifier system name \"%.*s\" is invalid: \"%.*s\"\n",
            (unsigned)spec.SystemName.size(), spec.SystemName.data(),
            (unsigned)line.size(), line.data());
        break;
    case TracepointSpecKind::ErrorDefinitionSystemNameInvalid:
        PrintStderr("error: definition system name \"%.*s\" is invalid: \"%.*s\"\n",
            (unsigned)spec.SystemName.size(), spec.SystemName.data(),
            (unsigned)line.size(), line.data());
        break;
    }
}

static bool
PushFrontDefsFromFile(Options const& o, std::vector<Tracepoint>& tracepoints, char const* filename)
{
    // CodeQL [SM01937] This is a sample/tool. Using externally-supplied path is intended behavior.
    FILE* file = fopen(filename, "r");
    if (file == nullptr)
    {
        PrintStderr("error: failed to open file \"%s\".\n",
            filename);
        return false;
    }

    std::vector<char> line;

    char buf[128];
    while (fgets(buf, sizeof(buf), file))
    {
        line.insert(line.end(), buf, buf + strlen(buf));
        if (line.back() == '\n')
        {
            PushFrontDef(o, tracepoints, { line.data(), line.size() });
            line.clear();
        }
    }

    bool const ok = 0 == ferror(file);
    fclose(file);

    if (!ok)
    {
        fprintf(stderr, "error: failed to read file \"%s\".\n", filename);
    }
    else
    {
        PushFrontDef(o, tracepoints, { line.data(), line.size() });
    }

    return ok;
}

static unsigned long
ArgBufferSize(char const* flagName, int argi, int argc, char* argv[], bool* usageError)
{
    auto const BufferSizeMax = 0x80000000 / 1024;
    unsigned bufferSize = 128;
    if (argi >= argc)
    {
        PrintStderr("error: missing value for flag %s\n",
            flagName);
        *usageError = true;
    }
    else
    {
        auto const* const value = argv[argi];
        auto bufferSizeLong = strtoul(value, nullptr, 0);
        if (bufferSizeLong == 0)
        {
            PrintStderr("error: expected positive integer for flag %s %s\n",
                flagName, value);
            *usageError = true;
        }
        else if (bufferSizeLong > 0x80000000 / 1024)
        {
            PrintStderr("error: value too large for flag %s 0x%lX (max 0x%X)\n",
                flagName, bufferSizeLong, BufferSizeMax);
            *usageError = true;
        }
        else
        {
            bufferSize = static_cast<unsigned>(bufferSizeLong);
        }
    }

    return bufferSize;
}

int
main(int argc, char* argv[])
{
    int error;

    try
    {
        std::vector<Tracepoint> tracepoints;
        Options o;
        unsigned buffersize = 128;
        bool realtime = true;
        char const* output = "./perf.data";
        bool showHelp = false;
        bool usageError = false;

        for (int argi = 1; argi < argc; argi += 1)
        {
            char const* const arg = argv[argi];
            if (arg[0] != '-')
            {
                PushFrontDef(o, tracepoints, arg);
            }
            else if (arg[1] != '-')
            {
                auto const flags = &arg[1];
                for (unsigned flagsPos = 0; flags[flagsPos] != '\0'; flagsPos += 1)
                {
                    auto const flag = flags[flagsPos];
                    switch (flag)
                    {
                    case 'b':
                        argi += 1;
                        buffersize = ArgBufferSize("-b", argi, argc, argv, &usageError);
                        break;
                    case 'c':
                        realtime = false;
                        break;
                    case 'C':
                        realtime = true;
                        break;
                    case 'i':
                        argi += 1;
                        if (argi < argc)
                        {
                            PushFrontDefsFromFile(o, tracepoints, argv[argi]);
                        }
                        else
                        {
                            PrintStderr("error: missing filename for flag -i\n");
                            usageError = true;
                        }
                        break;
                    case 'o':
                        argi += 1;
                        if (argi < argc)
                        {
                            output = argv[argi];
                        }
                        else
                        {
                            PrintStderr("error: missing filename for flag -o\n");
                            usageError = true;
                        }
                        break;
                    case 'v':
                        o.verbose = true;
                        break;
                    case 'h':
                        showHelp = true;
                        break;
                    default:
                        PrintStderr("error: invalid flag -%c\n", flag);
                        usageError = true;
                        break;
                    }
                }
            }
            else
            {
                auto const flag = &arg[2];
                if (0 == strcmp(flag, "buffersize"))
                {
                    argi += 1;
                    buffersize = ArgBufferSize("--buffersize", argi, argc, argv, &usageError);
                }
                else if (0 == strcmp(flag, "circular"))
                {
                    realtime = false;
                }
                else if (0 == strcmp(flag, "realtime"))
                {
                    realtime = true;
                }
                else if (0 == strcmp(flag, "input"))
                {
                    argi += 1;
                    if (argi < argc)
                    {
                        PushFrontDefsFromFile(o, tracepoints, argv[argi]);
                    }
                    else
                    {
                        PrintStderr("error: missing filename for flag --input\n");
                        usageError = true;
                    }
                }
                else if (0 == strcmp(flag, "output"))
                {
                    argi += 1;
                    if (argi < argc)
                    {
                        output = argv[argi];
                    }
                    else
                    {
                        PrintStderr("error: missing filename for flag --output\n");
                        usageError = true;
                    }
                }
                else if (0 == strcmp(flag, "verbose"))
                {
                    o.verbose = true;
                }
                else if (0 == strcmp(flag, "help"))
                {
                    showHelp = true;
                }
                else
                {
                    PrintStderr("error: invalid flag --%s\n", flag);
                    usageError = true;
                }
            }
        }

        if (showHelp)
        {
            fputs(UsageCommon, stdout);
            fputs(UsageLong, stdout);
            error = EINVAL;
        }
        else if (usageError)
        {
            fputs(UsageCommon, stderr);
            fputs(UsageShort, stderr);
            error = EINVAL;
        }
        else if (tracepoints.empty())
        {
            PrintStderr("error: no tracepoints specified, exiting.\n");
            error = EINVAL;
        }
        else
        {
            auto const mode = realtime ? TracepointSessionMode::RealTime : TracepointSessionMode::Circular;
            TracepointCache cache;
            TracepointSession session(
                cache,
                TracepointSessionOptions(mode, buffersize * 1024)
                .WakeupWatermark(2048)); // WaitForWakeup waits for a buffer to have >= 2048 bytes of data.

            unsigned enabled = 0;
            for (auto const& tp : tracepoints)
            {
                if (tp.spec.Kind == TracepointSpecKind::Identifier)
                {
                    error = cache.AddFromSystem(TracepointName(tp.spec.SystemName, tp.spec.EventName));
                    switch (error)
                    {
                    default:
                        PrintStderr("error: Cannot find format for \"%.*s:%.*s\", error %u\n",
                            (unsigned)tp.spec.SystemName.size(), tp.spec.SystemName.data(),
                            (unsigned)tp.spec.EventName.size(), tp.spec.EventName.data(),
                            error);
                        continue;
                    case 0:
                        PrintStderrIf(o.verbose, "verbose: Loaded format for \"%.*s:%.*s\".\n",
                            (unsigned)tp.spec.SystemName.size(), tp.spec.SystemName.data(),
                            (unsigned)tp.spec.EventName.size(), tp.spec.EventName.data());
                        break;
                    case EEXIST:
                        PrintStderrIf(o.verbose, "verbose: Format already loaded for \"%.*s:%.*s\".\n",
                            (unsigned)tp.spec.SystemName.size(), tp.spec.SystemName.data(),
                            (unsigned)tp.spec.EventName.size(), tp.spec.EventName.data());
                        break;
                    }
                }
                else
                {
                    error = cache.PreregisterTracepointDefinition(tp.spec);
                    switch (error)
                    {
                    default:
                        PrintStderr("error: Cannot pre-register \"%.*s:%.*s\", error %u\n",
                            (unsigned)tp.spec.SystemName.size(), tp.spec.SystemName.data(),
                            (unsigned)tp.spec.EventName.size(), tp.spec.EventName.data(),
                            error);
                        continue;
                    case 0:
                        PrintStderrIf(o.verbose, "verbose: Pre-registered \"%.*s:%.*s\".\n",
                            (unsigned)tp.spec.SystemName.size(), tp.spec.SystemName.data(),
                            (unsigned)tp.spec.EventName.size(), tp.spec.EventName.data());
                        break;
                    case EEXIST:
                        PrintStderrIf(o.verbose, "verbose: Already registered \"%.*s:%.*s\".\n",
                            (unsigned)tp.spec.SystemName.size(), tp.spec.SystemName.data(),
                            (unsigned)tp.spec.EventName.size(), tp.spec.EventName.data());
                        break;
                    }
                }

                error = session.EnableTracepoint(TracepointName(tp.spec.SystemName, tp.spec.EventName));
                if (error != 0)
                {
                    PrintStderr("error: Cannot enable \"%.*s:%.*s\", error %u\n",
                        (unsigned)tp.spec.SystemName.size(), tp.spec.SystemName.data(),
                        (unsigned)tp.spec.EventName.size(), tp.spec.EventName.data(),
                        error);
                }
                else
                {
                    enabled += 1;
                    PrintStderrIf(o.verbose, "verbose: Enabled \"%.*s:%.*s\".\n",
                        (unsigned)tp.spec.SystemName.size(), tp.spec.SystemName.data(),
                        (unsigned)tp.spec.EventName.size(), tp.spec.EventName.data());
                }
            }

            static constexpr int ExitSigs[] = { EXIT_SIGNALS };
            static constexpr unsigned ExitSigsCount = sizeof(ExitSigs) / sizeof(ExitSigs[0]);
            sigset_t exitSigSet;
            sigemptyset(&exitSigSet);
            for (auto exitSig : ExitSigs)
            {
                sigaddset(&exitSigSet, exitSig);
            }

            sigset_t oldSigSet;

            if (enabled == 0)
            {
                PrintStderr("error: No tracepoints enabled, exiting.\n");
                error = ENOENT;
            }
            else if (sigprocmask(SIG_BLOCK, &exitSigSet, &oldSigSet))
            {
                PrintStderr("error: sigprocmask returned %u\n",
                    errno);
                error = errno;
                if (error == 0)
                {
                    error = EINTR;
                }
            }
            else if (mode == TracepointSessionMode::Circular)
            {
                PrintStderrIf(o.verbose, "verbose: waiting for { " EXIT_SIGNALS_STR " }.\n");

                int sig = 0;
                sigwait(&exitSigSet, &sig);
                sigprocmask(SIG_SETMASK, &oldSigSet, nullptr);

                PrintStderrIf(o.verbose, "verbose: signal %u, writing \"%s\".\n",
                    sig, output);
                error = session.SavePerfDataFile(output);
                if (error != 0)
                {
                    PrintStderr("error: Error %u writing file \"%s\"\n",
                        error, output);
                }
            }
            else
            {
                static int signalHandled = 0; // not multi-thread safe.
                struct sigaction newAct = {};
                newAct.sa_handler = [](int sig)
                    {
                        PrintStderr("SIGNAL\n");
                        signalHandled = sig;
                    };
                newAct.sa_mask = exitSigSet;

                struct sigaction oldActs[ExitSigsCount] = {};
                unsigned sigsInstalled = 0;

                PerfDataFileWriter writer;
                TracepointTimestampRange writtenRange;
                uint64_t writerPos;
                error = writer.Create(output);
                if (error != 0)
                {
                    PrintStderr("error: Error %u creating file \"%s\"\n",
                        error, output);
                    goto RealtimeDone;
                }
                else
                {
                    PrintStderrIf(o.verbose, "verbose: created \"%s\".\n",
                        output);
                }

                PrintStderrIf(o.verbose, "verbose: waiting for { " EXIT_SIGNALS_STR " }.\n");

                for (; sigsInstalled != ExitSigsCount; sigsInstalled += 1)
                {
                    if (sigaction(ExitSigs[sigsInstalled], &newAct, &oldActs[sigsInstalled]))
                    {
                        PrintStderr("error: sigaction returned %u\n",
                            errno);
                        error = errno;
                        if (error == 0)
                        {
                            error = EINTR;
                        }
                        goto RealtimeDone;
                    }
                }

                error = writer.WriteFinishedInit();
                if (error != 0)
                {
                    PrintStderr("error: Error %u writing FinishedInit to \"%s\"\n",
                        error, output);
                    goto RealtimeDone;
                }

                writerPos = writer.FilePos();

                while (signalHandled == 0)
                {
                    error = session.WaitForWakeup(nullptr, &oldSigSet);
                    if (error != 0)
                    {
                        if (error != EINTR)
                        {
                            PrintStderr("error: ppoll returned %u\n",
                                error);
                        }
                        else
                        {
                            PrintStderrIf(o.verbose, "verbose: ppoll interrupted, signalHandled = %u.\n",
                                signalHandled);
                        }
                        break;
                    }

                    error = session.FlushToWriter(writer, &writtenRange);
                    if (error != 0)
                    {
                        PrintStderr("error: Error %u flushing to file \"%s\"\n",
                            error, output);
                        break;
                    }

                    auto newWriterPos = writer.FilePos();
                    PrintStderrIf(o.verbose, "verbose: flushed %lu bytes.\n",
                        static_cast<unsigned long>(newWriterPos - writerPos));
                    if (newWriterPos != writerPos)
                    {
                        writerPos = newWriterPos;
                        error = writer.WriteFinishedRound();
                        if (error != 0)
                        {
                            PrintStderr("error: Error %u writing FinishedRound to \"%s\"\n",
                                error, output);
                            break;
                        }
                    }
                }

                {
                    error = session.FlushToWriter(writer, &writtenRange);
                    if (error != 0)
                    {
                        PrintStderr("error: Error %u flushing to file \"%s\"\n",
                            error, output);
                        goto RealtimeDone;
                    }

                    auto newWriterPos = writer.FilePos();
                    PrintStderrIf(o.verbose, "verbose: flushed %lu bytes.\n",
                        static_cast<unsigned long>(newWriterPos - writerPos));
                }

                error = session.SetWriterHeaders(writer, &writtenRange);
                if (error != 0)
                {
                    PrintStderr("error: Error %u writing FinishedRound to \"%s\"\n",
                        error, output);
                    goto RealtimeDone;
                }

            RealtimeDone:

                for (unsigned sigsRestored = 0; sigsRestored != sigsInstalled; sigsRestored += 1)
                {
                    sigaction(ExitSigs[sigsRestored], &oldActs[sigsRestored], nullptr);
                }

                sigprocmask(SIG_SETMASK, &oldSigSet, nullptr);

                auto newError = writer.FinalizeAndClose();
                if (newError != 0 && error == 0)
                {
                    error = newError;
                    PrintStderr("error: Error %u finalizing file \"%s\"\n",
                        error, output);
                }
            }
        }
    }
    catch (std::exception const& ex)
    {
        PrintStderr("fatal error: %s\n", ex.what());
        error = ENOMEM;
    }

    return error;
}
