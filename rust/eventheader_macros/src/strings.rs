// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

/// Level names special-cased by level(...) option.
/// Strings must be strcmp-sorted for binary search.
pub const LEVEL_ENUMS: &[&str] = &[
    "CriticalError",
    "Error",
    "Informational",
    "Verbose",
    "Warning",
];

/// Opcode names special-cased by opcode(...) option.
/// Strings must be strcmp-sorted for binary search.
pub const OPCODE_ENUMS: &[&str] = &[
    "ActivityStart",
    "ActivityStop",
    "CollectionStart",
    "CollectionStop",
    "Extension",
    "Info",
    "Receive",
    "Reply",
    "Resume",
    "Send",
    "Suspend",
];

/// FieldEncoding names special-cased by type(...) option.
/// Strings must be strcmp-sorted for binary search.
pub const ENCODING_ENUMS: &[&str] = &[
    "Invalid",
    "StringLength16Char16",
    "StringLength16Char32",
    "StringLength16Char8",
    "Struct",
    "Value128",
    "Value16",
    "Value32",
    "Value64",
    "Value8",
    "ValueSize",
    "ZStringChar16",
    "ZStringChar32",
    "ZStringChar8",
];

/// FieldFormat names special-cased by format(...) option.
/// Strings must be strcmp-sorted for binary search.
pub const FORMAT_ENUMS: &[&str] = &[
    "Boolean",
    "Default",
    "Errno",
    "Float",
    "HexBytes",
    "HexInt",
    "IPv4",
    "IPv6",
    "Pid",
    "Port",
    "SignedInt",
    "String8",
    "StringJson",
    "StringUtf",
    "StringUtfBom",
    "StringXml",
    "Time",
    "UnsignedInt",
    "Uuid",
];

pub const EH_KEYWORD_CONST: &str = "_EH_KEYWORD";
pub const EH_TAG_CONST: &str = "_EH_TAG";
pub const EH_TRACEPOINT_VAR: &str = "_eh_tracepoint";
pub const EH_ARG_VAR: &str = "_eh_arg";
pub const EH_WRITE_FUNC: &str = "_eh_write";
pub const EH_LENGTHS_VAR: &str = "_eh_lengths";
pub const EH_ACTIVITY_ID_VAR: &str = "_eh_aid";
pub const EH_RELATED_ID_VAR: &str = "_eh_rid";
pub const EH_DUR_VAR: &str = "_eh_dur";
pub const EH_TRACEPOINT_STATIC: &str = "_EH_TRACEPOINT";
pub const EH_TRACEPOINT_PTR_STATIC: &str = "_EH_TRACEPOINT_PTR";

pub const TRACEPOINTS_SECTION_PREFIX: &str = "_eh_tracepoints_";
pub const TRACEPOINTS_SECTION_START_PREFIX: &str = "__start__eh_tracepoints_";
pub const TRACEPOINTS_SECTION_STOP_PREFIX: &str = "__stop__eh_tracepoints_";
pub const PROVIDER_PTR_VAR_PREFIX: &str = "_eh_define_provider_";
pub const EH_TRACEPOINT_WRITE_EVENTHEADER: &str = "write_eventheader";
pub const EH_TRACEPOINT_ENABLED: &str = "enabled";

