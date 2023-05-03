// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use alloc::vec::Vec;
use core::mem::size_of;
use core::ptr::copy_nonoverlapping;

use eventheader::FieldEncoding;
use eventheader::FieldFormat;
use eventheader::Opcode;
use eventheader::_internal::EventDataDescriptor;
use eventheader::_internal::EventHeader;
use eventheader::_internal::HeaderFlags;

use crate::provider::EventSet;

#[allow(unused_imports)] // For docs
use crate::Provider;

pub trait ValueField: Copy {}
impl ValueField for bool {}
impl ValueField for char {}
impl ValueField for f32 {}
impl ValueField for f64 {}
impl ValueField for i8 {}
impl ValueField for i16 {}
impl ValueField for i32 {}
impl ValueField for i64 {}
impl ValueField for isize {}
impl ValueField for u8 {}
impl ValueField for u16 {}
impl ValueField for u32 {}
impl ValueField for u64 {}
impl ValueField for usize {}
impl ValueField for [u8; 1] {}
impl ValueField for [u8; 2] {}
impl ValueField for [u8; 4] {}
impl ValueField for [u8; 8] {}
impl ValueField for [u8; 16] {}

pub trait StringField: ValueField + Default + Eq {}
impl StringField for char {}
impl StringField for i8 {}
impl StringField for i16 {}
impl StringField for i32 {}
impl StringField for u8 {}
impl StringField for u16 {}
impl StringField for u32 {}

trait ValueFieldEncoding: ValueField {
    const VALUE_ENCODING: FieldEncoding = match size_of::<Self>() {
        1 => FieldEncoding::Value8,
        2 => FieldEncoding::Value16,
        4 => FieldEncoding::Value32,
        8 => FieldEncoding::Value64,
        16 => FieldEncoding::Value128,
        _ => panic!(),
    };
}

impl<T: ValueField> ValueFieldEncoding for T {}

trait StringFieldEncoding: StringField {
    const STRING_ENCODING: FieldEncoding = match size_of::<Self>() {
        1 => FieldEncoding::StringLength16Char8,
        2 => FieldEncoding::StringLength16Char16,
        4 => FieldEncoding::StringLength16Char32,
        _ => panic!(),
    };
    const ZSTRING_ENCODING: FieldEncoding = match size_of::<Self>() {
        1 => FieldEncoding::ZStringChar8,
        2 => FieldEncoding::ZStringChar16,
        4 => FieldEncoding::ZStringChar32,
        _ => panic!(),
    };
}

impl<T: StringField> StringFieldEncoding for T {}

/// `EventBuilder` is a builder for events to be written through a [Provider].
///
/// # Overview
///
/// - Check [EventSet::enabled], e.g. `my_eventset_l5k1.enabled()` and only do the
///   remaining steps if it returns true. This avoids the unnecessary work of building
///   and writing the event when no trace collection sessions are listening for your event.
/// - Get an EventBuilder, e.g. `let mut builder = EventBuilder::new();`.
///   - EventBuilder is reusable. In some scenarios, you may get a performance improvement
///     by reusing builders instead of creating a new one for each event.
/// - Call `builder.reset("EventName", event_tag)` to begin building an event.
///   - The event name should be short and distinct. Don't use same name for two events in
///     the same provider that have different fields, levels, or keywords.
///   - event_tag is a 16-bit provider-defined value that will be included in the
///     metadata of the event. Use 0 if you are not using event tags.
/// - For each field you want to add to the event, call one of the `add` methods,
///   `builder.add_KIND("FieldName", field_value, FieldFormat::FORMAT, field_tag)`.
///   - Use [EventBuilder::add_value] to add a field containing a simple value like a
///     bool, char, float, integer, or UUID.
///   - Use [EventBuilder::add_value_sequence] to add a field containing a sequence of
///     simple values like an array of integers.
///   - Use [EventBuilder::add_str] to add a field containing a string or a binary blob.
///   - Use [EventBuilder::add_str_sequence] to add a field containing a sequence of strings or
///     a sequence of binary blobs.
///   - Use [EventBuilder::add_struct] to add a field that contains a group of sub-fields.
///   - The field name should be short and distinct.
///   - The `field_format` controls the how the bytes of the field value will be interpreted
///     by the decoder. For example, if you add a field with a 4-byte value, the
///     `field_format` tells the decoder whether to treat these 4 bytes as a decimal signed
///     integer, a decimal unsigned integer, a hexadecimal unsigned integer, a UCS-4 char, a
///     float, a time, an errno, etc.
///   - field_tag is a 16-bit provider-defined value that will be included in the
///     metadata of the field. Use 0 if you are not using field tags.
/// - If appropriate, configure other event options by calling:
///   - `builder.id_version(id, ver)` to set a manually-assigned event id and version.
///   - `builder.opcode(opcode)` to specify special semantics for the event, e.g.
///      activity-start or activity-stop.
/// - Call `builder.write(event_set, activity_id, related_id)` to
///   send the event to the kernel.
///   - `activity_id` is an optional 128-bit value that can be used during trace
///     analysis to group and correlate events. This should be specified for events that are
///     part of an activity (part of a group of related events), or `None` if the event is
///     not part of an activity.
///   - `related_id` is an optional 128-bit value that indicates the parent of a
///     newly-started activity. This may be specified for
///     [ActivityStart](Opcode::ActivityStart) events and should be `None` for other events.
///
/// # Event Size Limits
///
/// Linux tracepoints have a 64KB size limit. The size includes event headers (timestamp,
/// level, etc.), metadata (event name, field names, field types), and data (field values).
/// Events that are too large will cause `builder.write` to return an error.
#[derive(Debug)]
pub struct EventBuilder {
    meta: Vec<u8>,
    data: Vec<u8>,
    flags: HeaderFlags,
    version: u8,
    id: u16,
    tag: u16,
    opcode: Opcode,
}

