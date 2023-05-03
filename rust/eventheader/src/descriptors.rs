// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use core::marker::PhantomData;
use core::mem::size_of;

use crate::enums::ExtensionKind;
use crate::enums::HeaderFlags;
use crate::enums::Level;
use crate::enums::Opcode;

/// Characteristics of an eventheader event: severity level, id, etc.
///
/// Each EventHeader event starts with an instance of the `EventHeader` structure.
/// It contains core information recorded for every event to help with event
/// identification, filtering, and decoding.
///
/// If eventheader.flags has the [`HeaderFlags::Extension`] bit set then the
/// eventheader is followed by one or more [`EventHeaderExtension`] blocks.
/// Otherwise the eventheader is followed by the event payload data.
///
/// If [`EventHeaderExtension::kind`] has the chain flag set then the
/// EventHeaderExtension block is followed immediately (no alignment/padding) by
/// another extension block. Otherwise it is followed immediately (no
/// alignment/padding) by the event payload data.
///
/// If there is a `Metadata` extension then it contains the event name, field names,
/// and field types needed to decode the payload data. Otherwise, the payload
/// decoding system is defined externally, i.e. you will use the provider name to
/// find the appropriate decoding manifest, then use the event's id+version to
/// find the decoding information within the manifest, then use that decoding
/// information to decode the event payload data.
///
/// For a particular event definition (i.e. for a particular event name, or for a
/// particular nonzero event id+version), the information in the eventheader (and
/// in the `Metadata` extension, if present) should be constant. For example, instead
/// of having a single event with a runtime-variable level, you should have a
/// distinct event definition (with distinct event name and/or distinct event id)
/// for each level.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct EventHeader {
    /// Indicates whether the event uses 32-bit or 64-bit pointers, whether the
    /// event uses little-endian or big-endian byte order, and whether the
    /// event contains any header extension blocks. When generating events,
    /// this should be set to either Default or DefaultWithExtension.
    pub flags: HeaderFlags,

    /// Set version to 0 unless the event has a manually-assigned stable id.
    /// If the event does have a manually-assigned stable id, start the version
    /// at 0, then increment the version for each breaking change to the event
    /// (e.g. for changes to the field names, types, or semantics).
    pub version: u8,

    /// Set id to 0 unless the event has a manually-assigned stable id.
    pub id: u16,

    /// Provider-defined 16-bit value.
    pub tag: u16,

    /// Special semantics for event: 0=informational, 1=activity-start, 2=activity-stop.
    pub opcode: Opcode,

    /// Event severity level: 1=critical, 2=error, 3=warning, 4=info, 5=verbose.
    /// If unsure, use 5 (verbose).
    pub level: Level,
}

impl EventHeader {
    /// Creates a new header for an informational event.
    ///
    /// level: critical, error, warning, info, verbose; if unsure use verbose.
    ///
    /// has_extension: true if the event has one or more header extension blocks.
    pub const fn new(level: Level, has_extension: bool) -> EventHeader {
        return EventHeader {
            flags: if has_extension {
                HeaderFlags::DefaultWithExtension
            } else {
                HeaderFlags::Default
            },
            version: 0,
            id: 0,
            tag: 0,
            opcode: Opcode::Info,
            level,
        };
    }

    /// Creates a new descriptor from values.
    pub const fn from_parts(
        flags: HeaderFlags,
        version: u8,
        id: u16,
        tag: u16,
        opcode: Opcode,
        level: Level,
    ) -> EventHeader {
        return EventHeader {
            flags,
            version,
            id,
            tag,
            opcode,
            level,
        };
    }
}

/// Characteristics of an eventheader extension block.
///
/// Extension block is an EventHeaderExtension followed by `size` bytes of data.
/// Extension block is tightly-packed (no padding bytes, no alignment).
///
/// If [`EventHeader::flags`] has the Extension bit set then the EventHeader is
/// followed by one or more EventHeaderExtension blocks. Otherwise the EventHeader
/// is followed by the event payload data.
///
/// If [`EventHeaderExtension::kind`] has the chain flag set then the
/// EventHeaderExtension block is followed immediately (no alignment/padding) by
/// another extension block. Otherwise it is followed immediately (no
/// alignment/padding) by the event payload data.
#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct EventHeaderExtension {
    /// Size (in bytes) of the data block following this header.
    pub size: u16,

    /// Type of the data block following this header.
    pub kind: ExtensionKind,
}

