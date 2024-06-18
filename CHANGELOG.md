# LinuxTracepoints Change Log

## v1.4.0 (TBD)

- libtracepoint-control: New `tracepoint-collect` tool that records tracepoint
  events into a perf.data file.
- libtracepoint-control: TracepointSession SavePerfDataFile adds a
  `PERF_RECORD_FINISHED_INIT` record to the generated perf.data file.
- libeventheader: tool `eventheader-register` deleted. Instead, use
  `tracepoint-register` from libtracepoint.
- New field encoding `event_field_encoding_binary_length16_char8`. Same as
  `event_field_encoding_string_length16_char8` except that its default format
  is `hex_bytes`.
- New semantics for `event_field_encoding_binary_length16_char8` and
  `event_field_encoding_string_length16_char8` encodings to support nullable
  and variable-length fields. These encodings can now be used with any format.
  When used with a fixed-size format, this indicates a nullable field. For
  example, a field with encoding `binary_length16_char8` and format
  `signed_int` with length 1, 2, 4, or 8 would be formatted as a signed
  integer. The same field with length 0 would be formatted as a `null`. Any
  other length would be formatted as `hex_bytes`.
- Deprecated `ipv4` and `ipv6` formats. New code should use the `ip_address`
  format. When applied to a 4-byte field, `ip_address` should format as IPv4,
  and when applied to a 16-byte field, `ip_address` should format as IPv6.
- Decoding: When converting UTF-16 field values to UTF-8 output, treat the
  value as UTF-16 (previously treated as UCS-2). To aid in debugging, unmatched
  surrogate pairs still pass-through.
- Decoding: When processing UTF-8 fields, check for valid UTF-8 input. To aid
  in debugging, unmatched surrogate pairs still pass-through. Treat all other
  invalid UTF-8 sequences as Latin-1 sequences, i.e. if input is
  "valid UTF-8 A|non-UTF-8 B|valid UTF-8 C" the output will be
  "valid UTF-8 A|ConvertLatin1ToUtf8(B)|valid UTF-8 C".
- Decoding: Windows decoder now generates fully-normalized IPv6 strings for
  IPv6 fields, matching existing behavior of the Linux decoder.
- Decoding: Use `std::to_chars` instead of `snprintf` to convert floating-point
  field values to strings. This typically results in shorter strings, e.g. a
  float32 value might now decode to "3.14" instead of "3.1400001" because
  `std::from_chars<float>("3.14") == std::from_chars<float>("3.1400001")`.
- EventHeaderDynamic.h: Add support for generating fields with  `binary`
  encoding.

## v1.3.3 (2024-04-15)

- BUG FIX: EADDRINUSE returned during TraceLoggingRegister on newer kernels.
  The "name already in use" detection splits on whitespace, while all other
  processing splits on semicolon. Fix by adding space after each semicolon
  in `EVENTHEADER_COMMAND_TYPES`.
- libtracepoint-decode: In pipe mode, load event names at FinishedInit instead
  of HeaderLastFeature since not all traces emit HeaderLastFeature.
- libtracepoint-decode: Recognize files from LP32 systems as 32-bit.
- libtracepoint: new tool `tracepoint-register` for pre-registering
  tracepoints.
- libeventheader: existing tool `eventheader-register` is deprecated in
  favor of `tracepoint-register`.
- libeventheader-decode-dotnet: Moved to separate repository
  [LinuxTracepoints-Net](https://github.com/microsoft/LinuxTracepoints-Net).

## v1.3.2 (2024-02-27)

- Bug fix: Open `user_events_data` for `O_WRONLY` instead of `O_RDWR`.

## v1.3.1 (2024-01-11)

- `TracepointSession` supports per-CPU buffer sizes (including 0) to allow
  memory usage optimization when trace producers are known to be bound to
  specific CPUs.
- `TracepointSession` uses `PERF_ATTR_SIZE_VER3` for the size of
  `perf_event_attr` to minimize the chance of incompatibilities.

## v1.3.0 (2023-11-27)

- **Breaking changes** to `PerfDataFile`:
  - `dataFile.AttrCount()` method replaced by `EventDescCount()` method.
  - `dataFile.Attr(index)` method replaced by `EventDesc(index)` method.
    The returned `PerfEventDesc` object contains an `attr` pointer.
  - `dataFile.EventDescById(id)` method replaced by `FindEventDescById(id)`.
- **Breaking changes** to `PerfSampleEventInfo`:
  - `eventInfo.session` field renamed to `session_info`.
  - `eventInfo.attr` field replaced by `Attr()` method.
  - `eventInfo.name` field replaced by `Name()` method.
  - `eventInfo.sample_type` field replaced by `SampleType()` method.
  - `eventInfo.raw_meta` field replaced by `Metadata()` method.
- **Breaking changes** to `TracepointSession`:
  - `session.EnableTracePoint(...)` method renamed to `EnableTracepoint(...)`.
  - `session.DisableTracePoint(...)` method renamed to `DisableTracepoint(...)`.
- `EventFormatter` formats timestamps as date-time if clock information is
  available in the event metadata. If clock information is not present, it
  continues to format timestamps as seconds.
- `TracepointSession` provides `SavePerfDataFile(filename)` method to save
  the current contents of the session buffers into a `perf.data` file.
- `TracepointSession` now includes ID in default sample type.
- `TracepointSession` records clock information from the session.
- `TracepointSession` provides access to information about the tracepoints
   that have been added to the session (metadata, status, statistics).
- `PerfDataFile` decodes clock information from perf.data files if present.
- `PerfDataFile` provides access to more metadata via `PerfEventDesc` struct.
- `PerfDataFile` provides `EventDataSize` for determining the size of an event.
- New `PerfDataFileWriter` class for generating `perf.data` files.
- Changed procedure for locating the `user_events_data` file.
  - Old: parse `/proc/mounts` to determine the `tracefs` or `debugfs` mount
    point, then use that as the root for the `user_events_data` path.
  - New: try `/sys/kernel/tracing/user_events_data`; if that doesn't exist,
    parse `/proc/mounts` to find the `tracefs` or `debugfs` mount point.
  - Rationale: Probe an absolute path so that containers don't have to
    create a fake `/proc/mounts` and for efficiency in the common case.

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