impl EventBuilder {
    /// Returns a new event builder with default initial buffer capacity.
    ///
    /// Default capacity is currently 256 bytes for meta and 256 bytes for data.
    /// Buffers will automatically grow as needed.
    pub fn new() -> EventBuilder {
        return Self::new_with_capacity(256, 256);
    }

    /// Returns a new event builder with specified initial buffer capacities.
    /// Buffers will automatically grow as needed.
    pub fn new_with_capacity(meta_capacity: u16, data_capacity: u16) -> EventBuilder {
        let mut b = EventBuilder {
            meta: Vec::with_capacity(if meta_capacity < 4 {
                4
            } else {
                meta_capacity as usize
            }),
            data: Vec::with_capacity(data_capacity as usize),
            flags: HeaderFlags::DefaultWithExtension,
            version: 0,
            id: 0,
            tag: 0,
            opcode: Opcode::Info,
        };
        b.meta.resize(1, 0); // u8 name_nul_termination = 0;
        return b;
    }

    /// Clears the previous event (if any) from the builder and starts building a new
    /// event.
    ///
    /// - `name` is the event name. It should be short and unique. It must not contain any
    ///   `'\0'` bytes.
    ///
    /// - `event_tag` is a 16-bit integer that will be recorded in the event and can be
    ///   used for any provider-defined purpose. Use 0 if you are not using event tags.
    pub fn reset(&mut self, name: &str, event_tag: u16) -> &mut Self {
        debug_assert!(!name.contains('\0'), "event name must not contain '\\0'");

        self.meta.clear();
        self.data.clear();
        self.flags = HeaderFlags::DefaultWithExtension;
        self.version = 0;
        self.id = 0;
        self.tag = event_tag;
        self.opcode = Opcode::Info;

        self.meta.extend_from_slice(name.as_bytes());
        self.meta.push(0); // nul termination

        return self;
    }

