// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#![no_std]
#![allow(clippy::needless_return)]
#![warn(missing_docs)]

//! # EventHeader-encoded Linux Tracepoints
//!
//! ## EventHeader Technical Details
//!
//! `EventHeader` is a tracing convention layered on top of Linux Tracepoints.
//!
//! To reduce the number of unique Tracepoint names tracked by the kernel, we
//! use a small number of Tracepoints to manage a larger number of events. All
//! events with the same attributes (provider name, severity level, category
//! keyword, etc.) will share one Tracepoint.
//!
//! - This means we cannot enable/disable events individually. Instead, all events
//!   with similar attributes will be enabled/disabled as a group.
//! - This means we cannot rely on the kernel's Tracepoint metadata for event
//!   identity or event field names/types. Instead, all events contain a common
//!   header that provides event identity and optional field information. The
//!   kernel's Tracepoint metadata is used only for the Tracepoint's name and to
//!   determine whether the event follows the EventHeader conventions.
//!
//! We define a naming scheme to be used for the shared Tracepoint:
//!
//!   `ProviderName + '_' + 'L' + eventLevel + 'K' + eventKeyword + [Options]`
//!
//! We define a common event layout to be used by all EventHeader events. The
//! event has a header, optional header extensions, and then the event data:
//!
//!   `Event = eventheader + [HeaderExtensions] + Data`
//!
//! We define a format to be used for header extensions:
//!
//!   `HeaderExtension = eventheader_extension + ExtensionData`
//!
//! We define a header extension to be used for activity IDs.
//!
//! We define a header extension to be used for event metadata (event name, field
//! names, field types).
//!
//! For use in the event metadata extension, we define a field type system that
//! supports scalar, string, binary, array, and struct.
//!
//! Note that we assume that the Tracepoint name corresponding to the event is
//! available during event decoding. The event decoder obtains the provider name
//! and keyword for an event by parsing the event's Tracepoint name.
//!
//! ### Provider Names
//!
//! A provider is a component that generates events. Each event from a provider is
//! associated with a Provider Name that uniquely identifies the provider.
//!
//! The provider name should be short, yet descriptive enough to minimize the
//! chance of collision and to help developers track down the component generating
//! the events. Hierarchical namespaces may be useful for provider names, e.g.
//! `"MyCompany_MyOrg_MyComponent"`.
//!
//! Restrictions:
//!
//! - ProviderName may not contain `' '` or `':'` characters.
//! - `strlen(ProviderName + '_' + Attributes)` must be less than 256 characters.
//! - Some event APIs (e.g. tracefs) might impose additional restrictions on
//!   tracepoint names. For best compatibility, use only ASCII identifier
//!   characters `[A-Za-z0-9_]` in provider names.
//!
//! Event attribute semantics should be consistent within a given provider. While
//! some event attributes have generally-accepted semantics (e.g. level value 3
//! is defined below as "warning"), the precise semantics of the attribute values
//! are defined at the scope of a provider (e.g. different providers will use
//! different criteria for what constitutes a warning). In addition, some
//! attributes (tag, keyword) are completely provider-defined. All events with a
//! particular provider name should use consistent semantics for all attributes
//! (e.g. keyword bit `0x1` should have a consistent meaning for all events from a
//! particular provider but will mean something different for other providers).
//!
//! ### Tracepoint Names
//!
//! A Tracepoint is registered with the kernel for each unique combination of
//! `ProviderName + Attributes`. This allows a larger number of distinct events to
//! be controlled by a smaller number of kernel Tracepoints while still allowing
//! events to be enabled/disabled at a reasonable granularity.
//!
//! The Tracepoint name for an EventHeader event is defined as:
//!
//!   `ProviderName + '_' + 'L' + eventLevel + 'K' + eventKeyword + [Options]`
//!   or `printf("%s_L%xK%lx%s", providerName, eventLevel, eventKeyword, options)`,
//!   e.g. `"MyProvider_L3K2a"` or `"OtherProvider_L5K1fGperf"`.
//!
//! Event level is a uint8 value 1..255 indicating event severity, formatted as
//! lowercase hexadecimal, e.g. `printf("L%x", eventLevel)`. The defined level values
//! are: 1 = critical error, 2 = error, 3 = warning, 4 = information, 5 = verbose.
//!
//! Event keyword is a uint64 bitmask indicating event category membership,
//! formatted as lowercase hexadecimal, e.g. `printf("K%lx", eventKeyword)`. Each
//! bit in the keyword corresponds to a provider-defined category, e.g. a provider
//! might define 0x2 = networking and 0x4 = I/O so that keyword value of `0x2|0x4` =
//! `0x6` would indicate that an event is in both the networking and I/O categories.
//!
//! Options (optional attributes) can be specified after the keyword attribute.
//! Each option consists of an uppercase ASCII letter (option type) followed by 0
//! or more ASCII digits or lowercase ASCII letters (option value). To support
//! consistent event names, the options must be sorted in alphabetical order, e.g.
//! `"Aoption"` should come before `"Boption"`.
//!
//! The currently defined options are:
//!
//! - `'G'` = provider Group name. Defines a group of providers. This can be used by
//!   event analysis tools to find all providers that generate a certain kind of
//!   information.
//!
//! Restrictions:
//!
//! - ProviderName may not contain `' '` or `':'` characters.
//! - Tracepoint name must be less than 256 characters in length.
//! - Some event APIs (e.g. tracefs) might impose additional restrictions on
//!   tracepoint names. For best compatibility, use only ASCII identifier
//!   characters `[A-Za-z0-9_]` in provider names.
//!
//! ### Header
//!
//! Because multiple events may share a single Tracepoint, each event must contain
//! information needed to distinguish it from other events. To enable this, each
//! event starts with an `EventHeader` structure which contains information about
//! the event:
//!
//! - `flags`: Bits indicating pointer size (32 or 64 bits), byte order
//!   (big-endian or little), and whether any header extensions are present.
//! - `opcode`: Indicates special event semantics e.g. "normal event",
//!   "activity start event", "activity end event".
//! - `tag`: Provider-defined 16-bit value. Can be used for anything.
//! - `id`: 16-bit stable event identifier, or 0 if no identifier is assigned.
//! - `version`: 8-bit event version, incremented for e.g. field type changes.
//! - `level`: 8-bit event severity level, 1 = critical .. 5 = verbose.
//!   (level value in event header must match the level in the Tracepoint name.)
//!
//! If the `Extension` flag is not set, the header is immediately followed by the
//! event payload.
//!
//! If the `Extension` flag is set, the header is immediately followed by one or more
//! `EventHeaderExtension` blocks. Each header extension has a 16-bit size, a 15-bit type code,
//! and a 1-bit flag indicating whether another header extension block follows the
//! current extension. The final header extension block is immediately followed by the
//! event payload.
//!
//! The following header extensions are defined:
//!
//! - Activity ID: Contains a 128-bit ID that can be used to correlate events. May
//!   also contain the 128-bit ID of the parent activity (typically used only for the
//!   first event of an activity).
//! - Metadata: Contains the event's metadata - event name, event attributes, field
//!   names, field attributes, and field types. Both simple (e.g. Int32, HexInt16,
//!   Float64, Char32, Uuid) and complex (e.g. NulTerminatedString8,
//!   CountedString16, Binary, Struct, Array) types are supported.

