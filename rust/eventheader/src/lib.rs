// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#![no_std]
#![allow(clippy::needless_return)]
#![warn(missing_docs)]

//! # EventHeader-encoded Linux Tracepoints
//! 
//! The `eventheader` crate provides a simple and efficient way to log
//! EventHeader-encoded
//! [Tracepoints](https://www.kernel.org/doc/html/latest/trace/tracepoints.html)
//! via the Linux [user_events](https://docs.kernel.org/trace/user_events.html)
//! system.
//! 
//! This crate uses macros to generate event metadata at compile-time, improving runtime
//! performance and minimizing dependencies. To enable compile-time metadata generation,
//! the event schema must be specified at compile-time. For example, event name and
//! field names must be string literals, not variables.
//!
//! In rare cases, you might not know what events you want to log until runtime. For
//! example, you might be implementing a middle-layer library providing event support to a
//! dynamic top-layer or a scripting language like JavaScript or Python. In these cases,
//! you might use the `eventheader_dynamic` crate instead of this crate.
//! 
//! ## Configuration
//! 
//! - Linux kernel 6.4 or later, with `user_events` support enabled
//!   (`CONFIG_USER_EVENTS=y`).
//! - Must have either `tracefs` or `debugfs` mounted. For example, you might add
//!   the following line to your `/etc/fstab` file:
//!   `tracefs /sys/kernel/tracing tracefs defaults 0 0`
//! - The user that will generate events must have `x` access to the `tracing`
//!   directory, e.g. `chmod a+x /sys/kernel/tracing`
//! - The user that will generate events must have `rw` access to the
//!   `tracing/user_events_data` file, e.g.
//!   `chmod a+rw /sys/kernel/tracing/user_events_data`
//! - Collect traces using a tool like
//!   [`perf`](https://perf.wiki.kernel.org/index.php).
//! - Decode traces using a tool like
//!   [`decode-perf`](https://github.com/microsoft/LinuxTracepoints).
//! 
//! # Overview
//!
//! - Use [`define_provider!`] to create a static symbol for your [`Provider`], e.g.
//!
//!   `define_provider!(MY_PROVIDER, "MyCompany_MyComponent");`
//!
//! - During component initialization, register the provider, e.g.
//!
//!   `unsafe { MY_PROVIDER.register(); }`
//!
//!   - Safety: [`Provider::register`] is unsafe because all providers registered by an
//!     unloadable shared object **must** be properly unregistered before the shared
//!     object unloads. Since the provider is static, Rust will not automatically drop
//!     it. If you are writing an application (not a shared object), this is not a
//!     safety issue because the process exits before the application binary unloads.
//!
//! - As needed, use [`write_event!`] to send events to user_events.
//!
//! - During component shutdown, unregister the provider, e.g.
//!
//!   `MY_PROVIDER.unregister();`
//!
//! # Example
//!
//! ```
//! use eventheader as eh;
//!
//! // Define a static variable for the "MyCompany_MyComponent" provider.
//! eh::define_provider!(
//!     MY_PROVIDER,              // The static symbol to use for this provider.
//!     "MyCompany_MyComponent"); // The provider's name (string literal).
//!
//! // Register the provider at component initialization. If you don't register (or if
//! // register fails) then MY_PROVIDER.enabled() will always return false, the
//! // write_event! macro will be a no-op, and MY_PROVIDER.unregister() will be a no-op.
//! // Safety: If this is a shared object, you MUST call MY_PROVIDER.unregister() before unload.
//! unsafe { MY_PROVIDER.register(); }
//!
//! // As necessary, call write_event! to send events to user_events.
//! let field1_value = "String Value";
//! let field2_value = 42u32;
//! eh::write_event!(
//!     MY_PROVIDER,                    // The provider to use for the event.
//!     "MyEventName",                  // The event's name (string literal).
//!     level(Warning),                 // Event's severity level.
//!     keyword(0x23),                  // Event category bits.
//!     str8("Field1", field1_value),   // Add a string field to the event.
//!     u32("Field2", &field2_value),   // Add an integer field to the event.
//! );
//!
//! // Before module unload, unregister the provider.
//! MY_PROVIDER.unregister();
//! ```
//!
//! # Notes
//!
//! Field value expressions will only be evaluated if the event is enabled, i.e. only if
//! one or more perf logging sessions have enabled the tracepoint that corresponds to the
//! provider + level + keyword combination of the event.
//!
//! Perf events are limited in size (event size = headers + metadata + data). The system
//! will ignore any event that is larger than 64KB.
//!
//! Collect the events using a tool like [`perf`](https://perf.wiki.kernel.org/index.php).
//! Decode the events using a tool like
//! [`decode-perf`](https://github.com/microsoft/LinuxTracepoints).
//! 
//! Note that you cannot enable a tracepoint until the tracepoint has been registered.
//! Most programs will register all of their tracepoints when they start running, so
//! you can run the program once and then create the session to collect the events.
//! As an alternative, you can pre-register eventheader-based tracepoints using a tool
//! like [`eventheader-register`](https://github.com/microsoft/LinuxTracepoints).
//! 
//! For example, to collect and decode events with level=5 and keyword=1 from a provider
//! that was defined as `define_provider!(MY_PROVIDER, "MyCompany_MyComponent")`:
//!
//! ```text
//! perf record -e user_events:MyCompany_MyComponent_L5K1
//! <run your program>
//! <Ctrl-C to stop the perf tool>
//! decode-perf perf.data > perf.json
//! ```
//!
//! # EventHeader Technical Details
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