    /// Sends the finished event to the kernel with the provider, event level, and event
    /// keyword of the specified event set.
    ///
    /// - `event_set` should be a registered and enabled event set. Calling `write` on an
    ///   unregistered or disabled event set is a safe no-op (usually returns 0 in this
    ///   case, though it may return `ERANGE` if the event is too large).
    ///
    /// - `activity_id` contains the activity id to be assigned to the event. Use `None` if
    ///   the event is not part of an activity. (An activity is a group of related events
    ///   that all have the same activity id, started by an event with [Opcode::ActivityStart]
    ///   and ended by an event with [Opcode::ActivityStop].)
    ///
    /// - `related_id` contains the related activity id (parent activity) to be used for an
    ///   activity-start event. Use `None` if this is not an activity-start event or if the
    ///   activity does not have a parent activity. If `activity_id` is `None`, this must
    ///   also be `None`.
    ///
    /// Returns 0 for success. Returns a nonzero `errno` value for failure. The return
    /// value is for diagnostic/debugging purposes only and should generally be ignored
    /// in retail builds. Returns `ERANGE` (34) if the event (headers + metadata + data)
    /// is greater than 64KB. Returns other errors as reported by `writev`.
    pub fn write(
        &self,
        event_set: &EventSet,
        activity_id: Option<&[u8; 16]>,
        related_id: Option<&[u8; 16]>,
    ) -> i32 {
        debug_assert!(related_id.is_none() || activity_id.is_some());
        return if self.meta.len() + self.data.len() > 65535 - (52 + 16) {
            34 // libc::ERANGE
        } else {
            event_set.state().write_eventheader(
                &EventHeader {
                    flags: self.flags,
                    version: self.version,
                    id: self.id,
                    tag: self.tag,
                    opcode: self.opcode,
                    level: event_set.level(),
                },
                activity_id,
                related_id,
                self.meta.len() as u16,
                &mut [
                    EventDataDescriptor::zero(),
                    EventDataDescriptor::from_bytes(&self.meta),
                    EventDataDescriptor::from_bytes(&self.data),
                ],
            )
        };
    }

    /// Sets the id and version of the event. Default is id = 0, version = 0.
    ///
    /// EventHeader events are primarily identified by event name, not by event id.
    /// Most events use id = 0, version = 0 and therefore do not need to call this
    /// method.
    ///
    /// Events should use id = 0 and version = 0 unless they have a manually-assigned
    /// stable id. If the event has a manually-assigned stable id, it must be a nonzero
    /// value and the version should be incremented each time the event schema changes
    /// (i.e. each time the field names or field types are changed).
    pub fn id_version(&mut self, id: u16, version: u8) -> &mut Self {
        self.id = id;
        self.version = version;
        return self;
    }

    /// Sets the opcode of the event. Default opcode is [Opcode::Info] (0).
    ///
    /// Most events use opcode `Info` and therefore do not need to call this method.
    ///
    /// You can use opcode to create an activity (a group of related events):
    ///
    /// 1. Begin the activity by writing an activity-start event with opcode =
    ///    `ActivityStart`, activity_id = the id of the new activity (generated by e.g.
    ///    [`Uuid::new_v4`](https://docs.rs/uuid/latest/uuid/struct.Uuid.html#method.new_v4)),
    ///    and related_id = the id of the parent activity (or `None` for no parent).
    /// 2. As appropriate, write activity-info events with opcode = Info,
    ///    activity_id = the id of the activity, and related_id = None.
    /// 3. End the activity by writing an activity-stop event with opcode =
    ///    `ActivityStop`, activity_id = the id of the activity, related_id = `None`,
    ///    and the same event set as was used for the activity-start event.
    pub fn opcode(&mut self, opcode: Opcode) -> &mut Self {
        self.opcode = opcode;
        return self;
    }

    /// Adds a field containing the specified number of sub-fields.
    ///
    /// A struct is a way to logically group a number of fields. To add a struct to
    /// an event, call `builder.add_struct("StructName", struct_field_count, field_tag)`.
    /// Then add `struct_field_count` more fields and they will be considered to be
    /// members of the struct.
    ///
    /// - `field_name` should be a short and distinct string that describes the field.
    ///
    /// - `struct_field_count` specifies the number of subsequent fields that will be
    ///   considered to be part of this struct field. This must be in the range 1 to
    ///   127. Empty structs (structs that contain no fields) are not permitted.
    ///
    /// - `field_tag` is a 16-bit integer that will be recorded in the field and can be
    ///   used for any provider-defined purpose. Use 0 if you are not using field tags.
    ///
    /// Structs can nest. Each nested struct and its fields count as 1 field for the
    /// parent struct.
    pub fn add_struct(
        &mut self,
        field_name: &str,
        struct_field_count: u8,
        field_tag: u16,
    ) -> &mut Self {
        let masked_field_count = struct_field_count & FieldFormat::ValueMask;
        debug_assert_eq!(
            masked_field_count, struct_field_count,
            "struct_field_count must be less than 128"
        );
        assert!(masked_field_count != 0, "struct_field_count must not be 0");
        return self.raw_add_meta(
            field_name,
            FieldEncoding::Struct.as_int(),
            masked_field_count,
            field_tag,
        );
    }

