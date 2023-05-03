// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#![allow(non_upper_case_globals)]

use core::fmt;
use core::mem::size_of;

#[allow(unused_imports)]
use crate::descriptors::EventHeader; // For docs

/// Values for [`EventHeader::flags`].
///
/// Indicates whether the event came uses 32-bit or 64-bit pointers, whether
/// the event uses little-endian or big-endian byte order, and whether the
/// event contains any header extension blocks.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct HeaderFlags(u8);

impl HeaderFlags {
    /// Returns a HeaderFlags with the specified value.
    pub const fn from_int(value: u8) -> Self {
        return Self(value);
    }

    /// Returns the numeric value corresponding to this HeaderFlags value.
    pub const fn as_int(self) -> u8 {
        return self.0;
    }

    /// Pointer-32, big-endian, no extension blocks.
    pub const None: Self = HeaderFlags(0);

    /// Event uses 64-bit pointers, not 32-bit.
    pub const Pointer64: Self = HeaderFlags(0x01);

    /// Event uses little-endian byte order, not big-endian.
    pub const LittleEndian: Self = HeaderFlags(0x02);

    /// There is one or more EventHeaderExtension block.
    pub const Extension: Self = HeaderFlags(0x04);

    /// Pointer-size and Endian flags as appropriate for the target, no Extension
    /// blocks present.
    pub const Default: Self = HeaderFlags(
        if size_of::<usize>() == 8 {
            Self::Pointer64.0
        } else {
            Self::None.0
        } | if cfg!(target_endian = "little") {
            Self::LittleEndian.0
        } else {
            Self::None.0
        },
    );

    /// Pointer-size and Endian flags as appropriate for the target, one or more
    /// Extension blocks present.
    pub const DefaultWithExtension: Self = Self(Self::Default.0 | Self::Extension.0);
}

impl fmt::Display for HeaderFlags {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        return self.0.fmt(f);
    }
}

impl From<u8> for HeaderFlags {
    fn from(val: u8) -> Self {
        return Self(val);
    }
}

impl From<HeaderFlags> for u8 {
    fn from(val: HeaderFlags) -> Self {
        return val.0;
    }
}

/// The type of data contined in an [`EventHeaderExtension`] block.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct ExtensionKind(u16);

impl ExtensionKind {
    /// Returns an ExtensionKind with the specified value.
    pub const fn from_int(value: u16) -> Self {
        return Self(value);
    }

    /// Returns the numeric value corresponding to this ExtensionKind value.
    pub const fn as_int(self) -> u16 {
        return self.0;
    }

    /// Invalid extension kind.
    pub const Invalid: Self = Self(0);

    /// Extension contains an event definition (i.e. event metadata).
    /// No padding or alignment bytes.
    ///
    /// Event definition format:
    /// - char event_name[]; // Nul-terminated utf-8 string: "eventName{;attribName=attribValue}\0"
    /// - 0 or more field definition blocks, tightly-packed (no padding).
    ///
    /// Field definition block:
    /// - char field_name[]; // Nul-terminated utf-8 string: "fieldName{;attribName=attribValue}"
    /// - uint8_t encoding; // encoding is 0..31, with 3 flag bits.
    /// - uint8_t format; // Present if 0 != (encoding & 128). format is 0..127, with 1 flag bit.
    /// - uint16_t tag; // Present if 0 != (format & 128). Contains provider-defined value.
    /// - uint16_t array_length; // Present if 0 != (encoding & 32). Contains element count of constant-length array.
    ///
    /// Notes:
    /// - event_name and field_name may not contain any ';' characters.
    /// - event_name and field_name may be followed by 0 or more attribute strings.
    /// - attribute string is: ';' + attribName + '=' + attribValue.
    /// - attribName may not contain any ';' or '=' characters.
    /// - Semicolons in attribValue must be escaped by doubling, e.g.
    ///   "my;value" is escaped as "my;;value".
    /// - array_length may not be 0, i.e. constant-length arrays may not be empty.
    pub const Metadata: Self = Self(1);

