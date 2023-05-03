// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

/*--EventHeader Events--------------------------------------------------------

EventHeader is a tracing convention layered on top of Linux Tracepoints.

To reduce the number of unique Tracepoint names tracked by the kernel, we
use a small number of Tracepoints to manage a larger number of events. All
events with the same attributes (provider name, severity level, category
keyword, etc.) will share one Tracepoint.

- This means we cannot enable/disable events individually. Instead, all events
  with the same attributes will be enabled/disabled as a group.
- This means we cannot rely on the kernel's Tracepoint metadata for event
  identity or event field names/types. Instead, all events contain a common
  header that provides event identity, core event attributes, and support for
  optional event attributes. The kernel's Tracepoint metadata is used only for
  the Tracepoint's name and to determine whether the event follows the
  EventHeader conventions.

We define a naming scheme to be used for the shared Tracepoints:

  TracepointName = ProviderName + '_' + 'L' + EventLevel + 'K' + EventKeyword +
                   [Options]

We define a common event layout to be used by all EventHeader events. The
event has a header, optional header extensions, and then the event data:

  Event = eventheader + [HeaderExtensions] + Data

We define a format to be used for header extensions:

  HeaderExtension = eventheader_extension + ExtensionData

We define a header extension to be used for activity IDs.

We define a header extension to be used for event metadata (event name, field
names, field types).

For use in the event metadata extension, we define a field type system that
supports scalar, string, binary, array, and struct.

Note that we assume that the Tracepoint name corresponding to the event is
available during event decoding. The event decoder obtains the provider name
and keyword for an event by parsing the event's Tracepoint name.

--Provider Names--------------------------------------------------------------

A provider is a component that generates events. Each event from a provider is
associated with a Provider Name that uniquely identifies the provider.

The provider name should be short, yet descriptive enough to minimize the
chance of collision and to help developers track down the component generating
the events. Hierarchical namespaces may be useful for provider names, e.g.
"MyCompany_MyOrg_MyComponent".

Restrictions:

- ProviderName may not contain ' ' or ':' characters.
- strlen(ProviderName + '_' + Attributes) must be less than
  EVENTHEADER_NAME_MAX (256) characters.
- Some event APIs (e.g. tracefs) might impose additional restrictions on
  tracepoint names. For best compatibility, use only ASCII identifier characters
  [A-Za-z0-9_] in provider names.

Event attribute semantics should be consistent within a given provider. While
some event attributes have generally-accepted semantics (e.g. level value 3
is defined below as "warning"), the precise semantics of the attribute values
are defined at the scope of a provider (e.g. different providers will use
different criteria for what constitutes a warning). In addition, some
attributes (tag, keyword) are completely provider-defined. All events with a
particular provider name should use consistent semantics for all attributes
(e.g. keyword bit 0x1 should have a consistent meaning for all events from a
particular provider but will mean something different for other providers).

--Tracepoint Names------------------------------------------------------------

A Tracepoint is registered with the kernel for each unique combination of
ProviderName + Attributes. This allows a larger number of distinct events to
be controlled by a smaller number of kernel Tracepoints while still allowing
events to be enabled/disabled at a reasonable granularity.

The Tracepoint name for an EventHeader event is defined as:

  ProviderName + '_' + 'L' + eventLevel + 'K' + eventKeyword + [Options]
  or printf("%s_L%xK%lx%s", providerName, eventLevel, eventKeyword, options),
  e.g. "MyProvider_L3K2a" or "OtherProvider_L5K0Gperf".

Event level is a uint8 value 1..255 indicating event severity, formatted as
lowercase hexadecimal, e.g. printf("L%x", eventLevel). The defined level values
are: 1 = critical error, 2 = error, 3 = warning, 4 = information, 5 = verbose.

Event keyword is a uint64 bitmask indicating event category membership,
formatted as lowercase hexadecimal, e.g. printf("K%lx", eventKeyword). Each
bit in the keyword corresponds to a provider-defined category, e.g. a provider
might define 0x2 = networking and 0x4 = I/O so that keyword value of 0x2|0x4 =
0x6 would indicate that an event is in both the networking and I/O categories.

Options (optional attributes) can be specified after the keyword attribute.
Each option consists of an uppercase ASCII letter (option type) followed by 0
or more ASCII digits or lowercase ASCII letters (option value). To support
consistent event names, the options must be sorted in alphabetical order, e.g.
"Aoption" should come before "Boption".

The currently defined options are:

- 'G' = provider Group name. Defines a group of providers. This can be used by
  event analysis tools to find all providers that generate a certain kind of
  information.

Restrictions:

- ProviderName may not contain ' ' or ':' characters.
- Tracepoint name must be less than EVENTHEADER_NAME_MAX (256)
  characters in length.
- Some event APIs (e.g. tracefs) might impose additional restrictions on
  tracepoint names. For best compatibility, use only ASCII identifier characters
  [A-Za-z0-9_] in provider names.

--Header-----------------------------------------------------------------------

Because multiple events may share a single Tracepoint, each event must contain
information needed to distinguish it from other events. To enable this, each
event starts with an EventHeader structure which contains information about
the event:

- flags: Bits indicating pointer size (32 or 64 bits), byte order
  (big-endian or little), and whether any header extensions are present.
- opcode: Indicates special event semantics e.g. "normal event",
  "activity start event", "activity end event".
- tag: Provider-defined 16-bit value. Can be used for anything.
- id: 16-bit stable event identifier, or 0 if no identifier is assigned.
- version: 8-bit event version, incremented for e.g. field type changes.
- level: 8-bit event severity level, 1 = critical .. 5 = verbose.
  (level value in event header must match the level in the Tracepoint name.)

If the extension flag is not set, the header is immediately followed by the
event payload.

If the extension flag is set, the header is immediately followed by one or more
header extensions. Each header extension has a 16-bit size, a 15-bit type code,
and a 1-bit flag indicating whether another header extension follows the
current extension. The final header extension is immediately followed by the
event payload.

The following header extensions are defined:

- Activity ID: Contains a 128-bit ID that can be used to correlate events. May
  also contain the 128-bit ID of the parent activity (typically used only for
  the first event of an activity).
- Metadata: Contains the event's metadata: event name, event attributes, field
  names, field attributes, and field types. Both simple (e.g. Int32, HexInt16,
  Float64, Char32, Uuid) and complex (e.g. NulTerminatedString8,
  CountedString16, Binary, Struct, Array) types are supported.
*/
namespace EventHeaderDecode
{
    /// <summary>
    /// <para>
    /// Core metadata for an EventHeader event.
    /// </para><para>
    /// Each EventHeader event starts with an instance of the EventHeader structure.
    /// It contains core information recorded for every event to help with event
    /// identification, filtering, and decoding.
    /// </para><para>
    /// If EventHeader.Flags has the Extension bit set then the EventHeader is followed
    /// by one or more EventHeaderExtension blocks. Otherwise the EventHeader is
    /// followed by the event payload data.
    /// </para><para>
    /// If an EventHeaderExtension.Kind has the Chain flag set then the
    /// EventHeaderExtension block is followed immediately (no alignment/padding) by
    /// another extension block. Otherwise it is followed immediately (no
    /// alignment/padding) by the event payload data.
    /// </para><para>
    /// If there is a Metadata extension then it contains the event name, field names,
    /// and field types needed to decode the payload data. Otherwise, the payload
    /// decoding system is defined externally, i.e. you will use the provider name to
    /// find the appropriate decoding manifest, then use the event's id+version to
    /// find the decoding information within the manifest, then use that decoding
    /// information to decode the event payload data.
    /// </para><para>
    /// For a particular event definition (i.e. for a particular event name, or for a
    /// particular event id+version), the information in the EventHeader (and in the
    /// Metadata extension, if present) should be constant. For example, instead of
    /// having a single event with a runtime-variable level, you should have a
    /// distinct event definition (with distinct event name and/or distinct event id)
    /// for each level.
    /// </para>
    /// </summary>
    public struct EventHeader
    {
        /// <summary>
        /// Pointer64, LittleEndian, Extension.
        /// </summary>
        public EventHeaderFlags Flags;

