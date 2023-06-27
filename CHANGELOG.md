# LinuxTracepoints Change Log

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
