// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*
Emits a bunch of tracepoint events using various tracepoint-generation APIs. Typically
the results will be collected using a tool like perf or perf-collect, then formatted to
verify that the tracepoints were emitted and formatted correctly.
*/

#include "TestCommon.h"

int main()
{
    return TestCommon();
}
