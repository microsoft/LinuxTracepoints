// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use core::ffi;
use core::mem::size_of;
use core::pin::Pin;
use core::ptr;
use core::sync::atomic::AtomicI32;
use core::sync::atomic::AtomicU32;
use core::sync::atomic::Ordering;

use crate::descriptors::EventDataDescriptor;
use crate::descriptors::EventHeader;
use crate::descriptors::EventHeaderExtension;
use crate::enums::ExtensionKind;
use crate::enums::HeaderFlags;

#[cfg(all(target_os = "linux", feature = "user_events"))]
use libc as linux;

// Note: this is intentionally leaked.
static USER_EVENTS_DATA_FILE: UserEventsDataFile = UserEventsDataFile::new();

/// Requires: an errno-setting operation has failed.
///
/// Returns the current value of `linux::errno`.
/// Debug-asserts that `errno > 0`.
#[cfg(all(target_os = "linux", feature = "user_events"))]
fn get_failure_errno() -> i32 {
    let errno = unsafe { *linux::__errno_location() };
    debug_assert!(errno > 0); // Shouldn't call this unless an errno-based operation failed.
    return errno;
}

/// Sets `linux::errno` to 0.
#[cfg(all(target_os = "linux", feature = "user_events"))]
fn clear_errno() {
    unsafe { *linux::__errno_location() = 0 };
}

/// Copies the specified value to the specified location.
/// Returns the pointer after the end of the copy.
///
/// # Safety
///
/// Caller is responsible for making sure there is sufficient space in the buffer.
unsafe fn append_bytes<T: Sized>(dst: *mut u8, src: &T) -> *mut u8 {
    let size = size_of::<T>();
    ptr::copy_nonoverlapping(src as *const T as *const u8, dst, size);
    return dst.add(size);
}

struct UserEventsDataFile {
    /// Initial value is -EAGAIN.
    /// Negative value is -errno with the error code from failed open.
    /// Non-negative value is file descriptor for the "user_events_data" file.
    file_or_error: AtomicI32,
}

impl UserEventsDataFile {
    const EAGAIN_ERROR: i32 = -11;

    #[cfg(all(target_os = "linux", feature = "user_events"))]
    const fn is_space_char(ch: u8) -> bool {
        return ch == b' ' || ch == b'\t';
    }

    #[cfg(all(target_os = "linux", feature = "user_events"))]
    const fn is_nonspace_char(ch: u8) -> bool {
        return ch != b'\0' && !Self::is_space_char(ch);
    }