pub const BORROW_BORROW_PATH: &[&str] = &["core", "borrow", "Borrow", "borrow"];
pub const ASREF_PATH: &[&str] = &["core", "convert", "AsRef"];
pub const IDENTITY_PATH: &[&str] = &["core", "convert", "identity"];
pub const NULL_PATH: &[&str] = &["core", "ptr", "null"];
pub const BOOL_PATH: &[&str] = &["core", "primitive", "bool"];
pub const F32_PATH: &[&str] = &["core", "primitive", "f32"];
pub const F64_PATH: &[&str] = &["core", "primitive", "f64"];
pub const I8_PATH: &[&str] = &["core", "primitive", "i8"];
pub const I16_PATH: &[&str] = &["core", "primitive", "i16"];
pub const I32_PATH: &[&str] = &["core", "primitive", "i32"];
pub const I64_PATH: &[&str] = &["core", "primitive", "i64"];
pub const ISIZE_PATH: &[&str] = &["core", "primitive", "isize"];
pub const U8_PATH: &[&str] = &["core", "primitive", "u8"];
pub const U16_PATH: &[&str] = &["core", "primitive", "u16"];
pub const U32_PATH: &[&str] = &["core", "primitive", "u32"];
pub const U64_PATH: &[&str] = &["core", "primitive", "u64"];
pub const USIZE_PATH: &[&str] = &["core", "primitive", "usize"];
pub const OPTION_PATH: &[&str] = &["core", "option", "Option"];
pub const OPTION_NONE_PATH: &[&str] = &["core", "option", "Option", "None"];
pub const OPTION_SOME_PATH: &[&str] = &["core", "option", "Option", "Some"];
pub const RESULT_OK_PATH: &[&str] = &["core", "result", "Result", "Ok"];
pub const RESULT_ERR_PATH: &[&str] = &["core", "result", "Result", "Err"];
pub const SYSTEMTIME_DURATION_SINCE_PATH: &[&str] =
    &["std", "time", "SystemTime", "duration_since"];
pub const SYSTEMTIME_UNIX_EPOCH_PATH: &[&str] = &["std", "time", "SystemTime", "UNIX_EPOCH"];

pub const ENCODING_PATH: &[&str] = &["eventheader", "FieldEncoding"];
pub const LEVEL_VERBOSE_PATH: &[&str] = &["eventheader", "Level", "Verbose"];
pub const OPCODE_INFO_PATH: &[&str] = &["eventheader", "Opcode", "Info"];
pub const FORMAT_PATH: &[&str] = &["eventheader", "FieldFormat"];
pub const FORMAT_FROM_INT_PATH: &[&str] = &["eventheader", "FieldFormat", "from_int"];
pub const GUID_PATH: &[&str] = &["eventheader", "Guid"];
pub const PROVIDER_PATH: &[&str] = &["eventheader", "Provider"];

pub const PROVIDER_NEW_PATH: &[&str] = &["eventheader", "_internal", "provider_new"];
pub const EVENTHEADERTRACEPOINT_PATH: &[&str] =
    &["eventheader", "_internal", "EventHeaderTracepoint"];
pub const EVENTHEADERTRACEPOINT_NEW_PATH: &[&str] =
    &["eventheader", "_internal", "EventHeaderTracepoint", "new"];
pub const TAG_BYTE0_PATH: &[&str] = &["eventheader", "_internal", "tag_byte0"];
pub const TAG_BYTE1_PATH: &[&str] = &["eventheader", "_internal", "tag_byte1"];
pub const SLICE_COUNT_PATH: &[&str] = &["eventheader", "_internal", "slice_count"];
pub const TIME_FROM_DURATION_AFTER_PATH: &[&str] =
    &["eventheader", "_internal", "time_from_duration_after_1970"];
pub const TIME_FROM_DURATION_BEFORE_PATH: &[&str] =
    &["eventheader", "_internal", "time_from_duration_before_1970"];

pub const EVENTHEADER_FROM_PARTS_PATH: &[&str] =
    &["eventheader", "_internal", "EventHeader", "from_parts"];
pub const HEADER_FLAGS_DEFAULT_PATH: &[&str] = &[
    "eventheader",
    "_internal",
    "HeaderFlags",
    "DefaultWithExtension",
];

pub const DATADESC_ZERO_PATH: &[&str] =
    &["eventheader", "_internal", "EventDataDescriptor", "zero"];
pub const DATADESC_FROM_VALUE_PATH: &[&str] = &[
    "eventheader",
    "_internal",
    "EventDataDescriptor",
    "from_value",
];
pub const DATADESC_FROM_CSTR_PATH: &[&str] = &[
    "eventheader",
    "_internal",
    "EventDataDescriptor",
    "from_cstr",
];
pub const DATADESC_FROM_SLICE_PATH: &[&str] = &[
    "eventheader",
    "_internal",
    "EventDataDescriptor",
    "from_slice",
];