    /// Adds a field containing a simple value.
    ///
    /// - `field_name` should be a short and distinct string that describes the field.
    ///
    /// - `field_value` provides the data for the field. Note that the data is treated
    ///   as raw bytes, i.e. there will be no error, warning, or data conversion if the
    ///   type of the `field_value` parameter conflicts with the [FieldFormat] specified
    ///   by the `format` parameter. See below for the types accepted for this parameter.
    ///
    /// - `format` indicates how the decoder should interpret the field data. For example,
    ///   if the field value is `i8` or `i32`, you would likely set `format` to
    ///   [FieldFormat::SignedInt], and if the field value is `f64`, you would likely set
    ///   `format` to [FieldFormat::Float].
    ///
    /// - `field_tag` is a 16-bit integer that will be recorded in the field and can be
    ///   used for any provider-defined purpose. Use 0 if you are not using field tags.
    ///
    /// Types:
    ///
    /// - If `field_value` is a 1-byte type (`u8`, `i8`, `bool`, or `[u8;1]`), the field
    ///   will be encoded as [FieldEncoding::Value8]. For 1-byte types, if `format` is
    ///   [FieldFormat::Default], the field will be formatted as [FieldFormat::UnsignedInt].
    ///   Usable formats for 1-byte types include: `UnsignedInt`, `SignedInt`, `HexInt`,
    ///   `Boolean`, `HexBytes`, `String8`.
    /// - If `field_value` is a 2-byte type (`u16`, `i16`, or `[u8;2]`), the field will be
    ///   encoded as [FieldEncoding::Value16]. For 2-byte types, if `format` is
    ///   [FieldFormat::Default], the field will be formatted as [FieldFormat::UnsignedInt].
    ///   Usable formats for 2-byte types include: `UnsignedInt`, `SignedInt`, `HexInt`,
    ///   `Boolean`, `HexBytes`, `StringUtf`, `Port`.
    /// - If `field_value` is a 4-byte type (`u32`, `i32`, `f32`, `char`, or `[u8;4]`), the
    ///   field will be encoded as [FieldEncoding::Value32]. For 4-byte types, if `format` is
    ///   [FieldFormat::Default], the field will be formatted as [FieldFormat::UnsignedInt].
    ///   Usable formats for 4-byte types include: `UnsignedInt`, `SignedInt`, `HexInt`,
    ///   `Errno`, `Pid`, `Time`, `Boolean`, `Float`, `HexBytes`, `StringUtf`, `IPv4`.
    /// - If `field_value` is an 8-byte type (`u64`, `i64`, `f64`, or `[u8;8]`), the field
    ///   will be encoded as [FieldEncoding::Value64]. For 8-byte types, if `format` is
    ///   [FieldFormat::Default], the field will be formatted as [FieldFormat::UnsignedInt].
    ///   Usable formats include: `UnsignedInt`, `SignedInt`, `HexInt`, `Time`, `Float`,
    ///   `HexBytes`.
    /// - If `field_value` is a pointer-size type (`usize` or `isize`), the field will be
    ///   encoded as [FieldEncoding::ValueSize] (which is an alias for either `Value32` or
    ///   `Value64`). For pointer-sized types, if `format` is [FieldFormat::Default], the
    ///   field will be formatted as [FieldFormat::UnsignedInt]. Usable formats for
    ///   pointer-sized types include: `UnsignedInt`, `SignedInt`, `HexInt`, `Time`, `Float`,
    ///   `HexBytes`.
    /// - If `field_value` is a 16-byte type (`[u8;16]`), the field will be encoded as
    ///   [FieldEncoding::Value128]. For 16-byte types, if `format` is [FieldFormat::Default],
    ///   the field will be formatted as [FieldFormat::HexBytes]. Usable formats for 16-byte
    ///   types include: `HexBytes`, `Uuid`, `IPv6`.
    ///
    /// Notes:
    ///
    /// - Using [FieldFormat::Default] instead of another [FieldFormat] value saves 1 byte
    ///   per event in the trace data.
    ///   - For small values (1-8 bytes), [FieldFormat::Default] is equivalent to
    ///     [FieldFormat::UnsignedInt], so if logging a small value that you want formatted
    ///     as an unsigned decimal integer, you can save 1 byte per event with no change in
    ///     decoding behavior by using [FieldFormat::Default] instead of
    ///     [FieldFormat::UnsignedInt] for that field.
    ///   - For 16-byte values (i.e. `[u8;16]`), [FieldFormat::Default] is equivalent to
    ///     [FieldFormat::HexBytes], so if logging a 16-byte value that you want formatted
    ///     as hexadecimal bytes, you can save 1 byte per event with no change in decoding
    ///     behavior by using [FieldFormat::Default] instead of [FieldFormat::HexBytes]
    ///     for that field.
    pub fn add_value<V: ValueField>(
        &mut self,
        field_name: &str,
        field_value: V,
        format: FieldFormat,
        field_tag: u16,
    ) -> &mut Self {
        return self
            .raw_add_meta_scalar(field_name, V::VALUE_ENCODING, format, field_tag)
            .raw_add_data_value(&field_value);
    }

