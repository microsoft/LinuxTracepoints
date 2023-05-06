// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use core::ops;
use eh::_internal as ehi;
use eventheader as eh;

// The following is the planned implementation for define_provider!(MY_PROVIDER, "EhProvider1"):
extern "C" {
    #[link_name = "__start__EHS_EVENT_PTRS_MY_PROVIDER"]
    static mut _EHS_EVENT_PTRS_MY_PROVIDER_START: usize;
    #[link_name = "__stop__EHS_EVENT_PTRS_MY_PROVIDER"]
    static mut _EHS_EVENT_PTRS_MY_PROVIDER_STOP: usize;
}
#[link_section = "_EHS_EVENT_PTRS_MY_PROVIDER"]
static mut _EHS_EVENT_PTRS_MY_PROVIDER: *const ehi::EventHeaderTracepoint = core::ptr::null();
static MY_PROVIDER: eh::Provider = unsafe {
    ehi::provider_new(
        b"EhProvider1",
        b"", // Options, e.g. b"Gmygroup"
        ops::Range {
            start: &_EHS_EVENT_PTRS_MY_PROVIDER_START as *const usize
                as *mut *const ehi::EventHeaderTracepoint,
            end: &_EHS_EVENT_PTRS_MY_PROVIDER_STOP as *const usize
                as *mut *const ehi::EventHeaderTracepoint,
        },
    )
};

fn main() {
    let mut err;
    unsafe { MY_PROVIDER.register() };

    err =
    // The following is the planned implementation for write_event!(MY_PROVIDER, "EventName0"):
    {
        static _EHS_EVENT: ehi::EventHeaderTracepoint = ehi::EventHeaderTracepoint::new(
            ehi::EventHeader::new(eh::Level::Verbose, true),
            1,
            b"EventName0\0",
        );
        #[link_section = "_EHS_EVENT_PTRS_MY_PROVIDER"]
        static mut _EHS_EVENT_PTR: *const ehi::EventHeaderTracepoint = &_EHS_EVENT;

        println!("EventName0: {}", _EHS_EVENT.enabled());
        if !_EHS_EVENT.enabled() {
            0
        } else {
            _EHS_EVENT.write_eventheader(
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

    err =
    // The following is the planned implementation for write_event!(MY_PROVIDER, "EventName1", keyword(2)):
    {
        static _EHS_EVENT: ehi::EventHeaderTracepoint = ehi::EventHeaderTracepoint::new(
            ehi::EventHeader::new(eh::Level::Verbose, true),
            2,
            b"EventName1\0",
        );
        #[link_section = "_EHS_EVENT_PTRS_MY_PROVIDER"]
        static mut _EHS_EVENT_PTR: *const ehi::EventHeaderTracepoint = &_EHS_EVENT;

        println!("EventName1: {}", _EHS_EVENT.enabled());
        if !_EHS_EVENT.enabled() {
            0
        } else {
            _EHS_EVENT.write_eventheader(
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
