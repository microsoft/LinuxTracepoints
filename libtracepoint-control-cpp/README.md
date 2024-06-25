#  libtracepoint-control-cpp

- [perf-collect](tools/perf-collect.cpp) is a tool that collects tracepoint
  events into a `perf.data` file using the `libtracepoint-control-cpp` library.
  This tool is similar to the `perf record` command, but it includes special
  "pre-register" support to simplify collection of `user_events` tracepoints
  that are not yet registered when trace collection begins.
- `TracepointSession.h` implements an event collection session that can
  collect tracepoint events and enumerate the events that the session has
  collected.
- `TracepointPath.h` has functions for finding the `/sys/kernel/tracing`
  mount point and reading `format` files.
- `TracepointName.h` represents a tracepoint name (system name + event
  name); for instance, `user_events:eventName`.
- `TracepointSpec.h` represents a tracepoint specification (system name + event
  name + field definitions); for instance, `user_events:eventName int field1`.
- `TracepointCache.h` implements a cache for tracking parsed `format` files
  and locating cached data by `TracepointName` or by `common_type` id.
