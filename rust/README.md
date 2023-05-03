# EventHeader for Rust

- [eventheader](eventheader) is a utility crate implementing tracing
  infrastructure for writing Linux Tracepoints via `user_events`.
  (Generally not to be used directly.)
- [eventheader_dynamic](eventheader_dynamic) provides a mid-level API
  for generating runtime-specified events using the `EventHeader`
  convention. The events are written using the `user_events` system.
  This is intended for use as an implementation layer for a higher-level
  API like OpenTelemetry.