        /// <summary>
        /// Increment Version whenever event layout changes.
        /// </summary>
        public byte Version;

        /// <summary>
        /// Stable id for this event, or 0 if none.
        /// </summary>
        public ushort Id;

        /// <summary>
        /// Provider-defined event tag, or 0 if none.
        /// </summary>
        public ushort Tag;

        /// <summary>
        /// EventOpcode: info, start activity, stop activity, etc.
        /// </summary>
        public EventOpcode Opcode;

        /// <summary>
        /// EventLevel: critical, error, warning, info, verbose.
        /// </summary>
        public EventLevel Level;

        // Followed by: EventHeaderExtension block(s), then event payload.
    }

    /// <summary>
    /// Values for EventHeader.Flags.
    /// </summary>
    [System.Flags]
    public enum EventHeaderFlags : byte
    {
        /// <summary>
        /// Pointer32, BigEndian, no extensions.
        /// </summary>
        None = 0x00,

        /// <summary>
        /// Pointer is 64 bits, not 32 bits.
        /// </summary>
        Pointer64 = 0x01,

        /// <summary>
        /// Event uses little-endian, not big-endian.
        /// </summary>
        LittleEndian = 0x02,

        /// <summary>
        /// There is at least one EventHeaderExtension block.
        /// </summary>
        Extension = 0x04,
    }