    /// Extension contains activity id information.
    ///
    /// Any event that is part of an activity has an ActivityId extension.
    /// - Activity is started by an event with opcode = ActivityStart. The
    ///   ActivityId extension for the start event must contain a newly-generated
    ///   activity id and may optionally contain the parent activity id.
    /// - Activity may contain any number of normal events (opcode something other
    ///   than ActivityStart or ActivityStop). The ActivityId extension for each
    ///   normal event must contain the id of the associated activity (otherwise
    ///   it is not considered to be part of the activity).
    /// - Activity is ended by an event with opcode = ActivityStop. The ActivityId
    ///   extension for the stop event must contain the id of the activity that is
    ///   ending.
    ///
    /// An activity id is a 128-bit value that is unique within this trace
    /// session. It may be a UUID. Since UUID generation can be non-trivial, this
    /// may also be a 128-bit LUID (locally-unique id), generated using any method
    /// that is unlikely to conflict with any other activity ids in the same trace.
    ///
    /// If extension.size == 16 then value is a 128-bit activity id.
    ///
    /// If extension.size == 32 then value is a 128-bit activity id followed by a
    /// 128-bit related (parent) activity id.
    pub const ActivityId: Self = Self(2);

    /// If ChainFlag is not set, this is the last extension block (event payload data follows).
    /// If ChainFlag is set, this is not the last extension block (another extension block follows).
    pub const ChainFlag: u16 = 0x8000;

    /// Mask for the kind field.
    pub const ValueMask: u16 = 0x7FFF;
}

impl fmt::Display for ExtensionKind {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        return self.0.fmt(f);
    }
}

impl From<u16> for ExtensionKind {
    fn from(val: u16) -> Self {
        return Self(val);
    }
}

impl From<ExtensionKind> for u16 {
    fn from(val: ExtensionKind) -> Self {
        return val.0;
    }
}

/// Values for the encoding byte of a field definition.
///
/// The low 5 bits of the encoding byte contain the field's encoding. The encoding
/// indicates how a decoder should determine the size of the field. It also
/// indicates a default format behavior that should be used if the field has no
/// format specified or if the specified format is 0, unrecognized, or unsupported.
///
/// The top 3 bits of the field encoding byte are flags:
/// - `CArrayFlag` indicates that this field is a constant-length array, with the
///   element count specified as a 16-bit value in the event metadata (must not be
///   0).
/// - `VArrayFlag` indicates that this field is a variable-length array, with the
///   element count specified as a 16-bit value in the event payload (immediately
///   before the array elements, may be 0).
/// - `ChainFlag` indicates that a format byte is present after the encoding byte.
///   If `ChainFlag` is not set, the format byte is omitted and is assumed to be 0.
///
/// Setting both `CArrayFlag` and `VArrayFlag` is invalid (reserved).
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct FieldEncoding(u8);

impl FieldEncoding {
    /// Returns a `FieldEncoding` with the specified value.
    pub const fn from_int(value: u8) -> Self {
        return Self(value);
    }

    /// Returns the numeric value corresponding to this `FieldEncoding` value.
    pub const fn as_int(self) -> u8 {
        return self.0;
    }

    /// Invalid encoding value.
    pub const Invalid: Self = Self(0);

    /// 0-byte value, logically groups subsequent N fields, N = `format & 0x7F`, N must not be 0.
    pub const Struct: Self = Self(1);

    /// 1-byte value, default format UnsignedInt.
    ///
    /// Usable formats: UnsignedInt, SignedInt, HexInt, Boolean, HexBytes, String8.
    pub const Value8: Self = Self(2);

    /// 2-byte value, default format UnsignedInt.
    ///
    /// Usable formats: UnsignedInt, SignedInt, HexInt, Boolean, HexBytes, StringUtf, Port.
    pub const Value16: Self = Self(3);

    /// 4-byte value, default format UnsignedInt.
    ///
    /// Usable formats: UnsignedInt, SignedInt, HexInt, Errno, Pid, Time, Boolean, Float,
    /// HexBytes, StringUtf, IPv4.
    pub const Value32: Self = Self(4);

    /// 8-byte value, default format UnsignedInt.
    ///
    /// Usable formats: UnsignedInt, SignedInt, HexInt, Time, Float, HexBytes.
    pub const Value64: Self = Self(5);

