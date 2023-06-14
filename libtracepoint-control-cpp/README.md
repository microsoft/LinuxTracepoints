#  libtracepoint-control-cpp

- `TracingSession.h` implements an event collection session that can
  collect tracepoint events and enumerate the events that the session has
  collected.
- `TracingPath.h` has functions for finding the `/sys/kernel/tracing`
  mount point and reading `format` files.
- `TracingCache.h` implements a cache for tracking parsed `format` files
  and locating cached data by system+name or by `common_type` id.