    /// <summary>
    /// <para>
    /// Values for EventHeader.Opcode. Special semantics for events.
    /// </para><para>
    /// Most events set Opcode = Info (0). Other Opcode values add special semantics to
    /// an event that help the event analysis tool with grouping related events. The
    /// most frequently-used special semantics are ActivityStart and ActivityStop.
    /// </para><para>
    /// To record an activity:
    /// </para><list type="bullet"><item>
    /// Generate a new activity id. An activity id is a 128-bit value that must be
    /// unique within the trace. This can be a UUID or it can be generated by any
    /// other id-generation system that is unlikely to create the same value for any
    /// other activity id in the same trace.
    /// </item><item>
    /// Write an event with opcode = ActivityStart and with an ActivityId header
    /// extension. The ActivityId extension should have the newly-generated activity
    /// id, followed by the id of a parent activity (if any). If there is a parent
    /// activity, the extension length will be 32; otherwise it will be 16.
    /// </item><item>
    /// As appropriate, write any number of normal events (events with opcode set to
    /// something other than ActivityStart or ActivityStop, e.g. opcode = Info). To
    /// indicate that the events are part of the activity, each of these events
    /// should have an ActivityId header extension with the new activity id
    /// (extension length will be 16).
    /// </item><item>
    /// When the activity ends, write an event with opcode = ActivityStop and with
    /// an ActivityId header extension containing the activity id of the activity
    /// that is ending (extension length will be 16).
    /// </item></list>
    /// </summary>
    public enum EventOpcode : byte
    {
        /// <summary>
        /// Normal informational event.
        /// </summary>
        Info = 0,

        /// <summary>
        /// Begins an activity (the first event to use a particular activity id).
        /// </summary>
        ActivityStart,

        /// <summary>
        /// Ends an activity (the last event to use the particular activity id).
        /// </summary>
        ActivityStop,
        CollectionStart,
        CollectionStop,
        Extension,
        Reply,
        Resume,
        Suspend,
        Send,
        Receive = 0xf0,
    }

