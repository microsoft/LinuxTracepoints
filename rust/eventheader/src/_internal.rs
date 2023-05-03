// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#![doc(hidden)]
//! Internal implementation details for eventheader macros and eventheader_dynamic.
//! Contents subject to change without notice.

pub use crate::descriptors::slice_count;
pub use crate::descriptors::EventDataDescriptor;
pub use crate::descriptors::EventHeader;
pub use crate::descriptors::EventHeaderExtension;
pub use crate::enums::HeaderFlags;
pub use crate::native::TracepointState;

/// Type string for use in the DIAG_IOCSREG command string.
pub const EVENTHEADER_COMMAND_TYPES: &str =
    "u8 eventheader_flags;u8 version;u16 id;u16 tag;u8 opcode;u8 level";

/// Maximum length of a Tracepoint name "ProviderName_Attributes\0" (includes nul).
pub const EVENTHEADER_NAME_MAX: usize = 256;

/// Maximum length needed for a DIAG_IOCSREG command "ProviderName_Attributes CommandTypes\0".
pub const EVENTHEADER_COMMAND_MAX: usize =
    EVENTHEADER_NAME_MAX + 1 + EVENTHEADER_COMMAND_TYPES.len();