    /// Adds a field containing a sequence of simple values such as an array of integers.
    ///
    /// - `field_name` should be a short and distinct string that describes the field.
    ///
    /// - `field_value` provides the data for the field as an `IntoIterator`-of-reference-
    ///   to-ELEMENT, e.g. `&[u8]` or `&[float]`. The ELEMENT types accepted by this
    ///   method are the same as the value types accepted by [EventBuilder::add_value].
    ///   Note that the element data is treated as raw bytes, i.e. there will be no error,
    ///   warning, or data conversion if the type of the element conflicts with the
    ///   [FieldFormat] specified by the `format` parameter.
    ///
    /// - `format` indicates how the decoder should interpret the field data. For example,
    ///   if the field value is `&[i8]` or `&[i32]`, you would likely set `format` to
    ///   [FieldFormat::SignedInt], and if the field value is `&[f64]`, you would likely set
    ///   `format` to [FieldFormat::Float].
    ///
    /// - `field_tag` is a 16-bit integer that will be recorded in the field and can be
    ///   used for any provider-defined purpose. Use 0 if you are not using field tags.
    ///
    /// See [EventBuilder::add_value] for additional details about the compatible element
    /// types and how they are treated.
    ///
    /// For strings or binary blobs, use [EventBuilder::add_str] instead of this method.
    /// If you pass a string or blob to this method, the decoder will format the field as
    /// an array of values (e.g. `['a', 'b', 'c']` or `[0x61, 0x62, 0x63]`) rather than as a
    /// string or blob (e.g. `"abc"` or `"61 62 63"`).
    pub fn add_value_sequence<'a, V: 'a + ValueField>(
        &mut self,
        field_name: &str,
        field_values: impl IntoIterator<Item = &'a V>,
        format: FieldFormat,
        field_tag: u16,
    ) -> &mut Self {
        return self
            .raw_add_meta_vcount(field_name, V::VALUE_ENCODING, format, field_tag)
            .raw_add_data_range(field_values, |this, value| {
                this.raw_add_data_value(value);
            });
    }

    /// Adds a field containing a string or a binary blob.
    ///
    /// - `field_name` should be a short and distinct string that describes the field.
    ///
    /// - `field_value` provides the data for the field as a `&[ELEMENT]`, e.g. `&[u8]`
    ///   or `&[char]`. See below for the ELEMENT types accepted for this parameter.
    ///
    /// - `format` indicates how the decoder should interpret the field data. For example,
    ///   if the field value is a Unicode string, you would likely set `format` to
    ///   [FieldFormat::Default] (resulting in the field decoding as `StringUtf`), and if
    ///   the field value is a binary blob, you would likely set `format` to
    ///   [FieldFormat::HexBytes].
    ///
    /// - `field_tag` is a 16-bit integer that will be recorded in the field and can be
    ///   used for any provider-defined purpose. Use 0 if you are not using field tags.
    ///
    /// Types:
    ///
    /// - ELEMENT may be `u8`, `u16`, `u32`, `i8`, `i16`, `i32`, or `char`.
    /// - The field will be encoded as one of [FieldEncoding::StringLength16Char8],
    ///   [FieldEncoding::StringLength16Char16], or [FieldEncoding::StringLength16Char32],
    ///   based on the size of ELEMENT.
    /// - If `format` is [FieldFormat::Default], the field will be formatted as
    ///   [FieldFormat::StringUtf].
    /// - Usable formats include: `HexBytes`, `StringUtfBom`, `StringXml`, `StringJson`.
    /// - If ELEMENT is `u8` or `i8`, you may also use format `String8`, indicating a
    ///   non-Unicode string (usually treated as Latin-1).
    ///
    /// Note that [FieldFormat::Default] saves 1 byte in the trace. For string/binary
    /// encodings, [FieldFormat::Default] is treated as [FieldFormat::StringUtf], so you
    /// can save 1 byte in the trace by using [FieldFormat::Default] instead of
    /// [FieldFormat::StringUtf] for string fields.
    ///
    /// This is the same as `add_cstr` except that the field will be encoded as a
    /// counted sequence instead of as a nul-terminated string. In most cases you
    /// should prefer this method and use `add_cstr` only if you specifically need
    /// the nul-terminated encoding.
    pub fn add_str<V: StringField>(
        &mut self,
        field_name: &str,
        field_value: impl AsRef<[V]>,
        format: FieldFormat,
        field_tag: u16,
    ) -> &mut Self {
        return self
            .raw_add_meta_scalar(field_name, V::STRING_ENCODING, format, field_tag)
            .raw_add_data_counted(field_value.as_ref());
    }