    /// <summary>
    /// Values for EventHeader.Level.
    /// </summary>
    public enum EventLevel : byte
    {
        Invalid = 0,
        CriticalError,
        Error,
        Warning,
        Information,
        Verbose,
    }

    /// <summary>
    /// <para>
    /// Additional information for an EventHeader event.
    /// </para><para>
    /// If EventHeader.Flags has the Extension bit set then the EventHeader is
    /// followed by one or more EventHeaderExtension blocks. Otherwise the EventHeader
    /// is followed by the event payload data.
    /// </para><para>
    /// If EventHeaderExtension.Kind has the Chain flag set then the
    /// EventHeaderExtension block is followed immediately (no alignment/padding) by
    /// another extension block. Otherwise it is followed immediately (no
    /// alignment/padding) by the event payload data.
    /// </para>
    /// </summary>
    public struct EventHeaderExtension
    {
        public ushort Size;
        public EventHeaderExtensionKind Kind;

        // Followed by Size bytes of data. No padding/alignment.
    }

    /// <summary>
    /// Values for EventHeaderExtension.Kind.
    /// </summary>
    public enum EventHeaderExtensionKind : ushort
    {
        ValueMask = 0x7fff,

        /// <summary>
        /// If not set, this is the last extension block (event payload data follows).
        /// If set, this is not the last extension block (another extension block follows).
        /// </summary>
        FlagChain = 0x8000,

        /// <summary>
        /// Invalid extension kind.
        /// </summary>
        Invalid = 0,

        /// <summary>
        /// <para>
        /// Extension contains an event definition (i.e. event metadata).
        /// </para><para>
        /// Event definition format:
        /// </para><list type="bullet"><item>
        /// char EventName[]; // Nul-terminated utf-8 string: "eventName{;attribName=attribValue}"
        /// </item><item>
        /// 0 or more field definition blocks.
        /// </item></list><para>
        /// Field definition block:
        /// </para><list type="bullet"><item>
        /// char FieldName[]; // Nul-terminated utf-8 string: "fieldName{;attribName=attribValue}"
        /// </item><item>
        /// uint8_t Encoding; // Encoding is 0..31, with 3 flag bits.
        /// </item><item>
        /// uint8_t Format; // Present if (Encoding &amp; 128). Format is 0..127, with 1 flag bit.
        /// </item><item>
        /// uint16_t Tag; // Present if (Format &amp; 128). Contains provider-defined value.
        /// </item><item>
        /// uint16_t ArrayLength; // Present if (Encoding &amp; 32). Contains element count of constant-length array.
        /// </item></list><para>
        /// Notes:
        /// </para><list type="bullet"><item>
        /// eventName and fieldName may not contain any ';' characters.
        /// </item><item>
        /// eventName and fieldName may be followed by attribute strings.
        /// </item><item>
        /// attribute string is: ';' + attribName + '=' + attribValue.
        /// </item><item>
        /// attribName may not contain any ';' or '=' characters.
        /// </item><item>
        /// Semicolons in attribValue must be escaped by doubling, e.g.
        /// "my;value" is escaped as "my;;value".
        /// </item></list>
        /// </summary>
        Metadata,

        /// <summary>
        /// <para>
        /// Extension contains activity id information.
        /// </para><para>
        /// Any event that is part of an activity has an ActivityId extension.
        /// </para><list type="bullet"><item>
        /// Activity is started by an event with opcode = ActivityStart. The
        /// ActivityId extension for the start event must contain a newly-generated
        /// activity id and may optionally contain the parent activity id.
        /// </item><item>
        /// Activity may contain any number of normal events (opcode something other
        /// than ActivityStart or ActivityStop). The ActivityId extension for each
        /// normal event must contain the id of the associated activity (otherwise
        /// it is not considered to be part of the activity).
        /// </item><item>
        /// Activity is ended by an event with opcode = ActivityStop. The ActivityId
        /// extension for the stop event must contain the id of the activity that is
        /// ending.
        /// </item></list><para>
        /// An activity id is a 128-bit value that is unique within this trace
        /// session. It may be a UUID. Since UUID generation can be expensive, this
        /// may also be a 128-bit LUID (locally-unique id), generated using any method
        /// that is unlikely to conflict with other activity ids in the same trace.
        /// </para><para>
        /// If extension.Size == 16 then value is a 128-bit activity id.
        /// </para><para>
        /// If extension.Size == 32 then value is a 128-bit activity id followed by a
        /// 128-bit related (parent) activity id.
        /// </para>
        /// </summary>
        ActivityId,
    }

