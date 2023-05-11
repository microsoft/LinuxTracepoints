// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use eh::_internal as ehi;
use eventheader as eh;

// The following is the planned expansion for:
// define_provider!(MY_PROVIDER, "EhProvider1", group_name("mygroup"))
extern "C" {
    #[link_name = "__start__eh_event_ptrs_MY_PROVIDER"] // linker-generated symbol
    static mut __start__eh_event_ptrs_MY_PROVIDER: ::core::primitive::usize;
    #[link_name = "__stop__eh_event_ptrs_MY_PROVIDER"] // linker-generated symbol
    static mut __stop__eh_event_ptrs_MY_PROVIDER: ::core::primitive::usize;
}
#[link_section = "_eh_event_ptrs_MY_PROVIDER"]
#[no_mangle]
static mut _eh_define_provider_MY_PROVIDER: *const usize = ::core::ptr::null();
static MY_PROVIDER: eh::Provider = unsafe {
    eh::_internal::provider_new(
        b"EhProvider1",
        b"Gmygroup",
        &__start__eh_event_ptrs_MY_PROVIDER as *const usize,
        &__stop__eh_event_ptrs_MY_PROVIDER as *const usize,
    )
};

eh::define_provider!(MY_PROVIDER2, "EhProvider2");

fn main() {
    let mut err;
    unsafe { MY_PROVIDER.register() };

    {
        const _TLG_KEYWORD: ::core::primitive::u64 = 1u64;
        static _EH_EVENT: ::eventheader::_internal::EventHeaderTracepoint =
            ::eventheader::_internal::EventHeaderTracepoint::new(
                ::eventheader::_internal::EventHeader::from_parts(
                    ::eventheader::_internal::HeaderFlags::DefaultWithExtension,
                    0,
                    0,
                    0,
                    ::eventheader::Opcode::Info,
                    ::eventheader::Level::Verbose,
                ),
                _TLG_KEYWORD,
                &[69, 118, 101, 110, 116, 78, 97, 109, 101, 48, 0],
            );
        #[link_section = "_eh_event_ptrs_MY_PROVIDER"]
        static mut _EH_EVENT_PTR: *const ::eventheader::_internal::EventHeaderTracepoint =
            &_EH_EVENT;
        if !_EH_EVENT.enabled() {
            0u32
        } else {
            #[allow(clippy::too_many_arguments)]
            fn _tlg_write(
                _tlg_prov: &::eventheader::_internal::EventHeaderTracepoint,
                _tlg_aid: ::core::option::Option<&[::core::primitive::u8; 16]>,
                _tlg_rid: ::core::option::Option<&[::core::primitive::u8; 16]>,
            ) -> ::core::primitive::u32 {
                let _tlg_lengths: [::core::primitive::u16; 0] = [];
                _tlg_prov.write_eventheader(
                    _tlg_aid,
                    _tlg_rid,
                    &mut [
                        ::eventheader::_internal::EventDataDescriptor::zero(),
                        ::eventheader::_internal::EventDataDescriptor::zero(),
                    ],
                ) as ::core::primitive::u32
            }
            _tlg_write(
                &_EH_EVENT,
                ::core::option::Option::None,
                ::core::option::Option::None,
            )
        }
    };
    eh::write_event!(MY_PROVIDER, "EventName0", debug());

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
            ehi::EventHeader::from_parts(ehi::HeaderFlags::DefaultWithExtension, 0, 0, 0, eh::Opcode::Info, eh::Level::Verbose),
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
