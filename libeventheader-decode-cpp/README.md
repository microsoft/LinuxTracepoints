# libeventheader-decode-cpp

C++ library for decoding events that use the eventheader envelope.

- **[EventEnumerator.h](include/eventheader/EventEnumerator.h):**
  Splits an eventheader-encoded event into fields.
- **[EventFormatter.h](include/eventheader/EventFormatter.h):**
  Turns events or fields into strings.
- **[perf-decode](tools/perf-decode.cpp):**
  Tool that uses `EventFormatter` and `PerfDataFile` to decode a
  `perf.data` file into JSON text. Works on Linux or Windows.