    /// <summary>
    /// <para>
    /// Values for the Encoding byte of a field definition.
    /// </para><para>
    /// The low 5 bits of the Encoding byte contain the field's encoding. The encoding
    /// indicates how a decoder should determine the size of the field. It also
    /// indicates a default format behavior that should be used if the field has no
    /// format specified or if the specified format is 0, unrecognized, or unsupported.
    /// </para><para>
    /// The top 3 bits of the field encoding byte are flags:
    /// </para><list type="bullet"><item>
    /// FlagCArray indicates that this field is a constant-length array, with the
    /// element count specified as a 16-bit value in the event metadata (must not be
    /// 0).
    /// </item><item>
    /// FlagVArray indicates that this field is a variable-length array, with the
    /// element count specified as a 16-bit value in the event payload (immediately
    /// before the array elements, may be 0).
    /// </item><item>
    /// FlagChain indicates that a format byte is present after the encoding byte.
    /// If Chain is not set, the format byte is omitted and is assumed to be 0.
    /// </item></list><para>
    /// Setting both CArray and VArray is invalid (reserved).
    /// </para>
    /// </summary>
    public enum EventFieldEncoding : byte
    {
        ValueMask = 0x1F,
        FlagMask = 0xE0,

        /// <summary>
        /// Constant-length array: 16-bit element count in metadata (must not be 0).
        /// </summary>
        FlagCArray = 0x20,

        /// <summary>
        /// Variable-length array: 16-bit element count in payload (may be 0).
        /// </summary>
        FlagVArray = 0x40,

        /// <summary>
        /// An EventFieldFormat byte follows the EventFieldEncoding byte.
        /// </summary>
        FlagChain = 0x80,

        /// <summary>
        /// Invalid encoding value.
        /// </summary>
        Invalid = 0,

        /// <summary>
        /// 0-byte value, logically groups subsequent N fields, N = format &amp; 0x7F, N must not be 0.
        /// </summary>
        Struct,

        /// <summary>
        /// 1-byte value, default format UnsignedInt.
        /// </summary>
        Value8,

        /// <summary>
        /// 2-byte value, default format UnsignedInt.
        /// </summary>
        Value16,

        /// <summary>
        /// 4-byte value, default format UnsignedInt.
        /// </summary>
        Value32,

        /// <summary>
        /// 8-byte value, default format UnsignedInt.
        /// </summary>
        Value64,

        /// <summary>
        /// 16-byte value, default format HexBinary.
        /// </summary>
        Value128,

        /// <summary>
        /// zero-terminated uint8[], default format StringUtf.
        /// </summary>
        ZStringChar8,

        /// <summary>
        /// zero-terminated uint16[], default format StringUtf.
        /// </summary>
        ZStringChar16,

        /// <summary>
        /// zero-terminated uint32[], default format StringUtf.
        /// </summary>
        ZStringChar32,

        /// <summary>
        /// uint16 Length followed by uint8 Data[Length], default format StringUtf.
        /// Also used for binary data (format HexBinary).
        /// </summary>
        StringLength16Char8,

        /// <summary>
        /// uint16 Length followed by uint16 Data[Length], default format StringUtf.
        /// </summary>
        StringLength16Char16,