    /// Adds a field containing a sequence of strings or binary blobs.
    ///
    /// - `field_name` should be a short and distinct string that describes the field.
    ///
    /// - `field_value` provides the data for the field as an
    ///   `IntoIterator`-of-`&[ELEMENT]`.
    ///
    /// - `format` indicates how the decoder should interpret the field data. For example,
    ///   if the field value contains Unicode strings, you would likely set `format` to
    ///   [FieldFormat::Default] (resulting in the field decoding as `StringUtf`), and if
    ///   the field value contains binary blobs, you would likely set `format` to
    ///   [FieldFormat::HexBytes].
    ///
    /// - `field_tag` is a 16-bit integer that will be recorded in the field and can be
    ///   used for any provider-defined purpose. Use 0 if you are not using field tags.
    ///
    /// See [EventBuilder::add_str] for additional details about the compatible element
    /// types and how they are treated.
    ///
    /// This is the same as `add_cstr_sequence` except that the field will be encoded
    /// as a counted sequence instead of as a nul-terminated string. In most cases you
    /// should prefer this method and use `add_cstr_sequence` only if you specifically
    /// need the nul-terminated encoding.
    pub fn add_str_sequence<I: IntoIterator, V: StringField>(
        &mut self,
        field_name: &str,
        field_values: I,
        format: FieldFormat,
        field_tag: u16,
    ) -> &mut Self
    where
        I::Item: AsRef<[V]>,
    {
        return self
            .raw_add_meta_vcount(field_name, V::STRING_ENCODING, format, field_tag)
            .raw_add_data_range(field_values, |this, value| {
                this.raw_add_data_counted(value.as_ref());
            });
    }

    /// Adds a field containing a nul-terminated string.
    ///
    /// - `field_name` should be a short and distinct string that describes the field.
    ///
    /// - `field_value` provides the data for the field as a `&[ELEMENT]`, e.g. `&[u8]`
    ///   or `&[char]`. The field will include the provided values up to the first `0`
    ///   value in the slice (if any). See below for the element types accepted for this
    ///   parameter.
    ///
    /// - `format` indicates how the decoder should interpret the field data. For example,
    ///   if the field value is a Unicode string, you would likely set `format` to
    ///   [FieldFormat::Default] (resulting in the field decoding as `StringUtf`), and if
    ///   the field value is a binary blob, you would likely set `format` to
    ///   [FieldFormat::HexBytes].
    ///
    /// - `field_tag` is a 16-bit integer that will be recorded in the field and can be
    ///   used for any provider-defined purpose. Use 0 if you are not using field tags.
    ///
    /// Types:
    ///
    /// - ELEMENT may be `u8`, `u16`, `u32`, `i8`, `i16`, `i32`, or `char`.
    /// - The field will be encoded as one of [FieldEncoding::ZStringChar8],
    ///   [FieldEncoding::ZStringChar16], or [FieldEncoding::ZStringChar32], based on
    ///   the size of ELEMENT.
    /// - If `format` is [FieldFormat::Default], the field will be formatted as
    ///   [FieldFormat::StringUtf].
    /// - Usable formats include: `HexBytes`, `StringUtfBom`, `StringXml`, `StringJson`.
    /// - If ELEMENT is `u8` or `i8`, you may also use format `String8`, indicating a
    ///   non-Unicode string (usually treated as Latin-1).
    ///
    /// Note that [FieldFormat::Default] saves 1 byte in the trace. For string/binary
    /// encodings, [FieldFormat::Default] is treated as [FieldFormat::StringUtf], so you
    /// can save 1 byte in the trace by using [FieldFormat::Default] instead of
    /// [FieldFormat::StringUtf] for string fields.
    ///
    /// This is the same as `add_str` except that the field will be encoded as a
    /// nul-terminated string instead of as a counted string. In most cases you
    /// should prefer `add_str` and use this method only if you specifically need
    /// the nul-terminated encoding.
    pub fn add_cstr<V: StringField>(
        &mut self,
        field_name: &str,
        field_value: impl AsRef<[V]>,
        format: FieldFormat,
        field_tag: u16,
    ) -> &mut Self {
        return self
            .raw_add_meta_scalar(field_name, V::ZSTRING_ENCODING, format, field_tag)
            .raw_add_data_cstr(field_value.as_ref());
    }

