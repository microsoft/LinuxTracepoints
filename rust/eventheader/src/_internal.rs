// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#![doc(hidden)]
//! Internal implementation details for eventheader macros and eventheader_dynamic.
//! Contents subject to change without notice.

use core::time::Duration;

pub use crate::descriptors::slice_count;
pub use crate::descriptors::EventDataDescriptor;
pub use crate::descriptors::EventHeader;
pub use crate::descriptors::EventHeaderExtension;
pub use crate::enums::HeaderFlags;
pub use crate::native::TracepointState;
pub use crate::provider::provider_new;
pub use crate::provider::CommandString;
pub use crate::provider::EventHeaderTracepoint;

/// Type string for use in the DIAG_IOCSREG command string.
pub const EVENTHEADER_COMMAND_TYPES: &str =
    "u8 eventheader_flags;u8 version;u16 id;u16 tag;u8 opcode;u8 level";

/// Maximum length of a Tracepoint name "ProviderName_Attributes\0" (includes nul).
pub const EVENTHEADER_NAME_MAX: usize = 256;

/// Maximum length needed for a DIAG_IOCSREG command "ProviderName_Attributes CommandTypes\0".
pub const EVENTHEADER_COMMAND_MAX: usize =
    EVENTHEADER_NAME_MAX + 1 + EVENTHEADER_COMMAND_TYPES.len();

/// First byte of tag.
pub const fn tag_byte0(tag: u16) -> u8 {
    return tag.to_ne_bytes()[0];
}

/// Second byte of tag.
pub const fn tag_byte1(tag: u16) -> u8 {
    return tag.to_ne_bytes()[1];
}

/// Returns the time_t corresponding to a duration returned by a successful call to
/// `systemtime.duration_since(SystemTime::UNIX_EPOCH)`.
/// ```
/// # use eventheader::_internal as ehi;
/// # use std::time::SystemTime;
/// let systemtime = SystemTime::now();
/// let time_t = match systemtime.duration_since(SystemTime::UNIX_EPOCH) {
///     Ok(dur) => ehi::time_from_duration_after_1970(dur),
///     Err(err) => ehi::time_from_duration_before_1970(err.duration()),
/// };
/// ```
pub const fn time_from_duration_after_1970(duration: Duration) -> i64 {
    const I64_MAX: u64 = i64::MAX as u64;
    let duration_secs = duration.as_secs();
    if duration_secs > I64_MAX {
        i64::MAX
    } else {
        duration_secs as i64
    }
}

/// Returns the time_t corresponding to a duration returned by a failed call to
/// `systemtime.duration_since(SystemTime::UNIX_EPOCH)`.
/// ```
/// # use eventheader::_internal as ehi;
/// # use std::time::SystemTime;
/// let systemtime = SystemTime::now();
/// let filetime = match systemtime.duration_since(SystemTime::UNIX_EPOCH) {
///     Ok(dur) => ehi::time_from_duration_after_1970(dur),
///     Err(err) => ehi::time_from_duration_before_1970(err.duration()),
/// };
/// ```
pub const fn time_from_duration_before_1970(duration: Duration) -> i64 {
    const I64_MAX: u64 = i64::MAX as u64;
    let duration_secs = duration.as_secs();
    if duration_secs > I64_MAX {
        i64::MIN
    } else {
        // Note: Rounding towards negative infinity.
        -(duration_secs as i64) - ((duration.subsec_nanos() != 0) as i64)
    }
}