        /// <summary>
        /// uint16 Length followed by uint32 Data[Length], default format StringUtf.
        /// </summary>
        StringLength16Char32,

        /// <summary>
        /// Invalid encoding value. Value will change in future versions of this header..
        /// </summary>
        Max,
    }

    /// <summary>
    /// <para>
    /// Values for the Format byte of a field definition.
    /// </para><para>
    /// The low 7 bits of the Format byte contain the field's format.
    /// In the case of the Struct encoding, the low 7 bits of the Format byte contain
    /// the number of logical fields in the struct (which must not be 0).
    /// </para><para>
    /// The top bit of the field format byte is the FlagChain. If set, it indicates
    /// that a field tag (uint16) is present after the format byte. If not set, the
    /// field tag is not present and is assumed to be 0.
    /// </para>
    /// </summary>
    public enum EventFieldFormat : byte
    {
        ValueMask = 0x7F,

        /// <summary>
        /// A field tag (uint16) follows the Format byte.
        /// </summary>
        FlagChain = 0x80,

        /// <summary>
        /// Use the default format of the encoding.
        /// </summary>
        Default = 0,

        /// <summary>
        /// unsigned integer, event byte order.
        /// Use with Value8..Value64 encodings.
        /// </summary>
        UnsignedInt,

        /// <summary>
        /// signed integer, event byte order.
        /// Use with Value8..Value64 encodings.
        /// </summary>
        SignedInt,

        /// <summary>
        /// hex integer, event byte order.
        /// Use with Value8..Value64 encodings.
        /// </summary>
        HexInt,

        /// <summary>
        /// errno, event byte order.
        /// Use with Value32 encoding.
        /// </summary>
        Errno,

        /// <summary>
        /// process id, event byte order.
        /// Use with Value32 encoding.
        /// </summary>
        Pid,

        /// <summary>
        /// signed integer, event byte order, seconds since 1970.
        /// Use with Value32 or Value64 encodings.
        /// </summary>
        Time,

        /// <summary>
        /// 0 = false, 1 = true, event byte order.
        /// Use with Value8..Value32 encodings.
        /// </summary>
        Boolean,

        /// <summary>
        /// floating point, event byte order.
        /// Use with Value32..Value64 encodings.
        /// </summary>
        Float,

        /// <summary>
        /// binary, decoded as hex dump of bytes.
        /// Use with any encoding.
        /// </summary>
        HexBinary,

        /// <summary>
        /// 8-bit char string, unspecified character set (usually treated as ISO-8859-1 or CP-1252).
        /// Use with Value8 and Char8 encodings.
        /// </summary>
        String8,

        /// <summary>
        /// UTF string, event byte order, code unit size based on encoding.
        /// Use with Value16..Value32 and Char8..Char32 encodings.
        /// </summary>
        StringUtf,

        /// <summary>
        /// UTF string, BOM used if present, otherwise behaves like StringUtf.
        /// Use with Char8..Char32 encodings.
        /// </summary>
        StringUtfBom,

        /// <summary>
        /// XML string, otherwise behaves like StringUtfBom.
        /// Use with Char8..Char32 encodings.
        /// </summary>
        StringXml,

        /// <summary>
        /// JSON string, otherwise behaves like StringUtfBom.
        /// Use with Char8..Char32 encodings.
        /// </summary>
        StringJson,

        /// <summary>
        /// UUID, network byte order (RFC 4122 format).
        /// Use with Value128 encoding.
        /// </summary>
        Uuid,

        /// <summary>
        /// IP port, network byte order (in_port_t layout).
        /// Use with Value16 encoding.
        /// </summary>
        Port,

        /// <summary>
        /// IPv4 address, network byte order (in_addr layout).
        /// Use with Value32 encoding.
        /// </summary>
        IPv4,

        /// <summary>
        /// IPv6 address, in6_addr layout. Use with Value128 encoding.
        /// </summary>
        IPv6,
    }
}
