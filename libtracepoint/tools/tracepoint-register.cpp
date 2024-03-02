// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <tracepoint/tracepoint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <string>
#include <string_view>
#include <forward_list>

// From eventheader/eventheader.h:
#define EVENTHEADER_COMMAND_TYPES "u8 eventheader_flags;u8 version;u16 id;u16 tag;u8 opcode;u8 level"
enum {
    // Maximum length of a Tracepoint name "ProviderName_Attributes", including nul termination.
    EVENTHEADER_NAME_MAX = 256,

    // Maximum length needed for a DIAG_IOCSREG command "ProviderName_Attributes CommandTypes".
    EVENTHEADER_COMMAND_MAX = EVENTHEADER_NAME_MAX + sizeof(EVENTHEADER_COMMAND_TYPES)
};

// From tracepoint.c:
extern "C" int
tracepoint_connect2(
    tracepoint_state* tp_state,
    tracepoint_provider_state* provider_state,
    char const* tp_name_args,
    unsigned flags);

// From uapi/linux/perf_event.h:
enum user_reg_flag {
    USER_EVENT_REG_PERSIST = 1U << 0,
};

enum WaitSetting : unsigned char {
    WaitUnspecified,
    WaitNo,
    WaitYes,
};

struct TracepointInfo
{
    std::string command;
    tracepoint_state state;

    explicit
    TracepointInfo(std::string&& _command)
        : command(std::move(_command))
        , state(TRACEPOINT_STATE_INIT)
    {
        return;
    }
};

struct Options
{
    bool verbose = false;
    bool eventHeader = false;
};

#define EXIT_SIGNALS       SIGQUIT
#define EXIT_SIGNALS_STR "(SIGQUIT)"

static char const* const UsageCommon = R"(
Usage: tracepoint-register [options...] TracepointDefinitions...
)";

// Usage error: stderr += UsageCommon + UsageShort.
static char const* const UsageShort = R"(
Try "tracepoint-register --help" for more information.
)";

// -h or --help: stdout += UsageCommon + UsageLong.
static char const* const UsageLong = R"(
Pre-registers user_events tracepoints so that you can start a trace (i.e. with
the Linux "perf" tool) before running the program that generates the events.

Options:

-f, --file        Read tracepoint definitions from a file, "-f MyDefs.txt" or
                  "--file MyDefs.txt". Each line in the file is a
                  TracepointDefinition. Lines starting with '#' are ignored.

-p, --persist     Use the USER_EVENT_REG_PERSIST flag when registering each
                  tracepoint so that the tracepoints remain available after
                  exit (requires CAP_PERFMON).

-w, --wait        Do not exit until signalled )" EXIT_SIGNALS_STR R"(.
                  Keeps tracepoints registered until exit. This is the default
                  when -p is not specified.

-W, --nowait      Exit immediately. This is the default when -p is specified.

-e, --eventheader Subsequent TracepointDefinitions are EventHeader tracepoints
                  unless they start with ':' (inverts the default behavior).

-E, --noeventheader Subsequent TracepointDefinitions are normal tracepoints
                  unless they start with ':' (restores the default behavior).

-v, --verbose     Show verbose output. Place this before any other options.

-h, --help        Show this help message and exit.

A TracepointDefinition must be formatted as:

    name[:flag1[,flag2...]] [fieldDef1[;fieldDef2...]]

For example:

- MyEvent1
- MyEvent2 u32 MyField1
- MyEvent3:MyFlag u32 MyField1;struct MyStruct2 MyField2 20

Definitions with spaces must be enclosed in quotes when specified as
command-line arguments, e.g. "MyEvent2 u32 MyField1".

As a shortcut, an EventHeader tracepoint may be specified without any fields.
Add a leading ':' to indicate that the definition has omitted the EventHeader
fields. An EventHeader TracepointDefinition must be formatted as:

    :provider_attributes[:flag1[,flag2...]]

For example:

- :MyProvider_L2K1
- :MyProvider_L5K3ffGmygroup
- :MyProvider_L5K3ffGmygroup:MyFlag

EventHeader definitions must include "L" (level) and "K" (keyword) attributes.
)";

static bool
AsciiIsLowercaseHex(char ch)
{
    return
        ('0' <= ch && ch <= '9') ||
        ('a' <= ch && ch <= 'f');
}