impl EventHeaderExtension {
    /// Creates a new header for an extension block. Sets size to 0.
    pub fn new(kind: ExtensionKind) -> Self {
        return Self { size: 0, kind };
    }

    /// Creates a new header from values.
    pub fn from_parts(size: u16, kind: ExtensionKind, chain: bool) -> Self {
        return Self {
            size,
            kind: if chain {
                ExtensionKind::from_int(kind.as_int() | ExtensionKind::ChainFlag)
            } else {
                kind
            },
        };
    }
}

/// Describes a block of data to be sent to user_events via writev.
///
/// Note: This must have the same underlying representation as `struct iovec`.
#[repr(C)]
#[derive(Debug, Default)]
pub struct EventDataDescriptor<'a> {
    ptr: usize,
    size: usize,
    lifetime: PhantomData<&'a [u8]>,
}

impl<'a> EventDataDescriptor<'a> {
    /// Returns an EventDataDescriptor initialized with { null, 0 }.
    pub const fn zero() -> Self {
        return Self {
            ptr: 0,
            size: 0,
            lifetime: PhantomData,
        };
    }

    /// Returns true if this descriptor's size is 0.
    pub const fn is_empty(&self) -> bool {
        return self.size == 0;
    }

    /// Returns an EventDataDescriptor initialized with the specified ptr and size.
    ///
    /// # Safety
    ///
    /// This bypasses lifetime tracking. Caller must ensure that this
    /// EventDataDescriptor is not used after the referenced data's lifetime.
    /// Typically, this is done by overwriting the descriptor with
    /// [`EventDataDescriptor::zero`] after it has been used.
    pub const unsafe fn from_raw_ptr(ptr: usize, size: usize) -> Self {
        return Self {
            ptr,
            size,
            lifetime: PhantomData,
        };
    }

    /// Returns an EventDataDescriptor initialized with the specified slice's bytes.
    pub fn from_bytes(value: &'a [u8]) -> Self {
        return Self {
            ptr: value.as_ptr() as usize,
            size: value.len(),
            lifetime: PhantomData,
        };
    }

    /// Returns an EventDataDescriptor initialized with the specified value's bytes.
    pub fn from_value<T: Copy>(value: &'a T) -> Self {
        return Self {
            ptr: value as *const T as usize,
            size: size_of::<T>(),
            lifetime: PhantomData,
        };
    }

    /// Returns an EventDataDescriptor for a nul-terminated string.
    /// Returned descriptor does NOT include the nul-termination.
    ///
    /// Resulting descriptor's size is the minimum of:
    /// - `size_of::<T>() * 65535`
    /// - `size_of::<T>() * value.len()`
    /// - `size_of::<T>() * (index of first element equal to T::default())`
    pub fn from_cstr<T: Copy + Default + Eq>(mut value: &'a [T]) -> Self {
        let mut value_len = value.len();

        const MAX_LEN: usize = 65535;
        if value_len > MAX_LEN {
            value = &value[..MAX_LEN];
            value_len = value.len();
        }

        let zero = T::default();
        let mut len = 0;
        while len < value_len {
            if value[len] == zero {
                value = &value[..len];
                break;
            }

            len += 1;
        }

        return Self {
            ptr: value.as_ptr() as usize,
            size: size_of::<T>() * value.len(),
            lifetime: PhantomData,
        };
    }

    /// Returns an EventDataDescriptor for variable-length array field.
    ///
    /// Resulting descriptor's size is the minimum of:
    /// - `size_of::<T>() * 65535`
    /// - `size_of::<T>() * value.len()`
    pub fn from_slice<T: Copy>(mut value: &'a [T]) -> Self {
        let value_len = value.len();

        const MAX_LEN: usize = 65535;
        if MAX_LEN < value_len {
            value = &value[..MAX_LEN];
        }

        return Self {
            ptr: value.as_ptr() as usize,
            size: size_of::<T>() * value.len(),
            lifetime: PhantomData,
        };
    }
}

/// Returns the count for a variable-length array field.
///
/// Returns the smaller of `value.len()` and `65535`.
pub fn slice_count<T>(value: &[T]) -> u16 {
    let len = value.len();
    return if 65535 < len { 65535 } else { len as u16 };
}
