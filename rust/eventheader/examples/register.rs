// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use eh::_internal as ehi;
use eventheader as eh;

// The following is the planned expansion for:
// define_provider!(MY_PROVIDER, "EhProvider1", group_name("mygroup"))
extern "C" {
    #[link_name = "__start__eh_event_ptrs_MY_PROVIDER"] // linker-generated symbol
    static mut _start__eh_event_ptrs_MY_PROVIDER: ::core::primitive::usize;
    #[link_name = "__stop__eh_event_ptrs_MY_PROVIDER"] // linker-generated symbol
    static mut _stop__eh_event_ptrs_MY_PROVIDER: ::core::primitive::usize;
}
#[link_section = "_eh_event_ptrs_MY_PROVIDER"]
#[no_mangle]
static mut _eh_define_provider_MY_PROVIDER: *const usize = ::core::ptr::null();
static MY_PROVIDER: eh::Provider = unsafe {
    eh::_internal::provider_new(
        b"EhProvider1",
        b"Gmygroup",
        &_start__eh_event_ptrs_MY_PROVIDER as *const usize,
        &_stop__eh_event_ptrs_MY_PROVIDER as *const usize,
    )
};

fn main() {
    let mut err;
    unsafe { MY_PROVIDER.register() };

    err =
    // The following is the planned expansion for:
    // write_event!(MY_PROVIDER, "EventName0")
    {
        static _EH_EVENT: ehi::EventHeaderTracepoint = ehi::EventHeaderTracepoint::new(
            ehi::EventHeader::new(eh::Level::Verbose, true),
            1,
            b"EventName0\0",
        );
        #[link_section = "_eh_event_ptrs_MY_PROVIDER"]
        static mut _EH_EVENT_PTR: *const ehi::EventHeaderTracepoint = &_EH_EVENT;
        if !_EH_EVENT.enabled() {
            0
        } else {
            _EH_EVENT.write_eventheader(
                None,
                None,
                &mut [
                    ehi::EventDataDescriptor::zero(), // headers
                    ehi::EventDataDescriptor::zero(), // metadata
                ],
            )
        }
    };
    println!("err: {}", err);

    err =
    // The following is the planned expansion for:
    // write_event!(MY_PROVIDER, "EventName1", keyword(2))
    {
        static _EH_EVENT: ehi::EventHeaderTracepoint = ehi::EventHeaderTracepoint::new(
            ehi::EventHeader::new(eh::Level::Verbose, true),
            2,
            b"EventName1\0",
        );
        #[link_section = "_eh_event_ptrs_MY_PROVIDER"]
        static mut _EH_EVENT_PTR: *const ehi::EventHeaderTracepoint = &_EH_EVENT;
        if !_EH_EVENT.enabled() {
            0
        } else {
            _EH_EVENT.write_eventheader(
                None,
                None,
                &mut [
                    ehi::EventDataDescriptor::zero(),
                    ehi::EventDataDescriptor::zero(),
                ],
            )
        }
    };
    println!("err: {}", err);

    MY_PROVIDER.unregister();
}
