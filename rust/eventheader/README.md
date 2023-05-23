# EventHeader for Rust

The `eventheader` crate provides a simple and efficient way to log
`EventHeader`-encoded
[Tracepoints](https://www.kernel.org/doc/html/latest/trace/tracepoints.html)
via the Linux [user_events](https://docs.kernel.org/trace/user_events.html)
system. The events can be generated and collected on Linux 6.4 or later
(requires the `user_events` kernel feature to be enabled, the `tracefs` or
`debugfs` filesystem to be mounted, and appropriate permissions configured for
the `/sys/kernel/.../tracing/user_events_data` file).

This crate uses macros to generate event metadata at compile-time, improving
runtime performance and minimizing dependencies. To enable compile-time
metadata generation, the event schema must be specified at compile-time. For
example, event name and field names must be string literals, not variables.

In rare cases, you might not know what events you want to log until runtime.
For example, you might be implementing a middle-layer library providing event
support to a dynamic top-layer or a scripting language like JavaScript or
Python. In these cases, you might use the `eventheader_dynamic` crate instead
of this crate.
