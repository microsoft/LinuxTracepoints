// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

//! Tool that generates a markdown table describing the normal field types supported by
//! the [`write_event`] macro. This table is included in the documentation for
//! `write_event` in `eventheader/src/lib.rs`.

#![allow(clippy::needless_return)]

use std::cmp::Ordering;

#[allow(dead_code)]
#[path = "../src/enums.rs"]
mod enums;

#[allow(dead_code)]
#[path = "../src/field_option.rs"]
mod field_option;

#[path = "../src/field_options.rs"]
mod field_options;

#[allow(dead_code)]
#[path = "../src/strings.rs"]
mod strings;

use enums::*;
use field_option::*;
use field_options::FIELD_OPTIONS;

fn main() {
    println!("/// | Field Type | Rust Type | EventHeader Type");
    println!("/// |------------|-----------|-----------------");

    let mut options = Vec::from_iter(FIELD_OPTIONS);
    options.sort_by(|a, b| option_name_cmp(a.option_name, b.option_name));
    for field in options {
        let s = field.to_markdown();
        if !s.is_empty() {
            println!("{}", &s);
        }
    }
}

/// option_name_cmp tries to be human-friendly:
/// - `foo_slice` sorts immediately after `foo`.
/// - `foo_` sorts immediately after `foo_slice` ("str" < "str_xml" < "str16").
/// - Numbers are sorted by value ("u8" < "u16").
fn option_name_cmp(val1: &str, val2: &str) -> Ordering {
    let mut v1 = val1.as_bytes();
    let mut v2 = val2.as_bytes();
    let mut pos1 = 0;
    let mut pos2 = 0;

    const SLICE: &[u8] = b"_slice";

    let slice1 = v1.ends_with(SLICE);
    if slice1 {
        v1 = &v1[..v1.len() - SLICE.len()];
    }

    let slice2 = v2.ends_with(SLICE);
    if slice2 {
        v2 = &v2[..v2.len() - SLICE.len()];
    }

    let len1 = v1.len();
    let len2 = v2.len();

    let cmp = loop {
        if pos1 == len1 {
            if pos2 == len2 {
                break slice1.cmp(&slice2);
            } else {
                break Ordering::Less;
            }
        } else if pos2 == len2 {
            break Ordering::Greater;
        }

        let ch1 = v1[pos1];
        let ch2 = v2[pos2];

        if ch1.is_ascii_digit() && ch2.is_ascii_digit() {
            let num1 = parse_int(v1, pos1);
            let num2 = parse_int(v2, pos2);

            let num_cmp = num1.value.cmp(&num2.value);
            if num_cmp != Ordering::Equal {
                break num_cmp;
            } else {
                pos1 = num1.end_pos;
                pos2 = num2.end_pos;
            }
        } else {
            let char_cmp = ch1.cmp(&ch2);
            if char_cmp != Ordering::Equal {
                if ch1 == b'_' {
                    break Ordering::Less;
                } else if ch2 == b'_' {
                    break Ordering::Greater;
                } else {
                    break char_cmp;
                }
            }

            pos1 += 1;
            pos2 += 1;
        }
    };

    return cmp;
}

trait ToMarkdown {
    fn to_markdown(&self) -> String;
    fn normal_field(&self, s: &mut String, type_path: &[&str], is_slice: bool, note: &str);
}

impl ToMarkdown for FieldOption {
    /// Produces a markdown table row that describes this field type, or "" if field type is special.
    ///
    /// `| Field Type | Rust Type | EventHeader Type`
    fn to_markdown(&self) -> String {
        let mut s = String::new();

        match self.strategy {
            FieldStrategy::Scalar => {
                let note = if self.option_name == "errno" {
                    "errno"
                } else {
                    ""
                };
                self.normal_field(&mut s, self.value_type, false, note);
            }
            FieldStrategy::Slice => {
                let note = if self.option_name == "errno_slice" {
                    "errno"
                } else {
                    ""
                };
                self.normal_field(&mut s, self.value_type, true, note);
            }
            FieldStrategy::SystemTime => {
                self.normal_field(&mut s, &["std", "time", "SystemTime"], false, "systemtime");
            }
            FieldStrategy::CStr => {
                self.normal_field(&mut s, self.value_type, true, "cstr");
            }
            FieldStrategy::Str => {
                self.normal_field(&mut s, self.value_type, true, "");
            }
            FieldStrategy::Struct
            | FieldStrategy::RawStruct
            | FieldStrategy::RawStructSlice
            | FieldStrategy::RawData
            | FieldStrategy::RawField
            | FieldStrategy::RawFieldSlice
            | FieldStrategy::RawMeta
            | FieldStrategy::RawMetaSlice => {}
        }

        return s;
    }

