#  libtracepoint-control-cpp

- `TracingPath.h` has functions for finding the `/sys/kernel/tracing` mount
  point and reading `format` files.
- `TracingCache.h` implements a cache for tracking parsed `format` files based
  on system+name or by `common_type` id.
- `TracingSession.h` implements session control (add/remove tracepoints from a
  session) and data collection (enumerate the events that the session has
  collected).
