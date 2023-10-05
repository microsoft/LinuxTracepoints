# LinuxTracepoints Change Log

## v1.3.0 (TBD)

- `PerfEventInfo.h` adds a field with session information to the metadata of
  each event. The session information includes clock information.
- `PerfDataFile.h` decodes clock information from perf.data files if present.
- `TracepointSession.h` records clock information from the session.
- `EventFormatter.h` formats timestamps as date-time if clock information is
  available in the event metadata. If clock information is not present, it
  continues to format timestamps as seconds.
- Changed procedure for locating the `user_events_data` file.
  - Old: parse `/proc/mounts` to determine the `tracefs` or `debugfs` mount
    point, then use that as the root for the `user_events_data` path.
  - New: try `/sys/kernel/tracing/user_events_data`, then parse `/proc/mounts`
    to find the tracefs mount point (i.e. only parse `/proc/mounts` if the
    absolute path doesn't exist, and only look for tracefs, not debugfs).
  - Rationale: If `debugfs` is mounted then `tracefs` is always also mounted,
    so we don't ever need to look for `debugfs`. Probe the absolute path so
    that containers don't have to create a fake `/proc/mounts` and for startup
    efficiency.

## v1.2.1 (2023-07-24)

- Prefer `user_events_data` from `tracefs` over `user_events_data` from
  `debugfs`.

## v1.2 (2023-06-27)

- Added "Preregister" methods to the `TracepointCache` class so that a
  controller can pre-register events that it wants to collect.
- If no consumers have enabled a tracepoint, the kernel now returns `EBADF`.
  The provider APIs have been updated to be consistent with the new behavior.

## v1.1 (2023-06-20)

- Add namespaces to the C++ APIs.
- Move non-eventheader logic from eventheader-decode to new tracepoint-decode
  library.
- Add new libtracepoint-control library.