    /// 16-byte value, default format HexBytes.
    ///
    /// Usable formats: HexBytes, Uuid, IPv6.
    pub const Value128: Self = Self(6);

    /// zero-terminated uint8[], default format StringUtf.
    ///
    /// Usable formats: HexBytes, String8, StringUtf, StringUtfBom, StringXml,
    /// StringJson.
    pub const ZStringChar8: Self = Self(7);

    /// zero-terminated uint16[], default format StringUtf.
    ///
    /// Usable formats: HexBytes, StringUtf, StringUtfBom, StringXml, StringJson.
    pub const ZStringChar16: Self = Self(8);

    /// zero-terminated uint32[], default format StringUtf.
    ///
    /// Usable formats: HexBytes, StringUtf, StringUtfBom, StringXml, StringJson.
    pub const ZStringChar32: Self = Self(9);

    /// uint16 Length followed by uint8 Data\[Length\], default format StringUtf.
    /// Also used for binary data (format HexBytes).
    ///
    /// Usable formats: HexBytes, String8, StringUtf, StringUtfBom, StringXml,
    /// StringJson.
    pub const StringLength16Char8: Self = Self(10);

    /// uint16 Length followed by uint16 Data\[Length\], default format StringUtf.
    ///
    /// Usable formats: HexBytes, StringUtf, StringUtfBom, StringXml, StringJson.
    pub const StringLength16Char16: Self = Self(11);

    /// uint16 Length followed by uint32 Data\[Length\], default format StringUtf.
    ///
    /// Usable formats: HexBytes, StringUtf, StringUtfBom, StringXml, StringJson.
    pub const StringLength16Char32: Self = Self(12);

    /// usize value, default format UnsignedInt.
    /// This is an alias for either `Value32` or `Value64`.
    ///
    /// Usable formats: UnsignedInt, SignedInt, HexInt, Time, Float, HexBytes.
    pub const ValueSize: Self = if size_of::<usize>() == 8 {
        Self::Value64
    } else {
        Self::Value32
    };

    /// Mask for the kind field.
    pub const ValueMask: u8 = 0x1F;

    /// Mask for the flags.
    pub const FlagMask: u8 = 0xE0;

    /// Constant-length array: 16-bit element count in metadata (count must not be 0).
    pub const CArrayFlag: u8 = 0x20;

    /// Variable-length array: 16-bit element count in payload (count may be 0).
    pub const VArrayFlag: u8 = 0x40;

    /// A FieldFormat byte follows the FieldEncoding byte.
    pub const ChainFlag: u8 = 0x80;
}

impl fmt::Display for FieldEncoding {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        return self.0.fmt(f);
    }
}

impl From<u8> for FieldEncoding {
    fn from(val: u8) -> Self {
        return Self(val);
    }
}

impl From<FieldEncoding> for u8 {
    fn from(val: FieldEncoding) -> Self {
        return val.0;
    }
}

/// Values for the format byte of a field definition.
///
/// The low 7 bits of the format byte contain the field's format.
/// In the case of the `Struct` encoding, the low 7 bits of the format byte contain
/// the number of logical fields in the struct (which must not be 0).
///
/// The top bit of the field format byte is the `ChainFlag`. If set, it indicates
/// that a field tag (uint16) is present after the format byte. If not set, the
/// field tag is not present and is assumed to be 0.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct FieldFormat(u8);

impl FieldFormat {
    /// Returns a FieldFormat with the specified value.
    pub const fn from_int(value: u8) -> Self {
        return Self(value);
    }

    /// Returns the numeric value corresponding to this FieldFormat value.
    pub const fn as_int(self) -> u8 {
        return self.0;
    }

    /// Use the default format of the encoding.
    pub const Default: Self = Self(0);

    /// unsigned integer, event byte order. Use with Value8..Value64 encodings.
    pub const UnsignedInt: Self = Self(1);

    /// signed integer, event byte order. Use with Value8..Value64 encodings.
    pub const SignedInt: Self = Self(2);

    /// hex integer, event byte order. Use with Value8..Value64 encodings.
    pub const HexInt: Self = Self(3);

    /// errno, event byte order. Use with Value32 encoding.
    pub const Errno: Self = Self(4);

