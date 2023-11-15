# LinuxTracepoints Change Log

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
- `TracepointSession` supports saving/restoring session state to/from a list
  of file handles (i.e. an fdstore) so that a session can be transferred
  between processes.
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
