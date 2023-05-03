# libeventheader-decode-cpp

C++ library for decoding events that use the eventheader envelope.

- **[EventEnumerator.h](include/eventheader/EventEnumerator.h):**
  Splits an eventheader-encoded event into fields.
- **[EventFormatter.h](include/eventheader/EventFormatter.h):**
  Turns event fields into strings.
- **[PerfDataFile.h](include/PerfDataDecode/PerfDataFile.h):**
  Splits a `perf.data` file into events. Works on Linux or Windows.
- **[PerfEventMetadata.h](include/PerfDataDecode/PerfEventMetadata.h):**
  Parses a tracefs `format` file to get decoding information (metadata) for a
  tracepoint.
- **[decode-perf](samples/decode-perf.cpp):**
  Simple tool that uses `EventFormatter` and `PerfDataFile` to decode a
  `perf.data` file into JSON text. Works on Linux or Windows.
