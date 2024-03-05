// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <tracepoint/tracepoint.h>
#include <eventheader/eventheader.h>
#include <stdio.h>
#include <string.h>

static bool
IsLowercaseHex(char ch)
{
    return ('0' <= ch && ch <= '9') || ('a' <= ch && ch <= 'f');
}

int main(int argc, char* argv[])
{
    int result;
    tracepoint_provider_state providerState = TRACEPOINT_PROVIDER_STATE_INIT;

    if (argc <= 1 ||
        0 == strcmp(argv[1], "-h") ||
        0 == strcmp(argv[1], "--help"))
    {
        fputs(R"(
Usage: eventheader-register TracepointName1 [TracepointName2]...
Pre-registers eventheader tracepoint names so that you can start a trace before
running the program that generates the events.

Note: This tool is deprecated. Prefer the tracepoint-register tool from
libtracepoint.

Each TracepointName must be formatted as "<providerName>_L<level>K<keyword>"
or "<providerName>_L<level>K<keyword>G<providerGroup>". For example,
"MyProvider_L2K1" or "MyProvider_L5K3ffGmygroup".
)", stdout);
        result = 1;
    }
    else if (0 != (result = tracepoint_open_provider(&providerState)))
    {
        fprintf(stderr, "error: tracepoint_open_provider error %u\n", result);
    }
    else
    {
        for (int i = 1; i < argc; i += 1)
        {
            char const* arg = argv[i];

            if (strchr(arg, ' '))
            {
                fprintf(stderr, "error: name \"%s\" contains ' '.\n", arg);
                result = 1;
                continue;
            }

            if (strchr(arg, ':'))
            {
                fprintf(stderr, "error: name \"%s\" contains ':'.\n", arg);
                result = 1;
                continue;
            }

            if (strlen(arg) >= EVENTHEADER_NAME_MAX)
            {
                fprintf(stderr, "error: name \"%s\" is too long.\n", arg);
                result = 1;
                continue;
            }

            auto p = strrchr(arg, '_');
            if (!p || p[1] != 'L' || !IsLowercaseHex(p[2]))
            {
                fprintf(stderr, "error: name \"%s\" is missing the required \"_L<level>\" suffix.\n", arg);
                result = 1;
                continue;
            }

            p += 3; // Skip "_Ln"
            while (IsLowercaseHex(*p))
            {
                p += 1;
            }

            if (p[0] != 'K' || !IsLowercaseHex(p[1]))
            {
                fprintf(stderr, "error: name \"%s\" is missing the required \"K<keyword>\" suffix.\n", arg);
                result = 1;
                continue;
            }

            p += 2; // Skip "_Kn"

            while (
                ('0' <= *p && *p <= '9') ||
                ('A' <= *p && *p <= 'Z') ||
                ('a' <= *p && *p <= 'z'))
            {
                p += 1;
            }

            if (*p != 0)
            {
                fprintf(stderr, "error: name \"%s\" contains non-alphanumeric characters in the suffix.\n", arg);
                result = 1;
                continue;
            }

            char nameArgs[EVENTHEADER_COMMAND_MAX];
            snprintf(nameArgs, sizeof(nameArgs), "%s " EVENTHEADER_COMMAND_TYPES, arg);

            tracepoint_state tracepointState = TRACEPOINT_STATE_INIT;
            int const connectResult = tracepoint_connect(&tracepointState, &providerState, nameArgs);
            tracepoint_connect(&tracepointState, nullptr, ""); // Immediately and unconditionally disconnect.
            if (connectResult != 0)
            {
                fprintf(stderr, "warning: tracepoint_connect error %u for \"%s\"\n", connectResult, arg);
            }
        }
    }

    return result;
}
