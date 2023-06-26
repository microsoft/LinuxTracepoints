# LinuxTracepoints Change Log

## v1.2 (TBD)

- Kernel change means the "no sessions listening" case is now considered an
  error (`EBADF`). Update the comments/documentation and the early-out cases
  to match the kernel's new behavior.

## v1.1 (2023-06-20)

- Add namespaces to the C++ APIs.
- Move non-eventheader logic from eventheader-decode to new tracepoint-decode
  library.
- Add new libtracepoint-control library.