    /// Opens a file descriptor to the `user_events_data` file.
    /// Atomically updates `self.file_or_error` to either a negative
    /// value (-errno returned from `linux::open`) or a non-negative value
    /// (the file descriptor). If `self.file_or_error` already contains a
    /// non-negative value, the existing value is retained and the new
    /// descriptor is closed. In all cases, returns the final value of
    /// `self.file_or_error`.
    fn update(&self) -> i32 {
        let new_file_or_error;

        #[cfg(not(all(target_os = "linux", feature = "user_events")))]
        {
            new_file_or_error = 0;
        }
        #[cfg(all(target_os = "linux", feature = "user_events"))]
        {
            // Need to find the ".../tracing/user_events_data" file in tracefs or debugfs.

            // Determine tracefs/debugfs mount point by parsing "/proc/mounts".
            clear_errno();
            let mounts_file = unsafe {
                linux::fopen(
                    "/proc/mounts\0".as_ptr().cast::<i8>(),
                    "r\0".as_ptr().cast::<i8>(),
                )
            };
            if mounts_file.is_null() {
                new_file_or_error = -get_failure_errno();
            } else {
                let mut line = [0u8; 4097];
                loop {
                    let fgets_result = unsafe {
                        linux::fgets(
                            line.as_mut_ptr().cast::<i8>(),
                            line.len() as i32,
                            mounts_file,
                        )
                    };
                    if fgets_result.is_null() {
                        new_file_or_error = -linux::ENOTSUP;
                        break;
                    }

                    // line is "device_name mount_point file_system other_stuff..."

                    let mut line_pos = 0;

                    // device_name
                    while Self::is_nonspace_char(line[line_pos]) {
                        line_pos += 1;
                    }

                    // whitespace
                    while Self::is_space_char(line[line_pos]) {
                        line_pos += 1;
                    }

                    // mount_point
                    let mount_begin = line_pos;
                    while Self::is_nonspace_char(line[line_pos]) {
                        line_pos += 1;
                    }

                    let mount_end = line_pos;

                    // whitespace
                    while Self::is_space_char(line[line_pos]) {
                        line_pos += 1;
                    }

                    // file_system
                    let fs_begin = line_pos;
                    while Self::is_nonspace_char(line[line_pos]) {
                        line_pos += 1;
                    }

                    let fs_end = line_pos;

                    if !Self::is_space_char(line[line_pos]) {
                        // Ignore line if no whitespace after file_system.
                        continue;
                    }

                    let path_suffix: &[u8]; // Includes NUL
                    let fs = &line[fs_begin..fs_end];
                    if fs == b"tracefs" {
                        // "tracefsMountPoint/user_events_data"
                        path_suffix = b"/user_events_data\0";
                    } else if fs == b"debugfs" {
                        // "debugfsMountPoint/tracing/user_events_data"
                        path_suffix = b"/tracing/user_events_data\0";
                    } else {
                        continue;
                    }

                    let mount_len = mount_end - mount_begin;
                    let path_len = mount_len + path_suffix.len(); // Includes NUL
                    if path_len > line.len() {
                        continue;
                    }

                    // path = mountpoint + suffix
                    line.copy_within(mount_begin..mount_end, 0);
                    line[mount_len..path_len].copy_from_slice(path_suffix); // Includes NUL

                    // line is now something like "/sys/kernel/tracing/user_events_data\0" or
                    // "/sys/kernel/debug/tracing/user_events_data\0".
                    clear_errno();
                    let new_file =
                        unsafe { linux::open(line.as_ptr().cast::<i8>(), linux::O_RDWR) };
                    if 0 > new_file {
                        new_file_or_error = -get_failure_errno();
                    } else {
                        new_file_or_error = new_file;
                    }
                    break;
                }

                unsafe { linux::fclose(mounts_file) };
            }
        }

        let mut old_file_or_error = Self::EAGAIN_ERROR;
        loop {
            match self.file_or_error.compare_exchange(
                old_file_or_error,
                new_file_or_error,
                Ordering::Relaxed,
                Ordering::Relaxed,
            ) {
                Ok(_) => {
                    // We updated FILE_OR_ERROR to new.
                    return new_file_or_error;
                }
                Err(current_file_or_error) => {
                    // Somebody else updated FILE_OR_ERROR to current.
                    if current_file_or_error >= 0 || new_file_or_error < 0 {
                        // prefer current.
                        #[cfg(all(target_os = "linux", feature = "user_events"))]
                        if new_file_or_error >= 0 {
                            unsafe { linux::close(new_file_or_error) };
                        }
                        return current_file_or_error;
                    }

                    // current is an error, new is a file, try again.
                    old_file_or_error = current_file_or_error;
                }
            }
        }
    }

    // Initial state is -EAGAIN.
    pub const fn new() -> Self {
        return Self {
            file_or_error: AtomicI32::new(Self::EAGAIN_ERROR),
        };
    }

    // If file is open, closes it. Sets state to -EAGAIN.
    pub fn close(&self) {
        let file_or_error = self
            .file_or_error
            .swap(Self::EAGAIN_ERROR, Ordering::Relaxed);
        if file_or_error >= 0 {
            #[cfg(all(target_os = "linux", feature = "user_events"))]
            unsafe {
                linux::close(file_or_error)
            };
        }
    }

    // Returns existing state. This will be non-negative user_events_data file
    // descriptor or -errno if file is not currently open.
    #[cfg(all(target_os = "linux", feature = "user_events"))]
    pub fn peek(&self) -> i32 {
        return self.file_or_error.load(Ordering::Relaxed);
    }

    // If we have not already tried to open the `user_events_data` file, try
    // to open it, atomically update state, and return the new state. Otherwise,
    // return the existing state. Returns non-negative user_events_data file
    // descriptor on success or -errno for error.
    #[inline]
    pub fn get(&self) -> i32 {
        let file_or_error = self.file_or_error.load(Ordering::Relaxed);
        return if file_or_error == Self::EAGAIN_ERROR {
            self.update()
        } else {
            file_or_error
        };
    }
}

impl Drop for UserEventsDataFile {
    fn drop(&mut self) {
        self.close();
    }
}

/// Represents a tracepoint registration.
pub struct TracepointState {
    /// The kernel will update this variable with tracepoint state.
    /// It will be 0 if tracepoint is disabled, nonzero if tracepoint is enabled.
    enable_status: AtomicU32,

