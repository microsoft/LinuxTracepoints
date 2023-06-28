// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <tracepoint/TracepointSession.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>

#include <tracepoint/PerfEventAbi.h>

using namespace std::string_view_literals;
using namespace tracepoint_control;
using namespace tracepoint_decode;

int
main(int argc, char* argv[])
{
    int error;

    if (argc < 2 ||
        (0 != strcmp(argv[1], "0") && 0 != strcmp(argv[1], "1")))
    {
        fprintf(stderr,
            "Usage: control-session [0|1] systemName:eventName ...\n"
            "0 = circular, 1 = realtime\n");
        return 1;
    }

    auto const mode = 0 == strcmp(argv[1], "0")
        ? TracepointSessionMode::Circular
        : TracepointSessionMode::RealTime;

    TracepointCache cache;
    TracepointSession session(
        cache,
        TracepointSessionOptions(mode, 0) // 0 should round up to a 1-page buffer.
            .WakeupWatermark(100)); // WaitForWakeup waits for a buffer to have >= 100 bytes of data.

    fprintf(stderr, "Session: BC=%u BS=%x RT=%u MODE=%u\n",
        session.BufferCount(), session.BufferSize(), session.IsRealtime(), (unsigned)session.Mode());

    fprintf(stderr, "\n");

    for (int argi = 2; argi < argc; argi += 1)
    {
        TracepointName name(argv[argi]);
        error = cache.AddFromSystem(name);
        if (error != ENOENT || name.SystemName != UserEventsSystemName ||
            !name.IsValidEventHeader())
        {
            fprintf(stderr, "AddFromSystem(%s) = %u\n", argv[argi], error);
        }
        else
        {
            // User-specified EventHeader event is not registered.
            // Pre-register it and try to collect it anyway.
            error = cache.PreregisterEventHeaderTracepoint(name);
            fprintf(stderr, "PreregisterEventHeaderTracepoint(%s) = %u\n", argv[argi], error);
        }
    }

    fprintf(stderr, "\n");

    unsigned enabled = 0;
    for (int argi = 2; argi < argc; argi += 1)
    {
        error = session.EnableTracePoint(TracepointName(argv[argi]));
        fprintf(stderr, "EnableTracePoint(%s) = %u\n", argv[argi], error);
        enabled += error == 0;
    }

    if (enabled == 0)
    {
        return error;
    }

    for (;;)
    {
        fprintf(stderr, "\n");

        if (mode == TracepointSessionMode::Circular)
        {
            sleep(5);
        }
        else
        {
            int activeCount;
            error = session.WaitForWakeup(nullptr, nullptr, &activeCount);
            fprintf(stderr, "WaitForWakeup() = %u, active = %d\n", error, activeCount);
            if (error != 0)
            {
                sleep(5);
            }
        }

        error = session.EnumerateSampleEventsUnordered(
            [](PerfSampleEventInfo const& event) -> int
            {
                fprintf(stdout, "CPU%u: tid=%x time=0x%llx raw=0x%lx n=%s\n",
                    event.cpu,
                    event.tid,
                    (long long unsigned)event.time,
                    event.raw_data_size,
                    event.name);
                return 0;
            });
        fprintf(stderr, "Enum: %u, Count=%llu, Lost=%llu, Bad=%llu, BadBuf=%llu\n",
            error,
            (long long unsigned)session.SampleEventCount(),
            (long long unsigned)session.LostEventCount(),
            (long long unsigned)session.CorruptEventCount(),
            (long long unsigned)session.CorruptBufferCount());
    }

    return 0;
}
