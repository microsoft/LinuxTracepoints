#include <tracepoint/TracingSession.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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
        ? TracingMode::Circular
        : TracingMode::RealTime;

    TracingCache cache;
    TracingSession session(cache, TracingSessionOptions(mode, 0).WakeupEvents(1)); // 0 should result in a 1-page buffer.

    fprintf(stderr, "Session: BC=%u BS=%lx RT=%u MODE=%u\n",
        session.BufferCount(), session.BufferSize(), session.IsRealtime(), (unsigned)session.Mode());

    fprintf(stderr, "\n");

    for (int argi = 2; argi < argc; argi += 1)
    {
        error = session.EnableTracePoint(argv[argi]);
        fprintf(stderr, "EnableTracePoint(%s) = %u\n", argv[argi], error);
    }

    fprintf(stderr, "\n");

    for (int argi = 2; argi < argc; argi += 1)
    {
        error = session.DisableTracePoint(argv[argi]);
        fprintf(stderr, "DisableTracePoint(%s) = %u\n", argv[argi], error);
    }

    fprintf(stderr, "\n");

    for (int argi = 2; argi < argc; argi += 1)
    {
        error = session.EnableTracePoint(argv[argi]);
        fprintf(stderr, "EnableTracePoint(%s) = %u\n", argv[argi], error);
    }

    for (;;)
    {
        fprintf(stderr, "\n");

        if (mode == TracingMode::Circular)
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

        error = session.FlushSampleEventsUnordered(
            [](unsigned cpu, PerfSampleEventInfo const& event)
            {
                fprintf(stdout, "CPU%u: tid=%x time=0x%llx cpu=%u raw=0x%lx n=%s\n",
                    cpu,
                    event.tid,
                    (long long unsigned)event.time,
                    event.cpu,
                    event.raw_data_size,
                    event.name);
                return 0;
            });
        fprintf(stderr, "Flush: %u, Count=%llu, Lost=%llu, Bad=%llu, BadBuf=%llu\n",
            error,
            (long long unsigned)session.SampleEventCount(),
            (long long unsigned)session.LostEventCount(),
            (long long unsigned)session.CorruptEventCount(),
            (long long unsigned)session.CorruptBufferCount());
    }

    return 0;
}