    /// This will be a kernel-assigned value if registered,
    /// `UNREGISTERED_WRITE_INDEX` or `BUSY_WRITE_INDEX` if not registered.
    write_index: AtomicU32,
}

impl TracepointState {
    const UNREGISTERED_WRITE_INDEX: u32 = u32::MAX;
    const BUSY_WRITE_INDEX: u32 = u32::MAX - 1;
    const HIGHEST_VALID_WRITE_INDEX: u32 = u32::MAX - 2;

    #[cfg(all(target_os = "linux", feature = "user_events"))]
    const IOC_WRITE: ffi::c_ulong = 1;

    #[cfg(all(target_os = "linux", feature = "user_events"))]
    const IOC_READ: ffi::c_ulong = 2;

    #[cfg(all(target_os = "linux", feature = "user_events"))]
    const DIAG_IOC_MAGIC: ffi::c_ulong = '*' as ffi::c_ulong;

    #[cfg(all(target_os = "linux", feature = "user_events"))]
    const DIAG_IOCSREG: ffi::c_ulong =
        Self::ioc(Self::IOC_WRITE | Self::IOC_READ, Self::DIAG_IOC_MAGIC, 0);

    #[cfg(all(target_os = "linux", feature = "user_events"))]
    const DIAG_IOCSUNREG: ffi::c_ulong = Self::ioc(Self::IOC_WRITE, Self::DIAG_IOC_MAGIC, 2);

    #[cfg(all(target_os = "linux", feature = "user_events"))]
    const fn ioc(dir: ffi::c_ulong, typ: ffi::c_ulong, nr: ffi::c_ulong) -> ffi::c_ulong {
        const IOC_NRBITS: u8 = 8;
        const IOC_TYPEBITS: u8 = 8;
        const IOC_SIZEBITS: u8 = 14;
        const IOC_NRSHIFT: u8 = 0;
        const IOC_TYPESHIFT: u8 = IOC_NRSHIFT + IOC_NRBITS;
        const IOC_SIZESHIFT: u8 = IOC_TYPESHIFT + IOC_TYPEBITS;
        const IOC_DIRSHIFT: u8 = IOC_SIZESHIFT + IOC_SIZEBITS;

        return (dir << IOC_DIRSHIFT)
            | (typ << IOC_TYPESHIFT)
            | (nr << IOC_NRSHIFT)
            | ((size_of::<usize>() as ffi::c_ulong) << IOC_SIZESHIFT);
    }

    /// Creates a new unregistered tracepoint.
    pub const fn new(initial_enable_status: u32) -> Self {
        return Self {
            enable_status: AtomicU32::new(initial_enable_status),
            write_index: AtomicU32::new(Self::UNREGISTERED_WRITE_INDEX),
        };
    }

    /// Returns true if this tracepoint is registered and enabled.
    #[inline(always)]
    pub fn enabled(&self) -> bool {
        return 0 != self.enable_status.load(Ordering::Relaxed);
    }

    /// Unregisters this tracepoint.
    pub fn unregister(&self) -> i32 {
        let error;

        let old_write_index = self
            .write_index
            .swap(Self::BUSY_WRITE_INDEX, Ordering::Relaxed);
        match old_write_index {
            Self::BUSY_WRITE_INDEX => {
                error = 16; // EBUSY: Another thread is registering/unregistering. Do nothing.
                return error; // Return immediately, need to leave write_index = BUSY.
            }
            Self::UNREGISTERED_WRITE_INDEX => {
                error = 116; // EALREADY: Already unregistered. No action needed.
            }
            _ => {
                #[cfg(not(all(target_os = "linux", feature = "user_events")))]
                {
                    error = 0;
                }

                #[cfg(all(target_os = "linux", feature = "user_events"))]
                {
                    #[repr(C, packed)]
                    struct user_unreg {
                        size: u32,
                        disable_bit: u8,
                        reserved1: u8,
                        reserved2: u16,
                        disable_addr: u64,
                    }

                    let unreg = user_unreg {
                        size: size_of::<user_unreg>() as u32,
                        disable_bit: 0,
                        reserved1: 0,
                        reserved2: 0,
                        disable_addr: &self.enable_status as *const AtomicU32 as usize as u64,
                    };

                    clear_errno();
                    let ioctl_result = unsafe {
                        linux::ioctl(USER_EVENTS_DATA_FILE.peek(), Self::DIAG_IOCSUNREG, &unreg)
                    };
                    if 0 > ioctl_result {
                        error = get_failure_errno();
                    } else {
                        error = 0;
                    }
                }
            }
        }

        let old_write_index = self
            .write_index
            .swap(Self::UNREGISTERED_WRITE_INDEX, Ordering::Relaxed);
        debug_assert!(old_write_index == Self::BUSY_WRITE_INDEX);

        return error;
    }