    fn normal_field(&self, s: &mut String, type_path: &[&str], is_slice: bool, note: &str) {
        use std::fmt::Write;

        s.push_str("/// | ");

        s.push('`');
        s.push_str(self.option_name);
        s.push('`');
        if !note.is_empty() {
            s.push_str(" [^");
            s.push_str(note);
            s.push(']');
        }

        s.push_str(" | `&");

        if is_slice {
            s.push('[');
        }

        if self.value_array_count != 0 {
            s.push('[');
        }

        let type_path_start = if type_path[0] == "core" { 2 } else { 0 };
        s.push_str(type_path[type_path_start]);
        for type_path_part in type_path.iter().skip(type_path_start + 1) {
            s.push_str("::");
            s.push_str(type_path_part);
        }

        if self.value_array_count != 0 {
            write!(s, "; {}]", self.value_array_count).unwrap();
        }

        if is_slice {
            s.push(']');
        }

        s.push_str("` | ");

        push_enum_value(s, "FieldEncoding", encoding_to_string(self.encoding));
        if !matches!(self.format, FieldFormat::Default) {
            s.push_str(" + ");
            push_enum_value(s, "FieldFormat", format_to_string(self.format));
        }
    }
}

struct ParseResult {
    end_pos: usize,
    value: u32,
}

fn push_enum_value(s: &mut String, enum_name: &str, enum_value: &str) {
    s.push_str("[`");
    s.push_str(enum_value);
    s.push_str("`](");
    s.push_str(enum_name);
    s.push_str("::");
    s.push_str(enum_value);
    s.push(')');
}

fn parse_int(str: &[u8], pos: usize) -> ParseResult {
    let mut end_pos = pos + 1;
    let mut value = (str[pos] - b'0') as u32;
    while end_pos != str.len() && str[end_pos].is_ascii_digit() {
        value = value * 10 + (str[end_pos] - b'0') as u32;
        end_pos += 1;
    }

    return ParseResult { end_pos, value };
}

fn encoding_to_string(value: FieldEncoding) -> &'static str {
    return match value {
        FieldEncoding::Invalid => "Invalid",
        FieldEncoding::Struct => "Struct",
        FieldEncoding::Value8 => "Value8",
        FieldEncoding::Value16 => "Value16",
        FieldEncoding::Value32 => "Value32",
        FieldEncoding::Value64 => "Value64",
        FieldEncoding::Value128 => "Value128",
        FieldEncoding::ZStringChar8 => "ZStringChar8",
        FieldEncoding::ZStringChar16 => "ZStringChar16",
        FieldEncoding::ZStringChar32 => "ZStringChar32",
        FieldEncoding::StringLength16Char8 => "StringLength16Char8",
        FieldEncoding::StringLength16Char16 => "StringLength16Char16",
        FieldEncoding::StringLength16Char32 => "StringLength16Char32",
        FieldEncoding::ValueSize => "ValueSize",
    };
}

fn format_to_string(value: FieldFormat) -> &'static str {
    return match value {
        FieldFormat::Default => "Default",
        FieldFormat::UnsignedInt => "UnsignedInt",
        FieldFormat::SignedInt => "SignedInt",
        FieldFormat::HexInt => "HexInt",
        FieldFormat::Errno => "Errno",
        FieldFormat::Pid => "Pid",
        FieldFormat::Time => "Time",
        FieldFormat::Boolean => "Boolean",
        FieldFormat::Float => "Float",
        FieldFormat::HexBytes => "HexBytes",
        FieldFormat::String8 => "String8",
        FieldFormat::StringUtf => "StringUtf",
        FieldFormat::StringUtfBom => "StringUtfBom",
        FieldFormat::StringXml => "StringXml",
        FieldFormat::StringJson => "StringJson",
        FieldFormat::Uuid => "Uuid",
        FieldFormat::Port => "Port",
        FieldFormat::IPv4 => "IPv4",
        FieldFormat::IPv6 => "IPv6",
    };
}
