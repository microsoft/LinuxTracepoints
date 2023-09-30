# LinuxTracepoints Change Log

## v1.3.0 (TBD)

- `PerfEventInfo.h` adds a field with session information to the metadata of
  each event. The session information includes clock information.
- `PerfDataFile.h` decodes clock information from perf.data files if present.
- `TracepointSession.h` records clock information from the session.
- `EventFormatter.h` formats timestamps as date-time if clock information is
  available in the event metadata. If clock information is not present, it
  continues to format timestamps as seconds.

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