static bool
AsciiIsAlphanumeric(char ch)
{
    return
        ('0' <= ch && ch <= '9') ||
        ('A' <= ch && ch <= 'Z') ||
        ('a' <= ch && ch <= 'z');
}

// if (condition) fprintf(stderr, format, args...).
static void
PrintStderrIf(bool condition, const char* format, ...)
{
    if (condition)
    {
        va_list args;
        va_start(args, format);
        vfprintf(stderr, format, args);
        va_end(args);
    }
}

static bool
PushFrontDef(Options const& o, std::forward_list<TracepointInfo>& tracepoints, std::string_view def)
{
    std::string command;
    std::string_view defNoFlag;
    bool isEventHeader;

    if (def.empty() || def[0] != ':')
    {
        // No leading ':'.
        defNoFlag = def.substr(0);
        isEventHeader = o.eventHeader;
    }
    else
    {
        // Has leading ':'.
        defNoFlag = def.substr(1);
        isEventHeader = !o.eventHeader;
    }

    if (defNoFlag.empty())
    {
        PrintStderrIf(o.verbose, "verbose: empty definition\n");
        return true;
    }

    if (!isEventHeader)
    {
        // Traditional tracepoint definition. No validation.
        command = defNoFlag;
    }
    else
    {
        // EventHeader tracepoint definition.

        if (defNoFlag.find(' ') != std::string_view::npos)
        {
            fprintf(stderr, "error: eventheader definition \"%.*s\" contains invalid char ' '.\n",
                (unsigned)defNoFlag.size(), defNoFlag.data());
            return false;
        }

        // name = defNoFlag up to last ':'. If no ':', name = defNoFlag.
        auto const name = defNoFlag.substr(0, defNoFlag.rfind(':'));

        if (name.size() >= EVENTHEADER_NAME_MAX)
        {
            fprintf(stderr, "error: eventheader name \"%.*s\" is too long.\n",
                (unsigned)name.size(), name.data());
            return false;
        }

        if (name.find(':') != std::string_view::npos)
        {
            fprintf(stderr, "error: eventheader name \"%.*s\" contains invalid char ':'.\n",
                (unsigned)name.size(), name.data());
            return false;
        }

        auto pos = name.rfind('_');

        if (pos == std::string_view::npos ||
            name.size() < pos + 3 ||
            name[pos + 1] != 'L' ||
            !AsciiIsLowercaseHex(name[pos + 2]))
        {
            fprintf(stderr, "error: eventheader name \"%.*s\" is missing the required \"_L<level>\" suffix.\n",
                (unsigned)name.size(), name.data());
            return false;
        }

        // Skip "_Lnnn"
        pos += 3;
        while (pos < name.size() && AsciiIsLowercaseHex(name[pos]))
        {
            pos += 1;
        }

        if (name.size() < pos + 2 ||
            name[pos] != 'K' ||
            !AsciiIsLowercaseHex(name[pos + 1]))
        {
            fprintf(stderr, "error: eventheader name \"%.*s\" is missing the required \"K<keyword>\" suffix.\n",
                (unsigned)name.size(), name.data());
            return false;
        }

        // Skip "Knnn..."
        pos += 2;
        for (; pos < name.size(); pos += 1)
        {
            if (!AsciiIsAlphanumeric(name[pos]))
            {
                fprintf(stderr, "error: eventheader name \"%.*s\" contains non-alphanumeric characters in the suffix.\n",
                    (unsigned)name.size(), name.data());
                return false;
            }
        }

        command.reserve(defNoFlag.size() + sizeof(EVENTHEADER_COMMAND_TYPES));
        command = defNoFlag;
        command += ' ';
        command += EVENTHEADER_COMMAND_TYPES;
    }

    PrintStderrIf(o.verbose, "verbose: add \"%s\"\n",
        command.c_str());

    tracepoints.emplace_front(std::move(command));

    return true;
}

static bool
PushFrontDefsFromFile(Options const& o, std::forward_list<TracepointInfo>& tracepoints, char const* filename)
{
    // CodeQL [SM01937] This is a sample/tool. Using externally-supplied path is intended behavior.
    FILE* file = fopen(filename, "r");
    if (file == nullptr)
    {
        fprintf(stderr, "error: failed to open file \"%s\".\n", filename);
        return false;
    }

    std::string line;

    char buf[128];
    while (fgets(buf, sizeof(buf), file))
    {
        line += buf;
        if (line.back() == '\n')
        {
            line.pop_back(); // Remove newline.
            if (!line.empty() && line[0] != '#')
            {
                PushFrontDef(o, tracepoints, line);
            }

            line.clear();
        }
    }

    bool const ok = 0 == ferror(file);
    fclose(file);

    if (!ok)
    {
        fprintf(stderr, "error: failed to read file \"%s\".\n", filename);
    }
    else if (!line.empty() && line[0] != '#')
    {
        PushFrontDef(o, tracepoints, line);
    }

    return ok;
}

