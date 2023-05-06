// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use core::ffi;
use core::fmt;
use core::fmt::Write;
use core::ops;
use core::pin;
use core::ptr;
use core::str;
use core::sync::atomic;

use crate::Level;
use crate::_internal;
use crate::descriptors::EventDataDescriptor;
use crate::descriptors::EventHeader;
use crate::native;

#[allow(unused_imports)] // For docs
#[cfg(feature = "macros")]
use crate::define_provider;

#[allow(unused_imports)] // For docs
#[cfg(feature = "macros")]
use crate::write_event;

/// A connection for writing compile-time-defined Linux tracepoints.
///
/// # Overview
///
/// 1. Use [`define_provider!`] to create a static provider variable.
/// 2. Call [`Provider::register()`] during component initialization to open the
///    connection.
/// 3. Use [`write_event!`] as needed to write events.
/// 4. Call [`Provider::unregister()`] during component cleanup to close the connection.
pub struct Provider<'a> {
    name: &'a [u8],
    options: &'a [u8],
    events: ops::Range<*mut *const EventHeaderTracepoint<'a>>,
    busy: atomic::AtomicBool,
}

impl<'a> Provider<'a> {
    /// Returns this provider's name.
    pub fn name(&self) -> &str {
        return str::from_utf8(self.name).unwrap();
    }

    /// Returns this provider's options, e.g. "" or "Gmygroup".
    pub fn options(&self) -> &str {
        return str::from_utf8(self.options).unwrap();
    }

    /// If this provider is not registered, does nothing and returns 0.
    /// Otherwise, unregisters all tracepoints in the provider.
    ///
    /// Returns 0 for success or an errno if any tracepoints failed to unregister. The
    /// return value is for diagnostic purposes only and should generally be ignored in
    /// retail builds.
    pub fn unregister(&self) -> u32 {
        let mut result = 0;

        let was_busy = self.busy.swap(true, atomic::Ordering::Relaxed);
        if !was_busy {
            let events_slice = unsafe {
                &*ptr::slice_from_raw_parts(
                    self.events.start,
                    self.events.end.offset_from(self.events.start) as usize,
                )
            };

            for &event_ptr in events_slice {
                if event_ptr.is_null() {
                    break;
                }

                let event = unsafe { &*event_ptr };
                let err = event.state.unregister();
                if result == 0 {
                    result = err;
                }
            }

            self.busy.swap(false, atomic::Ordering::Relaxed);
        }

        return result as u32;
    }

    /// Register the provider.
    ///
    /// Returns 0 for success or an errno if any tracepoints failed to register. The
    /// return value is for diagnostic purposes only and should generally be ignored in
    /// retail builds.
    ///
    /// # Preconditions
    ///
    /// - Provider must not already be registered. Verified at runtime, failure = panic.
    /// - For a given provider object, a call on one thread to the provider's `register`
    ///   method must not occur at the same time as a call to the same provider's
    ///   `register` or `unregister` method on any other thread. Verified at runtime,
    ///   failure = panic.
    ///
    /// # Safety
    ///
    /// - If creating a shared object that can unload or creating a provider that might
    ///   run as part of a shared object that can unload, all registered providers
    ///   **must** be unregistered before the shared object unloads.
    ///
    ///   If a provider variable is registered by a shared object and the shared object
    ///   unloads while the provider is still registered, the process may subsequently
    ///   crash. This occurs because `register` asks the kernel to automatically update
    ///   certain process variables, and `unregister` ensures that these updates are
    ///   disabled. If the shared objects unloads without disabling the updates, the
    ///   process memory will be corrupted if the kernel updates memory that gets
    ///   assigned to another variable in the process.
    ///
    ///   The provider cannot unregister itself because the provider is static and Rust
    ///   does not drop static objects.
    pub unsafe fn register(&self) -> u32 {
        return self.register_impl();
    }

    fn register_impl(&self) -> u32 {
        let mut result = 0;

        let was_busy = self.busy.swap(true, atomic::Ordering::Acquire);
        if was_busy {
            panic!("provider.register called simultaneously with another call to register or unregister.");
        }

        if self.events.start < self.events.end {
            let events_slice = unsafe {
                &mut *ptr::slice_from_raw_parts_mut(
                    self.events.start,
                    self.events.end.offset_from(self.events.start) as usize,
                )
            };

            // The list of tracepoints is frequently created using linker tricks.
            // The linker tricks mean we end up with NULLs and some duplicates, so
            // we need to fix up the list in-place:
            // - Sort the list so that duplicates are next to each other and NULLs
            //   are at the end.
            // - Remove adjacent duplicates, filling the rest of the list with NULL.

            // Sort list. Use reverse sort so that NULL goes at end.
            events_slice.sort_unstable_by(|a, b| b.cmp(a));

            // Remove adjacent repeated elements.
            let end_pos = events_slice.len();
            let mut good_pos = 0;
            while good_pos != end_pos - 1 {
                if events_slice[good_pos] == events_slice[good_pos + 1] {
                    let mut next_pos = good_pos + 2;
                    while next_pos != end_pos {
                        if events_slice[good_pos] != events_slice[next_pos] {
                            good_pos += 1;
                            events_slice[good_pos] = events_slice[next_pos];
                        }
                        next_pos += 1;
                    }
                    break;
                }
                good_pos += 1;
            }

            // Fill remaining entries with NULL.
            let mut next_pos = good_pos + 1;
            while next_pos != end_pos {
                events_slice[next_pos] = ptr::null();
                next_pos += 1;
            }

            // Register all of the tracepoints.
            let mut command_string = CommandString::new();
            for &mut event_ptr in events_slice {
                if event_ptr.is_null() {
                    break;
                }

                let event = unsafe { &*event_ptr };
                let name_args = command_string.format(
                    self.name,
                    self.options,
                    event.header.level,
                    event.keyword,
                );
                let err = unsafe { pin::Pin::new_unchecked(&event.state).register(name_args) };
                if result == 0 {
                    result = err;
                }
            }
        }

        self.busy.swap(false, atomic::Ordering::Release);
        return result as u32;
    }
}