    /// process id, event byte order. Use with Value32 encoding.
    pub const Pid: Self = Self(5);

    /// signed integer, event byte order, seconds since 1970. Use with Value32 or Value64 encodings.
    pub const Time: Self = Self(6);

    /// 0 = false, 1 = true, event byte order. Use with Value8..Value32 encodings.
    pub const Boolean: Self = Self(7);

    /// floating point, event byte order. Use with Value32..Value64 encodings.
    pub const Float: Self = Self(8);

    /// binary, decoded as hex dump of bytes. Use with any encoding.
    pub const HexBytes: Self = Self(9);

    /// 8-bit char string, unspecified character set (usually treated as ISO-8859-1 or CP-1252). Use with Value8 and Char8 encodings.
    pub const String8: Self = Self(10);

    /// UTF string, event byte order, code unit size based on encoding. Use with Value16..Value32 and Char8..Char32 encodings.
    pub const StringUtf: Self = Self(11);

    /// UTF string, BOM used if present, otherwise behaves like string_utf. Use with Char8..Char32 encodings.
    pub const StringUtfBom: Self = Self(12);

    /// XML string, otherwise behaves like string_utf_bom. Use with Char8..Char32 encodings.
    pub const StringXml: Self = Self(13);

    /// JSON string, otherwise behaves like string_utf_bom. Use with Char8..Char32 encodings.
    pub const StringJson: Self = Self(14);

    /// UUID, network byte order (RFC 4122 format). Use with Value128 encoding.
    pub const Uuid: Self = Self(15);

    /// IP port, network byte order (in_port_t layout). Use with Value16 encoding.
    pub const Port: Self = Self(16);

    /// IPv4 address, network byte order (in_addr layout). Use with Value32 encoding.
    pub const IPv4: Self = Self(17);

    /// IPv6 address, in6_addr layout. Use with Value128 encoding.
    pub const IPv6: Self = Self(18);

    /// Mask for the type field.
    pub const ValueMask: u8 = 0x7F;

    /// A field tag (uint16) follows the format byte.
    pub const ChainFlag: u8 = 0x80;
}

impl fmt::Display for FieldFormat {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        return self.0.fmt(f);
    }
}

impl From<u8> for FieldFormat {
    fn from(val: u8) -> Self {
        return Self(val);
    }
}

impl From<FieldFormat> for u8 {
    fn from(val: FieldFormat) -> Self {
        return val.0;
    }
}

/// Indicates the severity of an event. Use Verbose if unsure.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct Level(pub(crate) u8);

impl Level {
    /// Returns a level with the specified value.
    #[inline(always)]
    pub const fn from_int(value: u8) -> Level {
        return Level(value);
    }

    /// Returns the integer value of this level.
    #[inline(always)]
    pub const fn as_int(self) -> u8 {
        return self.0;
    }

    /// Invalid event level.
    pub const Invalid: Level = Level(0);

    /// Critical error event.
    pub const CriticalError: Level = Level(1);

    /// Error event.
    pub const Error: Level = Level(2);

    /// Warning event.
    pub const Warning: Level = Level(3);

    /// Informational event.
    pub const Informational: Level = Level(4);

    /// Verbose event.
    pub const Verbose: Level = Level(5);
}

impl fmt::Display for Level {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        return self.0.fmt(f);
    }
}

impl From<u8> for Level {
    fn from(val: u8) -> Self {
        return Self(val);
    }
}

impl From<Level> for u8 {
    fn from(val: Level) -> Self {
        return val.0;
    }
}

/// Values for [`EventHeader::opcode`] indicating special semantics to be used by
/// the event decoder for grouping and organizing events, e.g. for activities.
///
/// Most events set opcode = `Info` (0). Other opcode values add special semantics to
/// an event that help the event analysis tool with grouping related events. The
/// most frequently-used special semantics are `ActivityStart` and `ActivityStop`.
///
/// To record an activity:
///
/// - Generate a new activity id. An activity id is a 128-bit value that must be
///   unique within the trace. This can be a UUID or it can be generated by any
///   other id-generation system that is unlikely to create the same value for any
///   other activity id in the same trace.
/// - Write an event with opcode = `ActivityStart`, with the activity id specified,
///   and if the activity is nesting within a parent activity, the related id
///   specified as the id of the parent activity.
/// - As appropriate, write any number of normal events (events with opcode set to
///   something other than `ActivityStart` or `ActivityStop`, e.g. opcode = `Info`).
///   To indicate that the events are part of the activity, each of these events
///   should have their activity id specified as the id of the activity.
/// - When the activity ends, write an event with opcode = `ActivityStop`,
///   with the activity id specified.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct Opcode(u8);