    /// Registers this tracepoint.
    ///
    /// Requires: this tracepoint is not currently registered.
    ///
    /// # Safety
    ///
    /// The tracepoint must be unregistered before it is deallocated. Note that it will
    /// unregister itself when dropped, so this is only an issue if the tracepoint is
    /// not dropped before it is deallocated, as might happen for a static variable in a
    /// shared library that gets unloaded.
    pub unsafe fn register(self: Pin<&Self>, _name_args: &ffi::CStr) -> i32 {
        let error;
        let new_write_index;

        let old_write_index = self
            .write_index
            .swap(Self::BUSY_WRITE_INDEX, Ordering::Relaxed);
        assert!(
            old_write_index == Self::UNREGISTERED_WRITE_INDEX,
            "register of active tracepoint (already-registered or being-unregistered)"
        );

        let user_events_data = USER_EVENTS_DATA_FILE.get();
        if user_events_data < 0 {
            error = -user_events_data;
            new_write_index = Self::UNREGISTERED_WRITE_INDEX;
        } else {
            #[cfg(not(all(target_os = "linux", feature = "user_events")))]
            {
                error = 0;
                new_write_index = 0;
            }

            #[cfg(all(target_os = "linux", feature = "user_events"))]
            {
                #[repr(C, packed)]
                struct user_reg {
                    size: u32,
                    enable_bit: u8,
                    enable_size: u8,
                    flags: u16,
                    enable_addr: u64,
                    name_args: u64,
                    write_index: u32,
                }

                let mut reg = user_reg {
                    size: size_of::<user_reg>() as u32,
                    enable_bit: 0,
                    enable_size: 4,
                    flags: 0,
                    enable_addr: &self.enable_status as *const AtomicU32 as usize as u64,
                    name_args: _name_args.as_ptr() as usize as u64,
                    write_index: 0,
                };

                clear_errno();
                let ioctl_result =
                    unsafe { linux::ioctl(user_events_data, Self::DIAG_IOCSREG, &mut reg) };
                if 0 > ioctl_result {
                    error = get_failure_errno();
                    new_write_index = Self::UNREGISTERED_WRITE_INDEX;
                } else {
                    error = 0;
                    new_write_index = reg.write_index;
                    debug_assert!(new_write_index <= Self::HIGHEST_VALID_WRITE_INDEX);
                }
            }
        }

        let old_write_index = self.write_index.swap(new_write_index, Ordering::Relaxed);
        debug_assert!(old_write_index == Self::BUSY_WRITE_INDEX);

        return error;
    }

    /// Fills in `data[0]` with the event's write_index, then sends to event to the
    /// `user_events_data` file.
    ///
    /// Requires: `data[0].is_empty()` since it will be used for the headers.
    pub fn write(&self, data: &mut [EventDataDescriptor]) -> i32 {
        debug_assert!(data[0].is_empty());

        let enable_status = self.enable_status.load(Ordering::Relaxed);
        let write_index = self.write_index.load(Ordering::Relaxed);
        if enable_status == 0 || write_index > Self::HIGHEST_VALID_WRITE_INDEX {
            return 0;
        }

        let writev_result = self.writev(data, &write_index.to_ne_bytes());
        return writev_result;
    }