unsafe impl Sync for Provider<'_> {}

impl Drop for Provider<'_> {
    fn drop(&mut self) {
        self.unregister();
    }
}

impl fmt::Debug for Provider<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        return write!(
            f,
            "Provider {{ name: \"{}\", options: {} }}",
            self.name(),
            self.options(),
        );
    }
}

/// Stores the information needed for registering and managing a tracepoint.
pub struct EventHeaderTracepoint<'a> {
    state: native::TracepointState,
    header: EventHeader,
    keyword: u64,
    metadata: &'a [u8],
}

impl<'a> EventHeaderTracepoint<'a> {
    /// Sets up the data for managing a tracepoint.
    pub const fn new(header: EventHeader, keyword: u64, metadata: &'a [u8]) -> Self {
        return Self {
            state: native::TracepointState::new(0),
            header,
            keyword,
            metadata,
        };
    }

    /// Returns true if this tracepoint is registered and enabled.
    #[inline(always)]
    pub fn enabled(&self) -> bool {
        return self.state.enabled();
    }

    /// Fills in `data[0]` with the event's header information,
    /// fills in `data[1]` with the event's metadata, then sends
    /// the event to the `user_events_data` file.
    ///
    /// Requires:
    /// - `data[0].is_empty()` since it will be used for the headers.
    /// - `data[1].is_empty()` since it will be used for the metadata.
    /// - related_id may only be present if activity_id is present.
    /// - if activity_id.is_some() || meta_len != 0 then event_header.flags
    ///   must equal DefaultWithExtension.
    pub fn write_eventheader<'b>(
        &self,
        activity_id: Option<&[u8; 16]>,
        related_id: Option<&[u8; 16]>,
        data: &mut [EventDataDescriptor<'b>],
    ) -> i32
    where
        'a: 'b,
    {
        debug_assert!(data[1].is_empty());
        data[1] = EventDataDescriptor::<'a>::from_bytes(self.metadata);
        return self.state.write_eventheader(
            &self.header,
            activity_id,
            related_id,
            self.metadata.len() as u16,
            data,
        );
    }
}

struct CommandStringBuffer {
    buf: [u8; _internal::EVENTHEADER_COMMAND_MAX],
    pos: usize,
}

impl CommandStringBuffer {
    fn write(&mut self, bytes: &[u8]) {
        self.buf[self.pos..self.pos + bytes.len()].copy_from_slice(bytes);
        self.pos += bytes.len();
    }
}

impl Write for CommandStringBuffer {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        self.write(s.as_bytes());
        return fmt::Result::Ok(());
    }
}

/// Helper for creating a command string for registering a tracepoint, e.g.
/// `ProviderName_L1K1f u8 eventheader_flags;u8 version;u16 id;u16 tag;u8 opcode;u8 level`.
pub struct CommandString(CommandStringBuffer);

impl CommandString {
    /// Creates a CommandString object.
    pub const fn new() -> Self {
        return Self(CommandStringBuffer {
            buf: [0; _internal::EVENTHEADER_COMMAND_MAX],
            pos: 0,
        });
    }

    /// Gets the CStr for the specified parameters:
    /// `ProviderName_LnnKnn... u8 eventheader_flags;u8 version;u16 id;u16 tag;u8 opcode;u8 level`.
    pub fn format(
        &mut self,
        provider_name: &[u8],
        provider_options: &[u8],
        level: Level,
        keyword: u64,
    ) -> &ffi::CStr {
        self.0.pos = 0;
        self.0.write(provider_name); // "ProviderName"
        write!(self.0, "_L{:x}K{:x}", level.as_int(), keyword).unwrap(); // "_LxKx"
        self.0.write(provider_options); // "Options"
        write!(self.0, " {}", _internal::EVENTHEADER_COMMAND_TYPES).unwrap(); // " CommandTypes"
        self.0.buf[self.0.pos] = b'\0';
        self.0.pos += 1;
        return ffi::CStr::from_bytes_with_nul(&self.0.buf[0..self.0.pos]).unwrap();
    }
}

/// For use by the define_provider macro: creates a new provider.
///
/// # Safety
///
/// - events_start..events_stop must be a valid range of valid pointers that
///   remain valid for the lifetime of the returned Provider.
pub const unsafe fn provider_new<'a>(
    name: &'a [u8],
    options: &'a [u8],
    events: ops::Range<*mut *const EventHeaderTracepoint<'a>>,
) -> Provider<'a> {
    return Provider {
        name,
        options,
        events,
        busy: atomic::AtomicBool::new(false),
    };
}
