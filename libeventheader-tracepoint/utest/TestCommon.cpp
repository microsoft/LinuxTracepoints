// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*
Generates a bunch of tracepoint events using various tracepoint-generation APIs.
*/

#include <eventheader/TraceLoggingProvider.h>
#include <inttypes.h>

TRACELOGGING_DEFINE_PROVIDER(
    LongProvider,
    "Long_Provider_Name_XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX0123456789",
    // {7a442600-4333-5126-6401-08ff132396f0}
    (0x7a442600, 0x4333, 0x5126, 0x64, 0x01, 0x08, 0xff, 0x13, 0x23, 0x96, 0xf0),
    TraceLoggingOptionGroupName("asdf"));

#include <stdio.h>
#include "TestCommon.h"

extern "C" {

    int TestC(void);
    bool TestCpp(void);

} // extern "C"

bool TestDynamic();

int TestCommon()
{
    bool allOk = true;
    bool oneOk;

    int result;

    char str[EVENTHEADER_COMMAND_MAX];

    result = EVENTHEADER_FORMAT_COMMAND(str, EVENTHEADER_COMMAND_MAX,
        TraceLoggingProviderName(LongProvider), -1, -1, TraceLoggingProviderOptions(LongProvider));
    if (result >= EVENTHEADER_COMMAND_MAX)
    {
        printf("ERROR: EVENTHEADER_FORMAT_COMMAND returned %d\n", result);
        allOk = false;
    }

    result = EVENTHEADER_FORMAT_TRACEPOINT_NAME(str, EVENTHEADER_NAME_MAX,
        TraceLoggingProviderName(LongProvider), -1, -1, TraceLoggingProviderOptions(LongProvider));
    if (result >= EVENTHEADER_NAME_MAX)
    {
        printf("ERROR: EVENTHEADER_FORMAT_TRACEPOINT_NAME returned %d\n", result);
        allOk = false;
    }

    TraceLoggingRegister(LongProvider);
    TraceLoggingWrite(LongProvider, "LongProviderEvent");
    TraceLoggingUnregister(LongProvider);

    oneOk = TestC();
    printf("TestC: %s\n", oneOk ? "ok" : "ERROR");
    allOk &= oneOk;

    oneOk = TestCpp();
    printf("TestCpp: %s\n", oneOk ? "ok" : "ERROR");
    allOk &= oneOk;

    oneOk = TestDynamic();
    printf("TestDynamic: %s\n", oneOk ? "ok" : "ERROR");
    allOk &= oneOk;

    return !allOk;
}