    /// Adds a field containing a sequence of nul-terminated strings.
    ///
    /// - `field_name` should be a short and distinct string that describes the field.
    ///
    /// - `field_value` provides the data for the field as an
    ///   `IntoIterator`-of-`&[ELEMENT]`.
    ///
    /// - `format` indicates how the decoder should interpret the field data. For example,
    ///   if the field value contains Unicode strings, you would likely set `format` to
    ///   [FieldFormat::StringUtf].
    ///
    /// - `field_tag` is a 16-bit integer that will be recorded in the field and can be
    ///   used for any provider-defined purpose. Use 0 if you are not using field tags.
    ///
    /// See [EventBuilder::add_cstr] for additional details about the compatible element
    /// types and how they are treated.
    ///
    /// This is the same as `add_str_sequence` except that the field will be encoded as a
    /// nul-terminated string instead of as a counted string. In most cases you should
    /// prefer `add_str_sequence` and use this method only if you specifically need the
    /// nul-terminated encoding.
    pub fn add_cstr_sequence<I: IntoIterator, V: StringField>(
        &mut self,
        field_name: &str,
        field_values: I,
        format: FieldFormat,
        field_tag: u16,
    ) -> &mut Self
    where
        I::Item: AsRef<[V]>,
    {
        return self
            .raw_add_meta_vcount(field_name, V::ZSTRING_ENCODING, format, field_tag)
            .raw_add_data_range(field_values, |this, value| {
                this.raw_add_data_cstr(value.as_ref());
            });
    }

    /// *Advanced scenarios:* Directly adds unchecked metadata to the event. Using this
    /// method may result in events that do not decode correctly.
    ///
    /// There are a few things that are supported by EventHeader that cannot be expressed
    /// by directly calling the add methods, e.g. array-of-struct. If these edge cases are
    /// important, you can use the raw_add_meta and raw_add_data methods to generate events
    /// that would otherwise be impossible. Doing this requires advanced understanding of
    /// the EventHeader encoding system. If done incorrectly, the resulting events will not
    /// decode properly.
    pub fn raw_add_meta_scalar(
        &mut self,
        field_name: &str,
        encoding: FieldEncoding,
        format: FieldFormat,
        field_tag: u16,
    ) -> &mut Self {
        debug_assert_eq!(
            encoding.as_int() & FieldEncoding::FlagMask,
            0,
            "encoding must not include any flags"
        );
        return self.raw_add_meta(field_name, encoding.as_int(), format.as_int(), field_tag);
    }

    /// *Advanced scenarios:* Directly adds unchecked metadata to the event. Using this
    /// method may result in events that do not decode correctly.
    ///
    /// There are a few things that are supported by EventHeader that cannot be expressed
    /// by directly calling the add methods, e.g. array-of-struct. If these edge cases are
    /// important, you can use the raw_add_meta and raw_add_data methods to generate events
    /// that would otherwise be impossible. Doing this requires advanced understanding of
    /// the EventHeader encoding system. If done incorrectly, the resulting events will not
    /// decode properly.
    pub fn raw_add_meta_vcount(
        &mut self,
        field_name: &str,
        encoding: FieldEncoding,
        format: FieldFormat,
        field_tag: u16,
    ) -> &mut Self {
        debug_assert_eq!(
            encoding.as_int() & FieldEncoding::FlagMask,
            0,
            "encoding must not include any flags"
        );
        return self.raw_add_meta(
            field_name,
            encoding.as_int() | FieldEncoding::VArrayFlag,
            format.as_int(),
            field_tag,
        );
    }

