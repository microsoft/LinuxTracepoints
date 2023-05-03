# Libraries for Linux user_events

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

## Contributing

This project welcomes contributions and suggestions.  Most contributions require you to agree to a
Contributor License Agreement (CLA) declaring that you have the right to, and actually do, grant us
the rights to use your contribution. For details, visit https://cla.opensource.microsoft.com.

When you submit a pull request, a CLA bot will automatically determine whether you need to provide
a CLA and decorate the PR appropriately (e.g., status check, comment). Simply follow the instructions
provided by the bot. You will only need to do this once across all repos using our CLA.

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).
For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or
contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.

## Trademarks

This project may contain trademarks or logos for projects, products, or services. Authorized use of Microsoft 
trademarks or logos is subject to and must follow 
[Microsoft's Trademark & Brand Guidelines](https://www.microsoft.com/en-us/legal/intellectualproperty/trademarks/usage/general).
Use of Microsoft trademarks or logos in modified versions of this project must not cause confusion or imply Microsoft sponsorship.
Any use of third-party trademarks or logos are subject to those third-party's policies.