/// Creates a static symbol representing a tracepoint provider.
///
/// `define_provider!(PROVIDER_SYMBOL, "ProviderName", options...);`
///
/// [Options:](#options)
///
/// - `group_name("providergroupname")`
///
/// # Overview
///
/// The `define_provider!` macro creates a symbol representing a group of tracepoints
/// in your component. The symbol is a static [`Provider`] variable that can be
/// used with [`write_event!`] to send eventheader-encoded events to `user_events`.
///
/// The `PROVIDER_SYMBOL` generated by `define_provider!` **must** be treated as a token,
/// not a variable. When invoking [`write_event!`], use the original symbol, not a
/// reference or alias.
///
/// You can think of `define_provider!(MY_PROVIDER, "MyProviderName");` as expanding
/// to code approximately like:
///
/// ```ignore
/// static MY_PROVIDER: eventheader::Provider =
///     eventheader::Provider::new("MyProviderName");
/// ```
///
/// **Note:** The provider starts out unregistered. You must call
/// `MY_PROVIDER.register();` to open the provider before using it. With the exception
/// of [`Provider::register`], all operations on an unregistered provider are no-ops
/// (they will do nothing and return immediately).
///
/// # Example
///
/// ```
/// use eventheader as eh;
///
/// eh::define_provider!(MY_PROVIDER, "MyCompany_MyComponent");
///
/// // Safety: If this is a shared object, you MUST call MY_PROVIDER.unregister() before unload.
/// unsafe { MY_PROVIDER.register(); }
///
/// let message = "We're low on ice cream.";
/// eh::write_event!(
///     MY_PROVIDER,
///     "MyWarningEvent",
///     level(Warning),
///     str8("MyFieldName", message),
/// );
///
/// MY_PROVIDER.unregister();
/// ```
///
/// # Syntax
///
/// `define_provider!(PROVIDER_SYMBOL, "ProviderName", options...);`
///
/// ## Required parameters
///
/// - `PROVIDER_SYMBOL`
///
///   The token that will be used to refer to the provider. This is used with
///   [`write_event!`] and with [Provider] methods like [`Provider::register`].
/// 
///   Note that the `PROVIDER_SYMBOL` must be treated as a token, not as a variable.
///   Using an alias or reference to this symbol with `write_event!` will not work.
///
/// - `"ProviderName"`
///
///   A **string literal** that specifies a short human-readable name for
///   the provider. This string will be included in the events and will be a primary
///   attribute for event identification. It needs to be unique so that it does not
///   conflict with names used by other providers. It should follow a namespace
///   convention like "CompanyName_ComponentName".
/// 
///   This string must not contain space, colon, or NUL characters. For best
///   compatibility with the Linux tracing system, it should start with an ASCII letter
///   or an underscore and should contain only ASCII letters, digits, and underscores.
///
/// ## Options
///
/// - `group_name("name")`
///
///   Specifies the name of a provider group that this provider will belong to.
///   Most providers do not join a provider group so most providers do not need to
///   specify the `group_id` option. The "name" string may contain lowercase ASCII
///   letters and digits.
///
///   Example: `group_name("mycompany")`
///
/// - `debug()`
///
///   For non-production diagnostics: prints the expanded macro during compilation.
/// 
/// - For compability with the `tracelogging` crate, certain other options may be
///   accepted and ignored.
#[cfg(feature = "macros")]
pub use eventheader_macros::define_provider;

