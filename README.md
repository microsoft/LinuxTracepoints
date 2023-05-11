# Libraries for Linux Tracepoints and user_events

This repository contains libraries for using the Linux
[user_events](https://docs.kernel.org/trace/user_events.html) facility to
generate tracepoints from user mode.

## Overview

- [libtracepoint](libtracepoint) -
  low-level C/C++ tracing interface. Designed to support replacement at
  link-time if a different implementation is needed.
  - [Default implementation](libtracepoint/src/tracepoint.c)
    writes directly to the Linux `user_events` facility.
  - [tracepoint-provider.h](libtracepoint/include/tracepoint/tracepoint-provider.h) -
    high-level C/C++ API for writing tracepoint events to any implementation
    of the tracepoint interface.
- [libeventheader-tracepoint](libeventheader-tracepoint) -
  `eventheader` envelope that supports extended attributes including severity
  level and optional field type information.
  - [TraceLoggingProvider.h](libeventheader-tracepoint/include/eventheader/TraceLoggingProvider.h) -
    high-level C/C++ API for writing `eventheader`-encapsulated events to any
    implementation of the tracepoint interface.
  - [EventHeaderDynamic.h](libeventheader-tracepoint/include/eventheader/EventHeaderDynamic.h) -
    mid-level C++ API for writing `eventheader`-encapsulated events, intended for
    use as an implementation layer for a higher-level API like OpenTelemetry.
- [libeventheader-decode-cpp](libeventheader-decode-cpp) -
  C++ library for decoding events that use the `eventheader` envelope.
  - `decode-perf` tool that decodes `perf.data` files to JSON.
- [libeventheader-decode-dotnet](libeventheader-decode-dotnet) -
  .NET library for decoding events that use the `eventheader` envelope.
- [Rust](rust) - API for generating `eventheader`-encapsulated events from Rust.

## General Usage

- Configure a Linux system with the `user_events` feature enabled.
  - Supported on Linux kernel 6.4 and later.
  - Kernel must be built with `user_events` support (`CONFIG_USER_EVENTS=y`).
  - Must have either `tracefs` or `debugfs` mounted. For example, you might add
    the following line to your `/etc/fstab` file:
    `tracefs /sys/kernel/tracing tracefs defaults 0 0`
  - The user that will generate events must have `x` access to the `tracing`
    directory, e.g. `chmod a+x /sys/kernel/tracing`
  - The user that will generate events must have `rw` access to the
    `tracing/user_events_data` file, e.g.
    `chmod a+rw /sys/kernel/tracing/user_events_data`
- Use one of the event generation APIs to write a program that generates events.
  - C/C++ programs can use
    [tracepoint-provider.h](libtracepoint/include/tracepoint/tracepoint-provider.h)
    to generate regular Linux Tracepoint events that are defined at compile-time.
    (Link with `libtracepoint`.)
  - C/C++ programs can use
    [TraceLoggingProvider.h](libeventheader-tracepoint/include/eventheader/TraceLoggingProvider.h)
    to generate eventheader-enabled Tracepoint events that are defined at
    compile-time. (Link with `libtracepoint` and `libeventheader-tracepoint`.)
  - C++ middle-layer APIs (e.g. an OpenTelemetry exporter) can use
    [EventHeaderDynamic.h](libeventheader-tracepoint/include/eventheader/EventHeaderDynamic.h)
    to generate eventheader-enabled Tracepoint events that are runtime-dynamic.
    (Link with `libtracepoint` and `libeventheader-tracepoint`.)
  - Rust middle-layer APIs (e.g. an OpenTelemetry exporter) can use the
    [eventheader_dynamic](rust/eventheader_dynamic/README.md) crate
    to generate eventheader-enabled Tracepoint events that are defined at
    compile-time.
- Running as a privileged user, use the Linux
  [`perf`](https://www.man7.org/linux/man-pages/man1/perf.1.html) tool
  to collect events to a `perf.data` file, e.g.
  `perf record -e user_events:MyEvent1,user_events:MyEvent2`.
  - The `perf` tool binary is typically available as part of the `linux-perf`
    package (e.g. can be installed by `apt install linux-perf`). However, this
    package installs a `perf_VERSION` binary, so you will need to add an
    appropriate VERSION suffix to your `perf` commands or use a wrapper script.
  - The `linux-base` package installs a `perf` wrapper script that redirects to
    the version of `perf` that matches your current kernel (if present) so that
    can run the appropriate version of `perf` without the VERSION suffix. This
    frequently doesn't work because the latest `perf` binary from `apt` doesn't
    always match the running kernel, so you may want to make your own wrapper
    script instead.
  - Note that for purposes of collecting events, it is usually not important
    for the version of the `perf` tool to match the kernel version, so it's
    ok to use e.g. `perf_5.10` even if you are running a newer kernel.
- Note that the events must have been registered before you can start
  collecting them. The `perf` command will report an error if the event is not
  yet registered.
  - For regular Linux `user_events` Tracepoints, you might need to run the
    program that generates the events once before the events will be available
    for collection.
  - For eventheader-enabled Tracepoint events, you can use the
    [`eventheader-register`](libeventheader-tracepoint/samples/eventheader-register.cpp)
    tool to pre-register an event based on its tracepoint name.
- Use the [`decode-perf`](libeventheader-decode-cpp/samples/decode-perf.cpp)
  tool to decode the `perf.data` file to JSON text.

## Contributing

This project welcomes contributions and suggestions.  Most contributions require you to agree to a
Contributor License Agreement (CLA) declaring that you have the right to, and actually do, grant us
the rights to use your contribution. For details, visit [https://cla.opensource.microsoft.com](https://cla.opensource.microsoft.com).

When you submit a pull request, a CLA bot will automatically determine whether you need to provide
a CLA and decorate the PR appropriately (e.g., status check, comment). Simply follow the instructions
provided by the bot. You will only need to do this once across all repos using our CLA.

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).
For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or
contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.

## Trademarks

This project may contain trademarks or logos for projects, products, or services. Authorized use of Microsoft
trademarks or logos is subject to and must follow
[Microsoft's Trademark & Brand Guidelines](https://www.microsoft.com/legal/intellectualproperty/trademarks/usage/general).
Use of Microsoft trademarks or logos in modified versions of this project must not cause confusion or imply Microsoft sponsorship.
Any use of third-party trademarks or logos are subject to those third-party's policies.
