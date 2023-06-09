#include <tracepoint/TracingCache.h>
#include <stdio.h>

using namespace std::string_view_literals;
using namespace tracepoint_control;

int
main(int argc, char* argv[])
{
    TracingCache cache;
    for (int argi = 1; argi < argc; argi += 1)
    {
        int error;
        std::string_view const arg = argv[argi];
        auto const splitPos = arg.find_first_of(":/"sv);
        auto const systemName = splitPos == arg.npos
            ? "user_events"sv
            : arg.substr(0, splitPos);
        auto const eventName = splitPos == arg.npos
            ? arg
            : arg.substr(splitPos + 1);

        error = cache.AddFromSystem(systemName, eventName);
        fprintf(stdout, "AddFromSystem(%.*s:%.*s)=%u\n",
            (unsigned)systemName.size(), systemName.data(),
            (unsigned)eventName.size(), eventName.data(),
            error);

        unsigned id = 0;
        if (auto const meta = cache.FindByName(systemName, eventName); meta)
        {
            fprintf(stdout, "- FindByName=%u\n", meta->Id());
            fprintf(stdout, "  Sys = %.*s\n", (unsigned)meta->SystemName().size(), meta->SystemName().data());
            fprintf(stdout, "  Name= %.*s\n", (unsigned)meta->Name().size(), meta->Name().data());
            fprintf(stdout, "  Fmt = %.*s\n", (unsigned)meta->PrintFmt().size(), meta->PrintFmt().data());
            fprintf(stdout, "  Flds= %u\n", (unsigned)meta->Fields().size());
            fprintf(stdout, "  Id  = %u\n", (unsigned)meta->Id());
            fprintf(stdout, "  CmnC= %u\n", (unsigned)meta->CommonFieldCount());
            fprintf(stdout, "  EH  = %u\n", (unsigned)meta->HasEventHeader());
            id = meta->Id();
        }

        if (auto const meta = cache.FindById(id); meta)
        {
            fprintf(stdout, "- FindById(%u)=%u\n", id, meta->Id());
        }
    }

    fprintf(stdout, "CommonTypeOffset=%d\n", cache.CommonTypeOffset());
    fprintf(stdout, "CommonTypeSize  =%u\n", cache.CommonTypeSize());

    return 0;
}
