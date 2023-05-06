// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use alloc::boxed::Box;
use alloc::collections::BTreeSet;
use alloc::sync::Arc;
use alloc::vec::Vec;

use core::borrow;
use core::cmp;
use core::fmt;
use core::hash;
use core::pin::Pin;
use core::str;

use eventheader::Level;
use eventheader::_internal::*;

#[allow(unused_imports)] // For docs
use crate::EventBuilder;

const fn is_option_val_char(ch: u8) -> bool {
    return (b'0' <= ch && ch <= b'9') || (b'a' <= ch && ch <= b'z');
}

/// Represents a connection for writing dynamic Linux tracepoints.
pub struct Provider {
    name: Box<[u8]>,
    options: Box<[u8]>,
    sets: BTreeSet<Pin<Arc<EventSet>>>,
}

impl Provider {
    /// Returns a default ProviderOptions.
    pub fn new_options<'a>() -> ProviderOptions<'a> {
        return ProviderOptions { group_name: "" };
    }

    /// Creates a new provider.
    ///
    /// - `name` is the name to use for this provider. It must be less than 234 chars
    ///   and must not contain `'\0'`, `' '`, or `':'`. For best compatibility with
    ///   trace processing components, it should contain only ASCII letters, digits,
    ///   and `'_'`. It should be short, human-readable, and unique enough to not
    ///   conflict with names of other providers. The provider name will typically
    ///   include a company name and a component name, e.g. `"MyCompany_MyComponent"`.
    ///
    /// - `options` can usually be `&Provider::new_options()`. If the provider needs to
    ///   join a provider group, use `Provider::new_options().group_name(provider_group_name)`.
    ///   Note that the total length of the provider name + the group name must be less than
    ///   234.
    ///
    /// Use `register_set()` to register event sets.
    ///
    /// Use [EventBuilder] to create events, then write them to a registered event set.
    pub fn new(name: &str, options: &ProviderOptions) -> Self {
        const NAMES_MAX: usize = EVENTHEADER_NAME_MAX - "_LffKffffffffffffffffG".len();
        assert!(
            name.len() + options.group_name.len() < NAMES_MAX,
            "provider name.len() + group_name.len() must be less than 234"
        );
        debug_assert!(!name.contains('\0'), "provider name must not contain '\\0'");
        debug_assert!(!name.contains(' '), "provider name must not contain ' '");
        debug_assert!(!name.contains(':'), "provider name must not contain ':'");

        let group_name_bytes = options.group_name.as_bytes();
        let options_box = if group_name_bytes.is_empty() {
            <Box<[u8]>>::default()
        } else {
            let mut options_vec = Vec::<u8>::with_capacity(1 + group_name_bytes.len());
            options_vec.push(b'G');
            options_vec.extend_from_slice(group_name_bytes);
            options_vec.into()
        };

        return Self {
            name: name.as_bytes().into(),
            options: options_box,
            sets: BTreeSet::new(),
        };
    }

    /// Returns this provider's name.
    pub fn name(&self) -> &str {
        return str::from_utf8(&self.name).unwrap();
    }

    /// Returns this provider's options, e.g. "" or "Gmygroup".
    pub fn options(&self) -> &str {
        return str::from_utf8(&self.options).unwrap();
    }

    /// If this provider is not registered, does nothing and returns 0.
    /// Otherwise, unregisters all event sets that were registered by this provider
    /// and clears the list of already-created event sets.
    ///
    /// Use `provider.unregister()` if you want to unregister the provider before it goes
    /// out of scope. The provider automatically unregisters when it is dropped so most
    /// users do  not need to call `unregister` directly.
    pub fn unregister(&mut self) {
        for set in &self.sets {
            set.state.unregister();
        }
        self.sets.clear();
    }

    /// If an event set with the specified level and keyword is in the list of
    /// already-created sets, returns it. Otherwise, returns `None`.
    pub fn find_set(&self, level: Level, keyword: u64) -> Option<Arc<EventSet>> {
        let opt_val = self.sets.get(&EventSetKey { keyword, level });
        match opt_val {
            None => return None,
            Some(val) => {
                let val_pin_arc = val.clone();

                // Safety:
                // - unsafe because we're turning the pinned Arc into an unpinned Arc, and
                //   it is possible to move-from an unpinned Arc via Arc::get_mut().
                // - Ok here because Arc::get_mut() only works when the Arc's refcount is
                //   1, and we will be holding our pinned Arc for as long as the set is
                //   registered, so the caller will always see a refcount of 2 or more and
                //   therefore will not be able to use Arc::get_mut().
                let val_arc = unsafe { Pin::into_inner_unchecked(val_pin_arc) };
                return Some(val_arc);
            }
        }
    }