    /// *Advanced scenarios:* Directly adds unchecked data to the event. Using this
    /// method may result in events that do not decode correctly.
    ///
    /// There are a few things that are supported by EventHeader that cannot be expressed
    /// by directly calling the add methods, e.g. array-of-struct. If these edge cases are
    /// important, you can use the raw_add_meta and raw_add_data methods to generate events
    /// that would otherwise be impossible. Doing this requires advanced understanding of
    /// the EventHeader encoding system. If done incorrectly, the resulting events will not
    /// decode properly.
    pub fn raw_add_data_value<T: Copy>(&mut self, value: &T) -> &mut Self {
        let value_size = size_of::<T>();
        let old_data_size = self.data.len();
        self.data.reserve(value_size);
        unsafe {
            copy_nonoverlapping(
                value as *const T as *const u8,
                self.data.as_mut_ptr().add(old_data_size),
                value_size,
            );
            self.data.set_len(old_data_size + value_size);
        }
        return self;
    }

    /// *Advanced scenarios:* Directly adds unchecked data to the event. Using this
    /// method may result in events that do not decode correctly.
    ///
    /// There are a few things that are supported by EventHeader that cannot be expressed
    /// by directly calling the add methods, e.g. array-of-struct. If these edge cases are
    /// important, you can use the raw_add_meta and raw_add_data methods to generate events
    /// that would otherwise be impossible. Doing this requires advanced understanding of
    /// the EventHeader encoding system. If done incorrectly, the resulting events will not
    /// decode properly.
    pub fn raw_add_data_slice<T: Copy>(&mut self, value: &[T]) -> &mut Self {
        let value_size = value.len() * size_of::<T>();
        let old_data_size = self.data.len();
        self.data.reserve(value_size);
        unsafe {
            copy_nonoverlapping(
                value.as_ptr() as *const u8,
                self.data.as_mut_ptr().add(old_data_size),
                value_size,
            );
            self.data.set_len(old_data_size + value_size);
        }
        return self;
    }

    fn raw_add_meta(
        &mut self,
        field_name: &str,
        encoding: u8,
        format: u8,
        field_tag: u16,
    ) -> &mut Self {
        debug_assert!(
            !field_name.contains('\0'),
            "field_name must not contain '\\0'"
        );

        self.meta.reserve(field_name.len() + 7);

        self.meta.extend_from_slice(field_name.as_bytes());
        self.meta.push(0); // nul termination

        if field_tag != 0 {
            self.meta.push(0x80 | encoding);
            self.meta.push(0x80 | format);
            self.meta.extend_from_slice(&field_tag.to_ne_bytes());
        } else if format != 0 {
            self.meta.push(0x80 | encoding);
            self.meta.push(format);
        } else {
            self.meta.push(encoding);
        }

        return self;
    }

    fn raw_add_data_cstr<T: Copy + Default + Eq>(&mut self, value: &[T]) -> &mut Self {
        let zero = T::default();
        let mut nul_pos = 0;
        while nul_pos != value.len() {
            if value[nul_pos] == zero {
                return self.raw_add_data_slice(&value[0..nul_pos + 1]);
            }
            nul_pos += 1;
        }

        return self.raw_add_data_slice(value).raw_add_data_value(&zero);
    }

    fn raw_add_data_counted<T: Copy>(&mut self, value: &[T]) -> &mut Self {
        if value.len() > 65535 {
            return self
                .raw_add_data_value(&65535)
                .raw_add_data_slice(&value[0..65535]);
        } else {
            return self
                .raw_add_data_value(&(value.len() as u16))
                .raw_add_data_slice(value);
        }
    }

    fn raw_add_data_range<T: IntoIterator>(
        &mut self,
        field_values: T,
        add_data: impl Fn(&mut Self, T::Item),
    ) -> &mut Self {
        let mut count = 0u16;

        // Reserve space for count.
        let old_data_size = self.data.len();
        self.raw_add_data_value(&count);

        for value in field_values {
            if count == u16::MAX {
                break;
            }
            count += 1;
            add_data(self, value);
        }

        // Save actual value of count.
        self.data[old_data_size..old_data_size + 2].copy_from_slice(&count.to_ne_bytes());
        return self;
    }
}

impl Default for EventBuilder {
    fn default() -> Self {
        return Self::new();
    }
}
