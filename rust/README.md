# EventHeader for Rust

- [eventheader](eventheader) provides an efficient high-level macro-based API
  for generating compile-time specified events using the `EventHeader`
  convention.  The events are written using the `user_events` system. This is
  intended for use by developers that want to log events from their code.  This
  crate also contains utility code shared with `eventheader_dynamic`.
- [eventheader_dynamic](eventheader_dynamic) provides a mid-level API
  for generating runtime-specified events using the `EventHeader`
  convention. The events are written using the `user_events` system.
  This is intended for use as an implementation layer for a higher-level
  API like OpenTelemetry.
- [eventheader_macros](eventheader_macros) provides proc macros for
  compile-time-defined events. The macros are exposed by the
  `eventheader` crate.