    /// If an event set with the specified level and keyword is in the list of
    /// already-created sets, returns it. Otherwise, creates a new event set, adds it to
    /// the list of already-created sets, attempts to register it, and returns the new
    /// event set. If registration fails, the new event set will have a non-zero errno
    /// and will never be enabled.
    pub fn register_set(&mut self, level: Level, keyword: u64) -> Arc<EventSet> {
        let set_pin_arc = match self.sets.get(&EventSetKey { keyword, level }) {
            Some(set_pin_arc) => set_pin_arc.clone(),
            None => {
                let mut set_arc = Arc::new(EventSet::new(0, level, keyword));
                let set_mut = Arc::get_mut(&mut set_arc).unwrap();

                // Safety:
                // - unsafe because we must guarantee that nobody ever moves-from
                //   or deallocates set_mut.state while it is registered.
                // - We will guarantee this by pinning set and storing it in an Arc.
                //   set is not pinned yet but will be soon, and we don't move-from it
                //   in the meantime.
                // - set will then remain pinned and referenced until self.unregister(),
                //   which will invoke state.unregister() for all sets, and only then will
                //   we release the Arc, so it cannot be deallocated/moved before then.
                let state_pin = unsafe { Pin::new_unchecked(&set_mut.state) };

                // Command = "ProviderName_LxKxOptions CommandTypes\0"
                let mut command_string = CommandString::new();
                let name_args = command_string.format(&self.name, &self.options, level, keyword);

                // Safety:
                // - unsafe because we must guarantee that state gets unregistered
                //   before it moves or is deallocated.
                // - state will get unregistered at self.unregister().
                // - state cannot be deallocated or moved before it gets unregistered.
                set_mut.errno = unsafe { state_pin.register(name_args) };

                let set_pin_arc = unsafe { Pin::new_unchecked(set_arc) };
                self.sets.insert(set_pin_arc.clone());
                set_pin_arc
            }
        };

        // Safety:
        // - unsafe because we're turning the pinned Arc into an unpinned Arc, and
        //   it is possible to move-from an unpinned Arc via Arc::get_mut().
        // - Ok here because Arc::get_mut() only works when the Arc's refcount is
        //   1, and we will be holding our pinned Arc for as long as the set is
        //   registered, so the caller will always see a refcount of 2 or more and
        //   therefore will not be able to use Arc::get_mut().
        let set_arc = unsafe { Pin::into_inner_unchecked(set_pin_arc) };
        return set_arc;
    }

    /// For testing purposes: Creates an inactive (unregistered) event set.
    ///
    /// If an event set with the specified level and keyword is in the list of
    /// already-created sets, returns it. Otherwise, creates a new **unregistered**
    /// event set, adds it to the list of already-created sets, and returns the new
    /// event set.
    pub fn create_unregistered(
        &mut self,
        enabled: bool,
        level: Level,
        keyword: u64,
    ) -> Arc<EventSet> {
        let set_pin_arc = match self.sets.get(&EventSetKey { keyword, level }) {
            Some(set_pin_arc) => set_pin_arc.clone(),
            None => {
                let set_pin_arc = Arc::pin(EventSet::new(enabled as u32, level, keyword));
                self.sets.insert(set_pin_arc.clone());
                set_pin_arc
            }
        };

        // Safety:
        // - unsafe because we're turning the pinned Arc into an unpinned Arc, and
        //   it is possible to move-from an unpinned Arc via Arc::get_mut().
        // - Ok here because Arc::get_mut() only works when the Arc's refcount is
        //   1, and we will be holding our pinned Arc for as long as the set is
        //   registered, so the caller will always see a refcount of 2 or more and
        //   therefore will not be able to use Arc::get_mut().
        let set_arc = unsafe { Pin::into_inner_unchecked(set_pin_arc) };
        return set_arc;
    }
}

impl Drop for Provider {
    fn drop(&mut self) {
        self.unregister();
    }
}

impl fmt::Debug for Provider {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        return write!(
            f,
            "Provider {{ name: \"{}\", options: \"{}\", sets: {} }}",
            self.name(),
            str::from_utf8(&self.options).unwrap(),
            self.sets.len(),
        );
    }
}

/// Builder for provider configuration. Used when registering a provider.
///
/// In most cases, you'll just use the default options.
#[derive(Clone, Copy, Default)]
pub struct ProviderOptions<'a> {
    group_name: &'a str,
}

impl<'a> ProviderOptions<'a> {
    /// Returns true if the specified string is a valid option value.
    /// A valid option value contains only ASCII digits and lowercase
    /// ASCII letters.
    pub const fn is_valid_option_value(value: &'a str) -> bool {
        let value_bytes = value.as_bytes();
        let mut i = 0;
        return loop {
            if i == value_bytes.len() {
                break true;
            }

            let ch = value_bytes[i];
            i += 1;
            if !is_option_val_char(ch) {
                break false;
            }
        };
    }