/// Sends an event to `user_events` via the specified provider.
///
/// `write_event!(PROVIDER_SYMBOL, "EventName", options and fields...);`
///
/// [Options:](#options)
///
/// - `level(Verbose)`
/// - `keyword(0x123)`
/// - `opcode(Info)`
/// - `activity_id(&guid)`
/// - `related_id(&guid)`
/// - `tag(0x123)`
/// - `id_version(23, 0)`
/// - `debug()`
///
/// [Fields:](#fields-1)
///
/// - `u32("FieldName", &int_val)`
/// - `u32_slice("FieldName", &int_vals[..])`
/// - `str8("FieldName", str_val)`
/// - `str8_json("FieldName", json_str_val)`
/// - `struct("FieldName", { str8("NestedField", str_val), ... })`
/// - [and many more...](#normal-field-types)
///
/// # Overview
///
/// The `write_event!` macro creates an eventheader-encoded event and sends it to
/// `user_events` using a [Provider] that was created by [`define_provider!`].
///
/// You can think of `write_event!(MY_PROVIDER, "EventName", options and fields...)`
/// as expanding to code that is something like the following:
///
/// ```ignore
/// if !MY_PROVIDER.enabled(event_level, event_keyword) {
///     0
/// } else {
///     writev(MY_PROVIDER, options and fields...)
/// }
/// ```
///
/// The `PROVIDER_SYMBOL` generated by [`define_provider!`] should be treated as a token,
/// not a variable. When invoking `write_event!`, use the original symbol, not a
/// reference or alias.
///
/// **Note:** The field value expressions are evaluated and the event is sent to `user_events`
/// only if the event is enabled, i.e. only if one or more perf logging sessions are listening
/// to the provider with filters that include the level and keyword of the event.
///
/// The `write_event!` macro returns a `u32` value with an errno result code. If no
/// logging sessions are listening for the event, `write_event!` immediately returns 0.
/// Otherwise, it returns the value returned by the underlying `writev` API. Since most
/// components treat logging APIs as fire-and-forget, this value should normally be ignored
/// in production code. It is generally used only for debugging and troubleshooting.
///
/// # Limitations
///
/// The Linux perf system is optimized for efficient handling of small events. Events have
/// the following limits:
///
/// - If the total event size (including headers, provider name string, event name
///   string, field name strings, and event data) exceeds 64KB, the event will not be
///   delivered to any sessions.
/// - If a struct contains more than 127 fields, the eventheader encoding will be unable
///   to represent the event. A field is anything with a "FieldName". The `write_event!`
///   macro will generate a compile error if a struct has more than 127 fields. You might
///   be able to work around this limitation by using arrays or by logging a series of
///   simpler events instead of a single complex event.
///
/// # Example
///
/// ```
/// use eventheader as eh;
///
/// eh::define_provider!(MY_PROVIDER, "MyCompany_MyComponent");
///
/// // Safety: If this is a shared object, you MUST call MY_PROVIDER.unregister() before unload.
/// unsafe { MY_PROVIDER.register(); }
///
/// let message = "We're low on ice cream.";
/// eh::write_event!(
///     MY_PROVIDER,
///     "MyWarningEvent",
///     level(Warning),
///     str8("MyFieldName", message),
/// );
///
/// MY_PROVIDER.unregister();
/// ```
///
/// # Syntax
///
/// `write_event!(PROVIDER_SYMBOL, "EventName", options and fields...);`
///
/// ## Required parameters
///
/// - `PROVIDER_SYMBOL`
///
///   The symbol for the provider that will be used for sending the event to `user_events`.
///   This is a symbol that was created by [`define_provider!`].
///
///   This should be the original symbol name created by [`define_provider!`], not a
///   reference or alias.
///
/// - `"EventName"`
///
///   A **string literal** that specifies a short human-readable name for the event. The
///   name will be included in the event and will be a primary attribute for event
///   identification. It should be unique so that the resulting events will not be
///   confused with other events in the same provider.
///
/// ## Options
///
/// - `level(event_level)`
///
///   Specifies the level (severity) of the event.
///
///   Level is important for event filtering so all events should specify a meaningful
///   non-zero level.
///
///   If the `level` option is not specified then the event's level will be
///   [Level::Verbose]. If the level is specified it must be a constant [Level] value.
///
/// - `keyword(event_keyword)`
///
///   Specifies the keyword (category bits) of the event.
///
///   The keyword is a 64-bit value where each bit in the keyword corresponds to a
///   provider-defined category. For example, the "MyCompany_MyComponent" provider might
///   define keyword bit `0x2` to indicate that the event is part of a "networking" event
///   category. In that case, any event in that provider with the `0x2` bit set in the
///   keyword is considered as belonging to the "networking" category.
///
///   Keyword is important for event filtering so all events should specify a meaningful
///   non-zero keyword.
///
///   If no `keyword` options are specified then the event's keyword will be `0x1` to
///   flag the event as not having any assigned keyword. If the `keyword` option is
///   specified it must be a constant `u64` value. The `keyword` option may be specified
///   more than once, in which case all provided keyword values will be OR'ed together in
///   the event's keyword.
///
/// - `opcode(event_opcode)`
///
///   Specifies the opcode attribute for the event.
///
///   The opcode indicates special event semantics such as "activity start" or "activity
///   stop" that can be used by the event decoder to group events together.
///
///   If the `opcode` option is not specified the event's opcode will be [Opcode::Info],
///   indicating no special semantics. If the opcode is specified it must be a constant
///   [Opcode] value.
///
/// - `activity_id(&guid)`
///
///   Specifies the activity id to use for the event.
///
///   If not specified, the event will not have any activity id. If specified, the value
///   must be a reference to a [Guid] or a reference to a `[u8; 16]`.
///
/// - `related_id(&guid)`
///
///   Specifies the related activity id to use for the event.
///
///   This value is normally set for the activity-[start](Opcode::ActivityStart) event to record the
///   parent activity of the newly-started activity. This is normally left unset for other
///   events.
///
///   If not specified, the event will not have any related activity id.
///   If specified, the value must be a reference to a [Guid] or a reference to a
///   `[u8; 16]`.
///
/// - `tag(event_tag)`
///
///   Specifies the tag to use for the event.
///
///   A tag is a 16-bit provider-defined value that is available when the event is
///   decoded. The tag's semantics are provider-defined, e.g. the "MyCompany_MyComponent"
///   provider might define tag value `0x1` to mean the event contains high-priority
///   information. Most providers do not use tags so most events do not need to specify
///   the `tag` option.
///
///   If the `tag` option is not specified the event's tag will be 0. If specified, the
///   tag must be a constant `u16` value.
///
/// - `id_version(event_id, event_version)`
///
///   Specifies a manually-assigned numeric id for this event, along with a version
///   that indicates changes in the event schema or semantics.
///
///   Most providers use the event name for event identification so most events do not
///   need to specify the `id_version` option.
///
///   The version should start at 0 and should be incremented each time a breaking change
///   is made to the event, e.g. when a field is removed or a field changes type.
///
///   If the `id_version` option is not specified then the event's id and version will be
///   0, indicating that no id has been assigned to the event. If id and version are
///   specified, the id must be a constant `u16` value and the version must be a constant
///   `u8` value.
///
/// - `debug()`
///
///   For non-production diagnostics: prints the expanded macro during compilation.
///
/// ## Fields
///
/// Event content is provided in fields. Each field is added to the event with a field
/// type.
///
/// There are three categories of field types:
///
/// - [Normal field types](#normal-fields) add a field to the event with a value such as
///   an integer, float, string, slice of i32, [etc.](#normal-field-types)
/// - [The struct field type](#struct-fields) adds a field to the event that contains a group
///   of other fields.
/// - [Raw field types](#raw-fields) directly add unchecked data (field content) and/or
///   metadata (field name and type information) to the event. They are used in advanced
///   scenarios to optimize event generation or to log complex data types that the other
///   field categories cannot handle.
///
/// ### Normal fields
///
/// All normal fields have a type, a name, and a value reference. They may optionally
/// specify a tag and/or a format.
///
/// **Normal field syntax:** `TYPE("NAME", VALUE_REF, tag(TAG), format(FORMAT))`
///
/// - `TYPE` controls the expected type of the `VALUE_REF` expression,
///   the eventheader encoding that the field will use in the event, and default format that the
///   field will have when it is decoded. TYPEs include `u32`, `str8`, `str16`,
///   `f32_slice` and [many others](#normal-field-types).
///
/// - `"NAME"` is a string literal that specifies the name of the field.
///
/// - `VALUE_REF` is a Rust expression that provides a reference to the value of the
///   field.
///
///   Field types that expect a slice `&[T]` type will also accept types that implement
///   the [`AsRef<[T]>`](AsRef) trait. For example, the `str8` field types expect a
///   `&[u8]` but will also accept `&str` or `&String` because those types implement
///   `AsRef<[u8]>`.
///
///   The field value expression will be evaluated only if the event is enabled, i.e.
///   only if at least one logging session is listening to the provider and has filtering
///   that includes this event's level and keyword.
///
/// - `tag(TAG)` specifies a 16-bit "field tag" with provider-defined semantics.
///
///   This is usually omitted because most providers do not use field tags.
///
///   If not present, the field tag is `0`. If present, the TAG must be a 16-bit constant
///   `u16` value.
///
/// - `format(FORMAT)` specifies an [FieldFormat] that overrides the format that would
///   normally apply for the given `TYPE`.
///
///   This is usually omitted because most valid formats are available without the use of
///   the format option. For example, you could specify a `str8` type with a `StringJson`
///   format, but this is unnecessary because there is already a `str8_json` type that has
///   the same effect.
///
///   If not present, the field's format depends on the field's `TYPE`. If present, the
///   FORMAT must be a constant [FieldFormat] value.
///
/// Example:
///
/// ```
/// # use eventheader as eh;
/// # eh::define_provider!(MY_PROVIDER, "MyCompany_MyComponent");
/// let message = "We're low on ice cream.";
/// eh::write_event!(
///     MY_PROVIDER,
///     "MyWarningEvent",
///     level(Warning),
///     str8("MyField1", message),                     // No options (normal)
///     str8("MyField2", message, format(StringJson)), // Using the format option
///     str8("MyField3", message, tag(0x1234)),        // Using the tag option
///     str8("MyField4", message, format(StringJson), tag(0x1234)), // Both options
/// );
/// ```
///
/// ### Normal field types
///
/// | Field Type | Rust Type | EventHeader Type
/// |------------|-----------|-----------------
/// | `binary` | `&[u8]` | [`StringLength16Char8`](FieldEncoding::StringLength16Char8) + [`HexBytes`](FieldFormat::HexBytes)
/// | `binaryc` | `&[u8]` | [`StringLength16Char8`](FieldEncoding::StringLength16Char8) + [`HexBytes`](FieldFormat::HexBytes)
/// | `bool8` | `&bool` | [`Value8`](FieldEncoding::Value8) + [`Boolean`](FieldFormat::Boolean)
/// | `bool8_slice` | `&[bool]` | [`Value8`](FieldEncoding::Value8) + [`Boolean`](FieldFormat::Boolean)
/// | `bool32` | `&i32` | [`Value32`](FieldEncoding::Value32) + [`Boolean`](FieldFormat::Boolean)
/// | `bool32_slice` | `&[i32]` | [`Value32`](FieldEncoding::Value32) + [`Boolean`](FieldFormat::Boolean)
/// | `char8_cp1252` | `&u8` | [`Value8`](FieldEncoding::Value8) + [`String8`](FieldFormat::String8)
/// | `char8_cp1252_slice` | `&[u8]` | [`Value8`](FieldEncoding::Value8) + [`String8`](FieldFormat::String8)
/// | `char16` | `&u16` | [`Value16`](FieldEncoding::Value16) + [`StringUtf`](FieldFormat::StringUtf)
/// | `char16_slice` | `&[u16]` | [`Value16`](FieldEncoding::Value16) + [`StringUtf`](FieldFormat::StringUtf)
/// | `codepointer` | `&usize` | [`ValueSize`](FieldEncoding::ValueSize) + [`HexInt`](FieldFormat::HexInt)
/// | `codepointer_slice` | `&[usize]` | [`ValueSize`](FieldEncoding::ValueSize) + [`HexInt`](FieldFormat::HexInt)
/// | `cstr8` [^cstr] | `&[u8]` | [`ZStringChar8`](FieldEncoding::ZStringChar8)
/// | `cstr8_cp1252` [^cstr] | `&[u8]` | [`ZStringChar8`](FieldEncoding::ZStringChar8) + [`String8`](FieldFormat::String8)
/// | `cstr8_json` [^cstr] | `&[u8]` | [`ZStringChar8`](FieldEncoding::ZStringChar8) + [`StringJson`](FieldFormat::StringJson)
/// | `cstr8_xml` [^cstr] | `&[u8]` | [`ZStringChar8`](FieldEncoding::ZStringChar8) + [`StringXml`](FieldFormat::StringXml)
/// | `cstr16` [^cstr] | `&[u16]` | [`ZStringChar16`](FieldEncoding::ZStringChar16)
/// | `cstr16_json` [^cstr] | `&[u16]` | [`ZStringChar16`](FieldEncoding::ZStringChar16) + [`StringJson`](FieldFormat::StringJson)
/// | `cstr16_xml` [^cstr] | `&[u16]` | [`ZStringChar16`](FieldEncoding::ZStringChar16) + [`StringXml`](FieldFormat::StringXml)
/// | `cstr32` [^cstr] | `&[u32]` | [`ZStringChar32`](FieldEncoding::ZStringChar32)
/// | `cstr32_json` [^cstr] | `&[u32]` | [`ZStringChar32`](FieldEncoding::ZStringChar32) + [`StringJson`](FieldFormat::StringJson)
/// | `cstr32_xml` [^cstr] | `&[u32]` | [`ZStringChar32`](FieldEncoding::ZStringChar32) + [`StringXml`](FieldFormat::StringXml)
/// | `errno` [^errno] | `&i32` | [`Value32`](FieldEncoding::Value32) + [`Errno`](FieldFormat::Errno)
/// | `errno_slice` [^errno] | `&[i32]` | [`Value32`](FieldEncoding::Value32) + [`Errno`](FieldFormat::Errno)
/// | `f32` | `&f32` | [`Value32`](FieldEncoding::Value32) + [`Float`](FieldFormat::Float)
/// | `f32_slice` | `&[f32]` | [`Value32`](FieldEncoding::Value32) + [`Float`](FieldFormat::Float)
/// | `f64` | `&f64` | [`Value64`](FieldEncoding::Value64) + [`Float`](FieldFormat::Float)
/// | `f64_slice` | `&[f64]` | [`Value64`](FieldEncoding::Value64) + [`Float`](FieldFormat::Float)
/// | `guid` | `&eventheader::Guid` | [`Value128`](FieldEncoding::Value128) + [`Uuid`](FieldFormat::Uuid)
/// | `guid_slice` | `&[eventheader::Guid]` | [`Value128`](FieldEncoding::Value128) + [`Uuid`](FieldFormat::Uuid)
/// | `hresult` | `&i32` | [`Value32`](FieldEncoding::Value32) + [`HexInt`](FieldFormat::HexInt)
/// | `hresult_slice` | `&[i32]` | [`Value32`](FieldEncoding::Value32) + [`HexInt`](FieldFormat::HexInt)
/// | `i8` | `&i8` | [`Value8`](FieldEncoding::Value8) + [`SignedInt`](FieldFormat::SignedInt)
/// | `i8_slice` | `&[i8]` | [`Value8`](FieldEncoding::Value8) + [`SignedInt`](FieldFormat::SignedInt)
/// | `i8_hex` | `&i8` | [`Value8`](FieldEncoding::Value8) + [`HexInt`](FieldFormat::HexInt)
/// | `i8_hex_slice` | `&[i8]` | [`Value8`](FieldEncoding::Value8) + [`HexInt`](FieldFormat::HexInt)
/// | `i16` | `&i16` | [`Value16`](FieldEncoding::Value16) + [`SignedInt`](FieldFormat::SignedInt)
/// | `i16_slice` | `&[i16]` | [`Value16`](FieldEncoding::Value16) + [`SignedInt`](FieldFormat::SignedInt)
/// | `i16_hex` | `&i16` | [`Value16`](FieldEncoding::Value16) + [`HexInt`](FieldFormat::HexInt)
/// | `i16_hex_slice` | `&[i16]` | [`Value16`](FieldEncoding::Value16) + [`HexInt`](FieldFormat::HexInt)
/// | `i32` | `&i32` | [`Value32`](FieldEncoding::Value32) + [`SignedInt`](FieldFormat::SignedInt)
/// | `i32_slice` | `&[i32]` | [`Value32`](FieldEncoding::Value32) + [`SignedInt`](FieldFormat::SignedInt)
/// | `i32_hex` | `&i32` | [`Value32`](FieldEncoding::Value32) + [`HexInt`](FieldFormat::HexInt)
/// | `i32_hex_slice` | `&[i32]` | [`Value32`](FieldEncoding::Value32) + [`HexInt`](FieldFormat::HexInt)
/// | `i64` | `&i64` | [`Value64`](FieldEncoding::Value64) + [`SignedInt`](FieldFormat::SignedInt)
/// | `i64_slice` | `&[i64]` | [`Value64`](FieldEncoding::Value64) + [`SignedInt`](FieldFormat::SignedInt)
/// | `i64_hex` | `&i64` | [`Value64`](FieldEncoding::Value64) + [`HexInt`](FieldFormat::HexInt)
/// | `i64_hex_slice` | `&[i64]` | [`Value64`](FieldEncoding::Value64) + [`HexInt`](FieldFormat::HexInt)
/// | `ipv4` | `&[u8; 4]` | [`Value32`](FieldEncoding::Value32) + [`IPv4`](FieldFormat::IPv4)
/// | `ipv4_slice` | `&[[u8; 4]]` | [`Value32`](FieldEncoding::Value32) + [`IPv4`](FieldFormat::IPv4)
/// | `ipv6` | `&[u8; 16]` | [`Value128`](FieldEncoding::Value128) + [`IPv6`](FieldFormat::IPv6)
/// | `ipv6c` | `&[u8; 16]` | [`Value128`](FieldEncoding::Value128) + [`IPv6`](FieldFormat::IPv6)
/// | `isize` | `&isize` | [`ValueSize`](FieldEncoding::ValueSize) + [`SignedInt`](FieldFormat::SignedInt)
/// | `isize_slice` | `&[isize]` | [`ValueSize`](FieldEncoding::ValueSize) + [`SignedInt`](FieldFormat::SignedInt)
/// | `isize_hex` | `&isize` | [`ValueSize`](FieldEncoding::ValueSize) + [`HexInt`](FieldFormat::HexInt)
/// | `isize_hex_slice` | `&[isize]` | [`ValueSize`](FieldEncoding::ValueSize) + [`HexInt`](FieldFormat::HexInt)
/// | `pid` | `&u32` | [`Value32`](FieldEncoding::Value32) + [`Pid`](FieldFormat::Pid)
/// | `pid_slice` | `&[u32]` | [`Value32`](FieldEncoding::Value32) + [`Pid`](FieldFormat::Pid)
/// | `pointer` | `&usize` | [`ValueSize`](FieldEncoding::ValueSize) + [`HexInt`](FieldFormat::HexInt)
/// | `pointer_slice` | `&[usize]` | [`ValueSize`](FieldEncoding::ValueSize) + [`HexInt`](FieldFormat::HexInt)
/// | `port` | `&u16` | [`Value16`](FieldEncoding::Value16) + [`Port`](FieldFormat::Port)
/// | `port_slice` | `&[u16]` | [`Value16`](FieldEncoding::Value16) + [`Port`](FieldFormat::Port)
/// | `socketaddress` | `&[u8]` | [`StringLength16Char8`](FieldEncoding::StringLength16Char8) + [`HexBytes`](FieldFormat::HexBytes)
/// | `socketaddressc` | `&[u8]` | [`StringLength16Char8`](FieldEncoding::StringLength16Char8) + [`HexBytes`](FieldFormat::HexBytes)
/// | `str8` | `&[u8]` | [`StringLength16Char8`](FieldEncoding::StringLength16Char8)
/// | `str8_cp1252` | `&[u8]` | [`StringLength16Char8`](FieldEncoding::StringLength16Char8) + [`String8`](FieldFormat::String8)
/// | `str8_json` | `&[u8]` | [`StringLength16Char8`](FieldEncoding::StringLength16Char8) + [`StringJson`](FieldFormat::StringJson)
/// | `str8_xml` | `&[u8]` | [`StringLength16Char8`](FieldEncoding::StringLength16Char8) + [`StringXml`](FieldFormat::StringXml)
/// | `str16` | `&[u16]` | [`StringLength16Char16`](FieldEncoding::StringLength16Char16)
/// | `str16_json` | `&[u16]` | [`StringLength16Char16`](FieldEncoding::StringLength16Char16) + [`StringJson`](FieldFormat::StringJson)
/// | `str16_xml` | `&[u16]` | [`StringLength16Char16`](FieldEncoding::StringLength16Char16) + [`StringXml`](FieldFormat::StringXml)
/// | `str32` | `&[u32]` | [`StringLength16Char32`](FieldEncoding::StringLength16Char32)
/// | `str32_json` | `&[u32]` | [`StringLength16Char32`](FieldEncoding::StringLength16Char32) + [`StringJson`](FieldFormat::StringJson)
/// | `str32_xml` | `&[u32]` | [`StringLength16Char32`](FieldEncoding::StringLength16Char32) + [`StringXml`](FieldFormat::StringXml)
/// | `systemtime` [^systemtime] | `&std::time::SystemTime` | [`Value64`](FieldEncoding::Value64) + [`Time`](FieldFormat::Time)
/// | `tid` | `&u32` | [`Value32`](FieldEncoding::Value32) + [`Pid`](FieldFormat::Pid)
/// | `tid_slice` | `&[u32]` | [`Value32`](FieldEncoding::Value32) + [`Pid`](FieldFormat::Pid)
/// | `time32` | `&i32` | [`Value32`](FieldEncoding::Value32) + [`Time`](FieldFormat::Time)
/// | `time64` | `&i64` | [`Value64`](FieldEncoding::Value64) + [`Time`](FieldFormat::Time)
/// | `u8` | `&u8` | [`Value8`](FieldEncoding::Value8)
/// | `u8_slice` | `&[u8]` | [`Value8`](FieldEncoding::Value8)
/// | `u8_hex` | `&u8` | [`Value8`](FieldEncoding::Value8) + [`HexInt`](FieldFormat::HexInt)
/// | `u8_hex_slice` | `&[u8]` | [`Value8`](FieldEncoding::Value8) + [`HexInt`](FieldFormat::HexInt)
/// | `u16` | `&u16` | [`Value16`](FieldEncoding::Value16)
/// | `u16_slice` | `&[u16]` | [`Value16`](FieldEncoding::Value16)
/// | `u16_hex` | `&u16` | [`Value16`](FieldEncoding::Value16) + [`HexInt`](FieldFormat::HexInt)
/// | `u16_hex_slice` | `&[u16]` | [`Value16`](FieldEncoding::Value16) + [`HexInt`](FieldFormat::HexInt)
/// | `u32` | `&u32` | [`Value32`](FieldEncoding::Value32)
/// | `u32_slice` | `&[u32]` | [`Value32`](FieldEncoding::Value32)
/// | `u32_hex` | `&u32` | [`Value32`](FieldEncoding::Value32) + [`HexInt`](FieldFormat::HexInt)
/// | `u32_hex_slice` | `&[u32]` | [`Value32`](FieldEncoding::Value32) + [`HexInt`](FieldFormat::HexInt)
/// | `u64` | `&u64` | [`Value64`](FieldEncoding::Value64)
/// | `u64_slice` | `&[u64]` | [`Value64`](FieldEncoding::Value64)
/// | `u64_hex` | `&u64` | [`Value64`](FieldEncoding::Value64) + [`HexInt`](FieldFormat::HexInt)
/// | `u64_hex_slice` | `&[u64]` | [`Value64`](FieldEncoding::Value64) + [`HexInt`](FieldFormat::HexInt)
/// | `usize` | `&usize` | [`ValueSize`](FieldEncoding::ValueSize)
/// | `usize_slice` | `&[usize]` | [`ValueSize`](FieldEncoding::ValueSize)
/// | `usize_hex` | `&usize` | [`ValueSize`](FieldEncoding::ValueSize) + [`HexInt`](FieldFormat::HexInt)
/// | `usize_hex_slice` | `&[usize]` | [`ValueSize`](FieldEncoding::ValueSize) + [`HexInt`](FieldFormat::HexInt)
///
/// [^cstr]: The `cstrN` types use a `0`-terminated string encoding in
/// the event. If the provided field value contains any `'\0'` characters then the event
/// will include the value up to the first `'\0'`; otherwise the event will include the
/// entire value. There is a small runtime overhead for locating the first `0` in the
/// string. To avoid the overhead and to ensure you log your entire string (including any
/// `'\0'` characters), prefer the `str` types (counted strings) over the `cstr` types
/// (`0`-terminated strings) unless you specifically need a `0`-terminated event encoding.
///
/// [^errno]: The `errno` type is intended for use with C-style `errno` error codes.
///
/// [^systemtime]: When logging `systemtime` types, `write_event!` will convert the
/// provided `std::time::SystemTime` value into a `time64_t` value containing the number
/// of seconds since 1970, rounding down to the nearest second.
///
/// ### Struct fields
///
/// A struct is a group of fields that are logically considered a single field.
///
/// Struct fields have type `struct`, a name, and a set of nested fields enclosed in
/// braces `{ ... }`. They may optionally specify a tag.
///
/// **Struct field syntax:** `struct("NAME", tag(TAG), { FIELDS... })`
///
/// - `"NAME"` is a string literal that specifies the name of the field.
///
/// - `tag(TAG)` specifies a 16-bit "field tag" with provider-defined semantics.
///
///   This is usually omitted because most providers do not use field tags.
///
///   If not present, the field tag is `0`. If present, the TAG must be a 16-bit constant
///   `u16` value.
///
/// - `{ FIELDS... }` is a list of other fields that will be considered to be part of
///   this field. This list may include normal fields, struct fields, and non-struct raw
///   fields.
///
/// Example:
///
/// ```
/// # use eventheader as eh;
/// # eh::define_provider!(MY_PROVIDER, "MyCompany_MyComponent");
/// let message = "We're low on ice cream.";
/// eh::write_event!(
///     MY_PROVIDER,
///     "MyWarningEvent",
///     level(Warning),
///     str8("RootField1", message),
///     str8("RootField2", message),
///     struct("RootField3", {
///         str8("MemberField1", message),
///         str8("MemberField2", message),
///         struct("MemberField3", tag(0x1234), {
///             str8("NestedField1", message),
///             str8("NestedField2", message),
///         }),
///         str8("MemberField4", message),
///     }),
///     str8("RootField4", message),
/// );
/// ```
///
/// ### Raw fields
///
/// *Advanced:* In certain cases, you may need capabilities not directly exposed by the
/// normal field types. For example,
///
/// - You might need to log an array of a variable-sized type, such as an array of
///   string.
/// - You might need to log an array of struct.
/// - You might want to log several fields in one block of data to reduce overhead.
///
/// In these cases, you can use the raw field types. These types are harder to use than
/// the normal field types. Using these types incorrectly can result in events that
/// cannot be decoded. To use these types correctly, you must understand how
/// eventheader events are encoded. `write_event!` does not verify that the provided
/// field types or field data are valid.
///
/// **Note:** eventheader stores event data tightly-packed with no padding, alignment, or
/// size. If your field data size does not match up with your field type, the remaining
/// fields of the event will decode incorrectly, not just the mismatched field.
///
/// Each raw field type has unique syntax and capabilities. However, in all cases,
/// the `format` and `tag` options have the same significance as in normal fields and may
/// be omitted if not needed. If omitted, `tag` defaults to `0` and `format` defaults to
/// [FieldFormat::Default].
///
/// Raw field data is always specified as `&[u8]`. The provided VALUE_BYTES must include
/// the entire field, including prefix (e.g. `u16` byte count prefix required on
/// "Length16" fields like [FieldEncoding::StringLength16Char8] and
/// [FieldEncoding::StringLength16Char16]) or suffix (e.g. `'\0'` termination required on
/// [FieldEncoding::ZStringChar8] fields).
///
/// - `raw_field("NAME", ENCODING, VALUE_BYTES, format(FORMAT), tag(TAG))`
///
///   The `raw_field` type allows you to add a field with direct control over the field's
///   contents. VALUE_BYTES is specified as `&[u8]` and you can specify any [FieldEncoding].
///
/// - `raw_field_slice("NAME", ENCODING, VALUE_BYTES, format(FORMAT), tag(TAG))`
///
///   The `raw_field` type allows you to add a variable-sized array field with direct
///   control over the field's contents. VALUE_BYTES is specified as `&[u8]` and you can
///   specify any [FieldEncoding]. Note that the provided VALUE_BYTES must include the
///   entire array, including the array element count, which is a `u16` element count
///   immediately before the field values.
///
/// - `raw_meta("NAME", ENCODING, format(FORMAT), tag(TAG))`
///
///   The `raw_meta` type allows you to add a field definition (name, encoding, format,
///   tag) without immediately adding the field's data. This allows you to specify
///   multiple `raw_meta` fields and then provide the data for all of the fields via one
///   or more `raw_data` fields before or after the corresponding `raw_meta` fields.
///
/// - `raw_meta_slice("NAME", ENCODING, format(FORMAT), tag(TAG))`
///
///   The `raw_meta_slice` type allows you to add a variable-length array field
///   definition (name, type, format, tag) without immediately adding the array's data.
///   This allows you to specify multiple `raw_meta` fields and then provide the data for
///   all of the fields via one or more `raw_data` fields (before or after the
///   corresponding `raw_meta` fields).
///
/// - `raw_struct("NAME", FIELD_COUNT, tag(TAG))`
///
///   The `raw_struct` type allows you to begin a struct and directly specify the number
///   of fields in the struct. The struct's member fields are specified separately (e.g.
///   via `raw_meta` or `raw_meta_slice`).
///
///   Note that the FIELD_COUNT must be a constant `u8` value in the range 0 to 127. It
///   indicates the number of subsequent logical fields that will be considered to be
///   part of the struct. In cases of nested structs, a struct and its fields count as a
///   single logical field.
///
/// - `raw_struct_slice("NAME", FIELD_COUNT, tag(TAG))`
///
///   The `raw_struct_slice` type allows you to begin a variable-length array-of-struct
///   and directly specify the number of fields in the struct. The struct's member fields
///   are specified separately (e.g. via `raw_meta` or `raw_meta_slice`).
///
///   The number of elements in the array is specified as a `u16` value immediately
///   before the array content.
///
///   Note that the FIELD_COUNT must be a constant `u8` value in the range 0 to 127. It
///   indicates the number of subsequent logical fields that will be considered to be
///   part of the struct. In cases of nested structs, a struct and its fields count as a
///   single logical field.
///
/// - `raw_data(VALUE_BYTES)`
///
///   The `raw_data` type allows you to add data to the event without specifying any
///   field. This should be used together with `raw_meta` or `raw_meta_slice` fields,
///   where the `raw_meta` or `raw_meta_slice` fields declare the field names and types
///   and the `raw_data` field(s) provide the corresponding data (including any array
///   element counts).
///
///   Note that eventheader events contain separate sections for metadata and data.
///   The `raw_meta` fields add to the compile-time-constant metadata section and the
///   `raw_data` fields add to the runtime-variable data section. As a result, it doesn't
///   matter whether the `raw_data` comes before or after the corresponding `raw_meta`.
///   In addition, you can use one `raw_data` to supply the data for any number of
///   fields or you can use multiple `raw_data` fields to supply the data for one field.
///
/// Example:
///
/// ```
/// # use eventheader as eh;
/// # eh::define_provider!(MY_PROVIDER, "MyCompany_MyComponent");
/// eh::write_event!(MY_PROVIDER, "MyWarningEvent", level(Warning),
///
///     // Make a Value8 + String8 field containing 1 byte of data.
///     raw_field("RawChar8", Value8, &[65], format(String8), tag(200)),
///
///     // Make a Value8 + String8 array containing u16 array-count (3) followed by 3 bytes.
///     raw_field_slice("RawChar8s", Value8, &[
///         3, 0,       // RawChar8s.Length = 3
///         65, 66, 67, // RawChar8s content = [65, 66, 67]
///     ], format(String8)),
///
///     // Declare a Value32 + HexInt field, but don't provide the data yet.
///     raw_meta("RawHex32", Value32, format(HexInt)),
///
///     // Declare a Value8 + HexInt array, but don't provide the data yet.
///     raw_meta_slice("RawHex8s", Value8, format(HexInt)),
///
///     // Provide the data for the previously-declared fields:
///     raw_data(&[
///         255, 0, 0, 0, // RawHex32 = 255
///         3, 0,         // RawHex8s.Length = 3
///         65, 66, 67]), // RawHex8s content = [65, 66, 67]
///
///     // Declare a struct with 2 fields. The next 2 logical fields in the event will be
///     // members of this struct.
///     raw_struct("RawStruct", 2),
///
///         // Type and data for first member of RawStruct.
///         raw_field("RawChar8", Value8, &[65], format(String8)),
///
///         // Type and data for second member of RawStruct.
///         raw_field_slice("RawChar8s", Value8, &[3, 0, 65, 66, 67], format(String8)),
///
///     // Declare a struct array with 2 fields.
///     raw_struct_slice("RawStructSlice", 2),
///
///         // Declare the first member of RawStructSlice. Type only (no data yet).
///         raw_meta("RawChar8", Value8, format(String8)),
///
///         // Declare the second member of RawStructSlice. Type only (no data yet).
///         raw_meta_slice("RawChar8s", Value8, format(String8)),
///
///     // Provide the data for the array of struct.
///     raw_data(&[
///         2, 0,       // RawStructSlice.Length = 2
///         48,         // RawStructSlice[0].RawChar8
///         3, 0,       // RawStructSlice[0].RawChar8s.Length = 3
///         65, 66, 67, // RawStructSlice[0].RawChar8s content
///         49,         // RawStructSlice[1].RawChar8
///         2, 0,       // RawStructSlice[1].RawChar8s.Length = 2
///         48, 49,     // RawStructSlice[1].RawChar8s content
///     ]),
/// );
/// ```
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
            Ok(dur) => ::eventheader::_internal::time_from_duration_after_1970(dur),
            Err(err) => ::eventheader::_internal::time_from_duration_before_1970(err.duration()),
        }
    };
}

mod descriptors;
mod enums;
mod guid;
mod native;
mod provider;
