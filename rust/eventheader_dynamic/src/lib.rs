// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#![no_std]
#![warn(missing_docs)]
#![allow(clippy::needless_return)]

//! # Dynamic EventHeader-encoded Linux Tracepoints
//!
//! `eventheader_dynamic` provides a flexible way to log `EventHeader`-encoded
//! events to [Linux user_events](https://docs.kernel.org/trace/user_events.html).
//! The events can be generated and collected on Linux 6.4 or later (requires the
//! `user_events` kernel feature to be enabled, the `tracefs` or `debugfs`
//! filesystem to be mounted, and appropriate permissions configured for the
//! `/sys/kernel/.../tracing/user_events_data` file).
//!
//! This "dynamic" implementation is more flexible than the implementation in the
//! [`eventheader`] crate. For example, it supports runtime-defined schema and can
//! easily log arrays of strings. However, it is harder to use, it has higher runtime
//! costs, and it depends on the `alloc` crate. This dynamic implementation is intended
//!  for use only when the set of events cannot be determined at compile-time. For
//! example, `eventheader_dynamic` might be used to implement a middle-layer library
//! providing tracing support to a scripting language like JavaScript or
//! Python. In other cases, use the [`eventheader`] crate instead of this crate.
//!
//! # Overview
//!
//! - Create a [Provider] object with the name of the provider to be used.
//! - Use the provider to register an [EventSet] for each level/keyword combination
//!   needed.
//! - Use [EventSet::enabled] to determine whether the event set is enabled.
//! - If the event set is enabled, use an [EventBuilder] to build the event
//!   (setting event properties and adding event fields) then write the event
//!   to the [EventSet].
//! - The provider will automatically unregister when it is dropped. You can manually
//!   call [Provider::unregister] if you want to unregister sooner or if the provider
//!   is stored within a static variable.
//!
//! # Example
//!
//! ```
//! use eventheader_dynamic as ehd;
//!
//! // Create a provider to use for all "MyCompany_MyComponent" events.
//! let mut provider = ehd::Provider::new("MyCompany_MyComponent", &ehd::Provider::new_options());
//!
//! // Create an event_set to use for all "MyCompany_MyComponent" events with severity
//! // level Verbose and event category bits 0x1f.
//! let event_l5k1f = provider.register_set(ehd::Level::Verbose, 0x1f);
//!
//! // If nobody is listening for events from this event set, the write method will do nothing.
//! // It is more efficient to only build and write the event if the event set is enabled.
//! if event_l5k1f.enabled() {
//!     let field1_value = "FieldValue";
//!     let field2_value = b'A';
//!
//!     // Build and write an event with two fields:
//!     ehd::EventBuilder::new()
//!         // Most events specify 0 for event tag.
//!         .reset("MyEventName", 0)
//!         // Most fields use 0 for field tag.
//!         .add_str("FieldName1", field1_value, ehd::FieldFormat::Default, 0)
//!         .add_value("FieldName2", field2_value, ehd::FieldFormat::String8, 0)
//!         // activity_id is None indicating event is not part of an activity.
//!         // related_id is None indicating event does not specify a parent activity.
//!         .write(&event_l5k1f, None, None);
//! }
//! ```
//!
//! # Notes
//!
//! The [EventBuilder] object is reusable. You may get a small performance benefit by
//! reusing an EventBuilder object for multiple events rather than using a new
//! EventBuilder for each event.
//!
//! Events are limited in size (event size = headers + metadata + data). The kernel will
//! ignore any event that is larger than 64KB.
//!
//! All event sets registered with a provider will become unregistered when the provider
//! is dropped or when you call `provider.unregister()`.
//!
//! Each event set maps to one tracepoint name, e.g. if the provider name is
//! "MyCompany_MyComponent", level is Verbose, and category bits are 0x1f, the event set
//! will correspond to a tracepoint named "MyCompany_MyComponent_L5K1f".
//!
//! Collect events to a file using a tool such as `perf`, e.g.
//! `perf record -e user_events:MyCompany_MyComponent_L5K1f`.
//!
//! Decode events using a tool such as `decode-perf`.

// Re-exports from eventheader:
pub use eventheader::FieldEncoding;
pub use eventheader::FieldFormat;
pub use eventheader::Level;
pub use eventheader::NativeImplementation;
pub use eventheader::Opcode;
pub use eventheader::NATIVE_IMPLEMENTATION;

// Exports from eventheader_dynamic:
pub use builder::EventBuilder;
pub use provider::EventSet;
pub use provider::Provider;
pub use provider::ProviderOptions;

pub mod changelog;

/// Converts a
/// [`std::time::SystemTime`](https://doc.rust-lang.org/std/time/struct.SystemTime.html)
/// into a [`time_t`](https://en.wikipedia.org/wiki/Unix_time) `i64` value.
///
/// This macro will convert the provided `SystemTime` value into a signed 64-bit
/// integer storing the number of seconds since 1970, saturating if the value is
/// out of the range that a 64-bit integer can represent.
///
/// The returned `i64` value can be used with [`EventBuilder::add_value`] and
/// [`EventBuilder::add_value_sequence`].
///
/// Note: `time_from_systemtime` is implemented as a macro because this crate is
/// `[no_std]`. Implementing this via a function would require this crate to reference
/// `std::time::SystemTimeError`.
#[macro_export]
macro_rules! time_from_systemtime {
    // Keep in sync with eventheader::time_from_systemtime.
    // The implementation is duplicated to allow for different doc comments.
    ($time:expr) => {
        match $time.duration_since(::std::time::SystemTime::UNIX_EPOCH) {
            Ok(dur) => ::tracelogging::_internal::time_from_duration_after_1970(dur),
            Err(err) => ::tracelogging::_internal::time_from_duration_before_1970(err.duration()),
        }
    };
}

extern crate alloc;
mod builder;
mod provider;
