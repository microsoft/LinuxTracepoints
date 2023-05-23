// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use crate::enums::{FieldEncoding, FieldFormat};

#[derive(Clone, Copy)]
pub enum FieldStrategy {
    /// meta = scalar; data = from_value
    Scalar,
    /// meta = scalar; data = from_value(time_from_duration_***_1970)
    SystemTime,
    /// meta = scalar; data = from_cstr + nul
    CStr,
    /// meta = scalar; data = slice_count + from_slice
    Str,
    /// meta = array; data = slice_count + from_slice, adds bit to intype.
    Slice,
    /// meta = scalar; data = none
    Struct,
    /// meta = scalar; data = none
    RawStruct,
    /// meta = array; data = none
    RawStructSlice,
    /// meta = none; data = from_slice
    RawData,
    /// meta = scalar; data = from_slice
    RawField,
    /// meta = array; data = from_slice
    RawFieldSlice,
    /// meta = scalar; data = none
    RawMeta,
    /// meta = array; data = none
    RawMetaSlice,
}

impl FieldStrategy {
    pub const fn is_slice(self) -> bool {
        match self {
            FieldStrategy::Scalar
            | FieldStrategy::SystemTime
            | FieldStrategy::CStr
            | FieldStrategy::Str
            | FieldStrategy::Struct
            | FieldStrategy::RawStruct
            | FieldStrategy::RawData
            | FieldStrategy::RawField
            | FieldStrategy::RawMeta => false,

            FieldStrategy::Slice
            | FieldStrategy::RawStructSlice
            | FieldStrategy::RawFieldSlice
            | FieldStrategy::RawMetaSlice => true,
        }
    }

    pub const fn has_metadata(self) -> bool {
        return !matches!(self, FieldStrategy::RawData);
    }

    pub const fn data_count(self) -> u8 {
        match self {
            FieldStrategy::Struct
            | FieldStrategy::RawStruct
            | FieldStrategy::RawStructSlice
            | FieldStrategy::RawMeta
            | FieldStrategy::RawMetaSlice => 0,

            FieldStrategy::Scalar
            | FieldStrategy::SystemTime
            | FieldStrategy::RawData
            | FieldStrategy::RawField
            | FieldStrategy::RawFieldSlice => 1,

            | FieldStrategy::CStr       // 1 for data, 1 for nul termination.
            | FieldStrategy::Str        // 1 for size, 1 for data.
            | FieldStrategy::Slice => 2,// 1 for size, 1 for data.
        }
    }
}

#[derive(Clone, Copy)]
pub struct FieldOption {
    pub option_name: &'static str,
    pub value_type: &'static [&'static str],
    pub encoding: FieldEncoding,
    pub format: FieldFormat,
    pub strategy: FieldStrategy,

    /// If a single value is an array of N elements, value_array_count = N.
    /// Otherwise, value_array_count = 0.
    ///
    /// This is 4 for IPv4, 16 for IPv6, and 0 for everything else.
    pub value_array_count: u8,
}

impl FieldOption {
    pub const fn new(
        option_name: &'static str,
        value_type: &'static [&'static str],
        encoding: FieldEncoding,
        format: FieldFormat,
        strategy: FieldStrategy,
        value_array_count: u8,
    ) -> Self {
        Self {
            option_name,
            strategy,
            value_type,
            encoding,
            format,
            value_array_count,
        }
    }
}
