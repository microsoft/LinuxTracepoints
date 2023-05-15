// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#![allow(non_upper_case_globals)]

#[derive(Clone, Copy)]
pub enum EnumToken {
    U8(u8),
    Str(&'static str),
}

#[derive(Clone, Copy)]
pub enum FieldEncoding {
    Invalid,
    Struct,
    Value8,
    Value16,
    Value32,
    Value64,
    Value128,
    ZStringChar8,
    ZStringChar16,
    ZStringChar32,
    StringLength16Char8,
    StringLength16Char16,
    StringLength16Char32,

    // The following type needs to be expressed symbolically in the generated code, i.e.
    // we have to generate "ValueSize.to_int()" instead of literal "4" or "5".
    ValueSize,
}

impl FieldEncoding {
    pub const VArrayFlag: u8 = 0x40;

    pub const fn to_token(self) -> EnumToken {
        match self {
            FieldEncoding::ValueSize => EnumToken::Str("ValueSize"),
            other => EnumToken::U8(other as u8),
        }
    }
}

#[derive(Clone, Copy)]
pub enum FieldFormat {
    Default,
    #[allow(dead_code)]
    UnsignedInt,
    SignedInt,
    HexInt,
    Errno,
    Pid,
    Time,
    Boolean,
    Float,
    HexBytes,
    String8,
    StringUtf,
    #[allow(dead_code)]
    StringUtfBom,
    StringXml,
    StringJson,
    Uuid,
    Port,
    IPv4,
    IPv6,
}

impl FieldFormat {
    pub const ValueMask: u8 = 0x7F;
}