int
main(int argc, char* argv[])
{
    int error;
    tracepoint_provider_state providerState = TRACEPOINT_PROVIDER_STATE_INIT;

    try
    {
        std::forward_list<TracepointInfo> tracepoints;
        Options o;
        auto waitSetting = WaitUnspecified;
        bool persist = false;
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
                    case 'f':
                        argi += 1;
                        if (argi < argc)
                        {
                            PushFrontDefsFromFile(o, tracepoints, argv[argi]);
                        }
                        else
                        {
                            fprintf(stderr, "error: missing filename for flag -f\n");
                            usageError = true;
                        }
                        break;
                    case 'p':
                        persist = true;
                        break;
                    case 'w':
                        waitSetting = WaitYes;
                        break;
                    case 'W':
                        waitSetting = WaitNo;
                        break;
                    case 'e':
                        o.eventHeader = true;
                        break;
                    case 'E':
                        o.eventHeader = false;
                        break;
                    case 'v':
                        o.verbose = true;
                        break;
                    case 'h':
                        showHelp = true;
                        break;
                    default:
                        fprintf(stderr, "error: invalid flag -%c\n", flag);
                        usageError = true;
                        break;
                    }
                }
            }
            else
            {
                auto const flag = &arg[2];
                if (0 == strcmp(flag, "file"))
                {
                    argi += 1;
                    if (argi < argc)
                    {
                        PushFrontDefsFromFile(o, tracepoints, argv[argi]);
                    }
                    else
                    {
                        fprintf(stderr, "error: missing filename for flag --file\n");
                        usageError = true;
                    }
                }
                else if (0 == strcmp(flag, "persist"))
                {
                    persist = true;
                }
                else if (0 == strcmp(flag, "wait"))
                {
                    waitSetting = WaitYes;
                }
                else if (0 == strcmp(flag, "nowait"))
                {
                    waitSetting = WaitNo;
                }
                else if (0 == strcmp(flag, "eventheader"))
                {
                    o.eventHeader = true;
                }
                else if (0 == strcmp(flag, "noeventheader"))
                {
                    o.eventHeader = false;
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
                    fprintf(stderr, "error: invalid flag --%s\n", flag);
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
            fprintf(stderr, "error: no tracepoints specified, exiting.\n");
            error = EINVAL;
        }
        else
        {
            error = tracepoint_open_provider(&providerState);
            if (0 != error)
            {
                fprintf(stderr, "error: tracepoint_open_provider failed (%u).\n",
                    error);
            }
            else
            {
                tracepoints.reverse();
                for (auto& tracepoint : tracepoints)
                {
                    unsigned const flags = persist ? USER_EVENT_REG_PERSIST : 0;
                    int connectResult = tracepoint_connect2(&tracepoint.state, &providerState, tracepoint.command.c_str(), flags);
                    if (connectResult != 0)
                    {
                        fprintf(stderr, "warning: tracepoint_connect failed (%u) for \"%s\".\n",
                            connectResult, tracepoint.command.c_str());
                    }
                }

                if (waitSetting == WaitYes ||
                    (waitSetting == WaitUnspecified && !persist))
                {
                    PrintStderrIf(o.verbose, "verbose: waiting for " EXIT_SIGNALS_STR ".\n");

                    static const int ExitSigs[] = { EXIT_SIGNALS };
                    sigset_t exitSigSet;
                    sigemptyset(&exitSigSet);
                    for (auto exitSig : ExitSigs)
                    {
                        sigaddset(&exitSigSet, exitSig);
                    }

                    int sig = 0;
                    sigwait(&exitSigSet, &sig);
                    PrintStderrIf(o.verbose, "verbose: signal %u.\n",
                        sig);
                }
            }

            tracepoint_close_provider(&providerState);
        }
    }
    catch (std::exception const& ex)
    {
        fprintf(stderr, "\nfatal error: %s\n", ex.what());
        error = ENOMEM;
    }

    return error;
}
