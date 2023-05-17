// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use proc_macro::*;

use crate::strings::*;
use crate::tree::Tree;

use crate::enabled_info::EnabledInfo;

pub struct EnabledGenerator {
    /// scratch tree 1
    tree1: Tree,
    /// scratch tree 2
    tree2: Tree,
    /// scratch tree 3
    event_tree: Tree,
}

impl EnabledGenerator {
    pub fn new(span: Span) -> Self {
        return Self {
            tree1: Tree::new(span),
            tree2: Tree::new(span),
            event_tree: Tree::new(span),
        };
    }

    pub fn generate(&mut self, enabled: EnabledInfo) -> TokenStream {
        let provider_symbol_string = enabled.provider_symbol.to_string();
        let provider_symbol_span = enabled.provider_symbol.span();

        // put it all together:
        /*
        const _EH_KEYWORD = keywords...;
        const _EH_TAGn: u16 = TAGn;
        static _EH_TRACEPOINT = EventHeaderTracepoint::new(...);
        static _EH_TRACEPOINT_PTR = &_EH_TRACEPOINT;
        if !_EH_TRACEPOINT.enabled() {
            0u32
        } else {
            enabled_tree...
        }
        */

        self.event_tree
            // identity::<&Provider>(&PROVIDER) // ensure compile error for bad provider symbol
            .add_identity_call(
                &mut self.tree2,
                PROVIDER_PATH,
                0,
                self.tree1
                    .add_punct("&")
                    .add_token(enabled.provider_symbol)
                    .drain(),
            )
            .add_punct(";")
            // identity::<&* const usize>(&_eh_define_provider_MY_PROVIDER) // ensure compile error for aliased provider symbol
            .add_path(IDENTITY_PATH)
            .add_punct("::")
            .add_punct("<")
            .add_punct("&")
            .add_punct("*")
            .add_ident("const")
            .add_scalar_type_path(&mut self.tree2, USIZE_PATH, 0)
            .add_punct(">")
            .add_group_paren(
                self.tree1
                    .add_ident("unsafe")
                    .add_group_curly(
                        self.tree2
                            .add_punct("&")
                            .push_span(provider_symbol_span)
                            .add_ident(&String::from_iter(
                                [PROVIDER_PTR_VAR_PREFIX, &provider_symbol_string].into_iter(),
                            ))
                            .pop_span()
                            .drain(),
                    )
                    .drain(),
            )
            .add_punct(";")
            // static _EH_TRACEPOINT: EventHeaderTracepoint = EventHeaderTracepoint::new(...);
            .add_ident("static")
            .add_ident(EH_TRACEPOINT_STATIC)
            .add_punct(":")
            .add_path(EVENTHEADERTRACEPOINT_PATH)
            .add_punct("=")
            .add_path_call(
                EVENTHEADERTRACEPOINT_NEW_PATH,
                self.tree1
                    // EventHeader::from_parts(...),
                    .add_path_call(
                        EVENTHEADER_FROM_PARTS_PATH,
                        self.tree2
                            .add_path(HEADER_FLAGS_DEFAULT_PATH)
                            .add_punct(",")
                            .add_literal(Literal::u8_unsuffixed(0))
                            .add_punct(",")
                            .add_literal(Literal::u16_unsuffixed(0))
                            .add_punct(",")
                            .add_literal(Literal::u16_unsuffixed(0))
                            .add_punct(",")
                            .add_path(OPCODE_INFO_PATH)
                            .add_punct(",")
                            .add_tokens(enabled.level)
                            .drain(),
                    )
                    .add_punct(",")
                    // _EH_KEYWORD,
                    .add_tokens(enabled.keyword)
                    .add_punct(",")
                    // &[metadata]
                    .add_punct("&")
                    .add_group_square([])
                    .drain(),
            )
            .add_punct(";")
            // #[link_section = "_eh_tracepoints_MY_PROVIDER"]
            .add_punct("#")
            .add_group_square(
                self.tree1
                    .add_ident("link_section")
                    .add_punct("=")
                    .add_literal(Literal::string(&String::from_iter(
                        [TRACEPOINTS_SECTION_PREFIX, &provider_symbol_string].into_iter(),
                    )))
                    .drain(),
            )
            // static mut _EH_TRACEPOINT_PTR: *const ehi::EventHeaderTracepoint = &_EH_TRACEPOINT;
            .add_ident("static")
            .add_ident("mut")
            .add_ident(EH_TRACEPOINT_PTR_STATIC)
            .add_punct(":")
            .add_punct("*")
            .add_ident("const")
            .add_path(EVENTHEADERTRACEPOINT_PATH)
            .add_punct("=")
            .add_punct("&")
            .add_ident(EH_TRACEPOINT_STATIC)
            .add_punct(";")
            // _EH_TRACEPOINT.enabled()
            .add_ident(EH_TRACEPOINT_STATIC)
            .add_punct(".")
            .add_ident(EH_TRACEPOINT_ENABLED)
            .add_group_paren([]);

        // Wrap the event in "{...}":
        let enabled_tokens = TokenStream::from(TokenTree::Group(Group::new(
            Delimiter::Brace,
            self.event_tree.drain().collect(),
        )));

        return enabled_tokens;
    }
}