impl Opcode {
    /// Returns an opcode with the specified value.
    #[inline(always)]
    pub const fn from_int(value: u8) -> Opcode {
        return Opcode(value);
    }

    /// Returns the integer value of this opcode.
    #[inline(always)]
    pub const fn as_int(self) -> u8 {
        return self.0;
    }

    /// Normal event. The event may set activity_id if it is part of an activity.
    pub const Info: Opcode = Opcode(0);

    /// Event indicates the beginning of an activity. The event should set related_id to
    /// the id of the parent activity and should set activity_id to the id of the
    /// newly-started activity. All subsequent events that use the new activity_id will
    /// be considered as part of this activity, up to the corresponding
    /// [ActivityStop](Opcode::ActivityStop) event.
    pub const ActivityStart: Opcode = Opcode(1);

    /// Event indicates the end of an activity. The event should set activity_id
    /// to the id of the activity that is ending and should use the same level
    /// and keyword as were used for the corresponding
    /// [ActivityStart](Opcode::ActivityStart) event.
    pub const ActivityStop: Opcode = Opcode(2);

    /// Data Collection Start event
    pub const CollectionStart: Opcode = Opcode(3);

    /// Data Collection Stop event
    pub const CollectionStop: Opcode = Opcode(4);

    /// Extension event
    pub const Extension: Opcode = Opcode(5);

    /// Reply event
    pub const Reply: Opcode = Opcode(6);

    /// Resume event
    pub const Resume: Opcode = Opcode(7);

    /// Suspend event
    pub const Suspend: Opcode = Opcode(8);

    /// Message Send event
    pub const Send: Opcode = Opcode(9);

    /// Message Receive event
    pub const Receive: Opcode = Opcode(240);

    /// Reserved for future definition by Microsoft
    pub const ReservedOpcode241: Opcode = Opcode(241);

    /// Reserved for future definition by Microsoft
    pub const ReservedOpcode242: Opcode = Opcode(242);

    /// Reserved for future definition by Microsoft
    pub const ReservedOpcode243: Opcode = Opcode(243);

    /// Reserved for future definition by Microsoft
    pub const ReservedOpcode244: Opcode = Opcode(244);

    /// Reserved for future definition by Microsoft
    pub const ReservedOpcode245: Opcode = Opcode(245);

    /// Reserved for future definition by Microsoft
    pub const ReservedOpcode246: Opcode = Opcode(246);

    /// Reserved for future definition by Microsoft
    pub const ReservedOpcode247: Opcode = Opcode(247);

    /// Reserved for future definition by Microsoft
    pub const ReservedOpcode248: Opcode = Opcode(248);

    /// Reserved for future definition by Microsoft
    pub const ReservedOpcode249: Opcode = Opcode(249);

    /// Reserved for future definition by Microsoft
    pub const ReservedOpcode250: Opcode = Opcode(250);

    /// Reserved for future definition by Microsoft
    pub const ReservedOpcode251: Opcode = Opcode(251);

    /// Reserved for future definition by Microsoft
    pub const ReservedOpcode252: Opcode = Opcode(252);

    /// Reserved for future definition by Microsoft
    pub const ReservedOpcode253: Opcode = Opcode(253);

    /// Reserved for future definition by Microsoft
    pub const ReservedOpcode254: Opcode = Opcode(254);

    /// Reserved for future definition by Microsoft
    pub const ReservedOpcode255: Opcode = Opcode(255);
}

impl fmt::Display for Opcode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        return self.0.fmt(f);
    }
}

impl From<u8> for Opcode {
    fn from(val: u8) -> Self {
        return Self(val);
    }
}

impl From<Opcode> for u8 {
    fn from(val: Opcode) -> Self {
        return val.0;
    }
}