    /// Creates default provider options.
    /// - No provider group name.
    pub const fn new() -> Self {
        return Self { group_name: "" };
    }

    /// Sets the name of the provider group that should be set for this provider.
    /// Requires: name is a valid option value (contains only ASCII digits and
    /// lowercase ASCII letters).
    ///
    /// Most providers do not set any provider group so this is usually not called.
    pub fn group_name(&mut self, name: &'a str) -> &mut Self {
        debug_assert!(
            Self::is_valid_option_value(name),
            "group_name must contain only 0..9 and a..z"
        );

        self.group_name = name;
        return self;
    }
}

impl<'a> fmt::Debug for ProviderOptions<'a> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        return write!(
            f,
            "ProviderOptions {{ group_name: \"{}\" }}",
            self.group_name,
        );
    }
}

struct EventSetKey {
    keyword: u64,
    level: Level,
}

impl Eq for EventSetKey {}

impl hash::Hash for EventSetKey {
    fn hash<H: hash::Hasher>(&self, state: &mut H) {
        self.level.hash(state);
        self.keyword.hash(state);
    }
}

impl PartialEq for EventSetKey {
    fn eq(&self, other: &Self) -> bool {
        self.level == other.level && self.keyword == other.keyword
    }
}

impl Ord for EventSetKey {
    fn cmp(&self, other: &Self) -> cmp::Ordering {
        return match self.level.cmp(&other.level) {
            cmp::Ordering::Equal => self.keyword.cmp(&other.keyword),
            level_ordering => level_ordering,
        };
    }
}

impl PartialOrd for EventSetKey {
    fn partial_cmp(&self, other: &Self) -> Option<cmp::Ordering> {
        Some(self.cmp(other))
    }
}

/// Tracks the status of a set of events that have the same provider, level, and keyword.
pub struct EventSet {
    state: TracepointState,
    key: EventSetKey,
    errno: i32,
}

impl EventSet {
    fn new(enable_status: u32, level: Level, keyword: u64) -> EventSet {
        return EventSet {
            state: TracepointState::new(enable_status),
            key: EventSetKey { keyword, level },
            errno: 0,
        };
    }

    /// Returns an inactive (unregistered) EventSet. The returned event set's
    /// `enabled()` method will always return `false`, and any attempt to write
    /// to this event set will have no effect (safe no-op).
    ///
    /// This method may be used to create a placeholder event set. Active
    /// (registered) event sets are created using [`Provider::register_set`].
    pub const fn new_unregistered() -> EventSet {
        return EventSet {
            state: TracepointState::new(0),
            key: EventSetKey {
                keyword: 0,
                level: Level::from_int(0),
            },
            errno: 22, // EINVAL
        };
    }

    pub(crate) fn state(&self) -> &TracepointState {
        return &self.state;
    }

    #[inline(always)]
    pub(crate) fn level(&self) -> Level {
        return self.key.level;
    }

    /// Returns true if any logging session is listening for events with the
    /// provider, level, and keyword associated with this event set.
    #[inline(always)]
    pub fn enabled(&self) -> bool {
        return self.state.enabled();
    }

    /// Returns 0 if this event set was successfully registered, or a nonzero
    /// error code if `open("/sys/kernel/.../tracing/user_events_data")` failed
    /// or `ioctl(user_events_data, DIAG_IOCSREG, ...)` returned an error.
    pub fn errno(&self) -> i32 {
        return self.errno;
    }
}

impl Eq for EventSet {}

impl hash::Hash for EventSet {
    fn hash<H: hash::Hasher>(&self, state: &mut H) {
        self.key.hash(state);
    }
}

impl PartialEq for EventSet {
    fn eq(&self, other: &Self) -> bool {
        self.key.eq(&other.key)
    }
}

impl Ord for EventSet {
    fn cmp(&self, other: &Self) -> cmp::Ordering {
        self.key.cmp(&other.key)
    }
}

impl PartialOrd for EventSet {
    fn partial_cmp(&self, other: &Self) -> Option<cmp::Ordering> {
        Some(self.key.cmp(&other.key))
    }
}

impl borrow::Borrow<EventSetKey> for Pin<Arc<EventSet>> {
    fn borrow(&self) -> &EventSetKey {
        return &self.key;
    }
}

impl fmt::Debug for EventSet {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        return write!(
            f,
            "EventSet {{ enabled: {}, level: {:x}, keyword: {:x}, errno: {} }}",
            self.state.enabled(),
            self.key.level.as_int(),
            self.key.keyword,
            self.errno,
        );
    }
}