/// define_provider
#[cfg(feature = "macros")]
pub use eventheader_macros::define_provider;

/// write_event
#[cfg(feature = "macros")]
pub use eventheader_macros::write_event;

pub use enums::FieldEncoding;
pub use enums::FieldFormat;
pub use enums::Level;
pub use enums::Opcode;
pub use guid::Guid;
pub use native::NativeImplementation;
pub use native::NATIVE_IMPLEMENTATION;
pub use provider::Provider;
pub mod _internal;
pub mod changelog;

/// Converts a
/// [`std::time::SystemTime`](https://doc.rust-lang.org/std/time/struct.SystemTime.html)
/// into a [`time_t`](https://en.wikipedia.org/wiki/Unix_time) `i64` value.
/// (Usually not needed - the `systemtime` field type does this automatically.)
///
/// This macro will convert the provided `SystemTime` value into a signed 64-bit
/// integer storing the number of seconds since 1970, saturating if the value is
/// out of the range that a 64-bit integer can represent.
///
/// The returned `i64` value can be used with [`write_event!`] via the `posix_time64`
/// and `posix_time64_slice` field types. As an alternative, you can use the `systemtime`
/// field type, which will automatically convert the provided
/// `std::time::SystemTime` value into a `time_t` before writing the event.
///
/// Note: `time_from_systemtime` is implemented as a macro because this crate is
/// `[no_std]`. Implementing this via a function would require this crate to reference
/// `std::time::SystemTimeError`.
#[macro_export]
macro_rules! time_from_systemtime {
    // Keep in sync with eventheader_dynamic::time_from_systemtime.
    // The implementation is duplicated to allow for different doc comments.
    ($time:expr) => {
        match $time.duration_since(::std::time::SystemTime::UNIX_EPOCH) {
            Ok(dur) => ::tracelogging::_internal::time_from_duration_after_1970(dur),
            Err(err) => ::tracelogging::_internal::time_from_duration_before_1970(err.duration()),
        }
    };
}

mod descriptors;
mod enums;
mod guid;
mod native;
mod provider;