    /// Fills in `data[0]` with the event's write_index, event_header,
    /// activity extension block (if an activity id is provided), and
    /// metadata extension block header (if meta_len != 0), then sends
    /// the event to the `user_events_data` file.
    ///
    /// Requires:
    /// - `data[0].is_empty()` since it will be used for the headers.
    /// - related_id may only be present if activity_id is present.
    /// - if activity_id.is_some() || meta_len != 0 then event_header.flags
    ///   must equal DefaultWithExtension.
    /// - If meta_len != 0 then `data[1]` starts with metadata extension
    ///   block data.
    pub fn write_eventheader(
        &self,
        event_header: &EventHeader,
        activity_id: Option<&[u8; 16]>,
        related_id: Option<&[u8; 16]>,
        meta_len: u16,
        data: &mut [EventDataDescriptor],
    ) -> i32 {
        debug_assert!(data[0].is_empty());
        debug_assert!(related_id.is_none() || activity_id.is_some());

        if activity_id.is_some() || meta_len != 0 {
            debug_assert!(event_header.flags == HeaderFlags::DefaultWithExtension);
        }

        let enable_status = self.enable_status.load(Ordering::Relaxed);
        let write_index = self.write_index.load(Ordering::Relaxed);
        if enable_status == 0 || write_index > Self::HIGHEST_VALID_WRITE_INDEX {
            return 0;
        }

        let mut extension_count = (activity_id.is_some() as u8) + ((meta_len != 0) as u8);

        const HEADERS_SIZE_MAX: usize = size_of::<u32>() // write_index
            + size_of::<EventHeader>() // event_header
            + size_of::<EventHeaderExtension>() + 16 + 16 // activity header + activity_id + related_id
            + size_of::<EventHeaderExtension>(); // metadata header (last because data[1] has the metadata)
        let mut headers: [u8; HEADERS_SIZE_MAX] = [0; HEADERS_SIZE_MAX];
        let headers_len;
        unsafe {
            let mut headers_ptr = headers.as_mut_ptr();
            headers_ptr = append_bytes(headers_ptr, &write_index);
            headers_ptr = append_bytes(headers_ptr, event_header);

            match activity_id {
                None => debug_assert!(related_id.is_none()),
                Some(aid) => match related_id {
                    None => {
                        extension_count -= 1;
                        headers_ptr = append_bytes(
                            headers_ptr,
                            &EventHeaderExtension::from_parts(
                                16,
                                ExtensionKind::ActivityId,
                                extension_count > 0,
                            ),
                        );
                        headers_ptr = append_bytes(headers_ptr, aid);
                    }
                    Some(rid) => {
                        extension_count -= 1;
                        headers_ptr = append_bytes(
                            headers_ptr,
                            &EventHeaderExtension::from_parts(
                                32,
                                ExtensionKind::ActivityId,
                                extension_count > 0,
                            ),
                        );
                        headers_ptr = append_bytes(headers_ptr, aid);
                        headers_ptr = append_bytes(headers_ptr, rid);
                    }
                },
            }

            if meta_len != 0 {
                extension_count -= 1;
                headers_ptr = append_bytes(
                    headers_ptr,
                    &EventHeaderExtension::from_parts(
                        meta_len,
                        ExtensionKind::Metadata,
                        extension_count > 0,
                    ),
                );
            }

            headers_len = headers_ptr.offset_from(headers.as_mut_ptr()) as usize;
        }

        debug_assert!(headers_len <= headers.len());
        debug_assert!(extension_count == 0);

        let writev_result = self.writev(data, &headers[0..headers_len]);
        return writev_result;
    }

    // Returns 0 for success, errno for error.
    fn writev(&self, _data: &mut [EventDataDescriptor], _headers: &[u8]) -> i32 {
        #[cfg(all(target_os = "linux", feature = "user_events"))]
        unsafe {
            // Unsafe: Putting headers into a container a with longer lifetime.
            _data[0] =
                EventDataDescriptor::from_raw_ptr(_headers.as_ptr() as usize, _headers.len());

            let writev_result = linux::writev(
                USER_EVENTS_DATA_FILE.peek(),
                _data.as_ptr() as *const linux::iovec,
                _data.len() as i32,
            );

            // Clear the container before headers lifetime ends.
            _data[0] = EventDataDescriptor::zero();

            if 0 > writev_result {
                return get_failure_errno();
            }
        }

        return 0;
    }
}

impl Drop for TracepointState {
    fn drop(&mut self) {
        self.unregister();
    }
}

/// Possible configurations under which this crate can be compiled: `LinuxUserEvents` or
/// `Other`.
pub enum NativeImplementation {
    /// Crate compiled for other configuration (no logging is performed).
    Other,

    /// Crate compiled for Linux user_events configuration (logging is performed via
    /// `user_events_data` file).
    LinuxUserEvents,
}

/// The configuration under which this crate was compiled: `LinuxUserEvents` or `Other`.
pub const NATIVE_IMPLEMENTATION: NativeImplementation =
    if cfg!(all(target_os = "linux", feature = "user_events")) {
        NativeImplementation::LinuxUserEvents
    } else {
        NativeImplementation::Other
    };
