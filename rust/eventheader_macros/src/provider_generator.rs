// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use proc_macro::*;

use crate::provider_info::ProviderInfo;
use crate::strings::*;
use crate::tree::Tree;

pub struct ProviderGenerator {
    prov_tree: Tree,
    tree1: Tree,
    tree2: Tree,
}

impl ProviderGenerator {
    pub fn new(span: Span) -> Self {
        return Self {
            prov_tree: Tree::new(span),
            tree1: Tree::new(span),
            tree2: Tree::new(span),
        };
    }

    pub fn generate(&mut self, provider: ProviderInfo) -> TokenStream {
        let provider_sym = provider.symbol.to_string();

        // __start__eh_tracepoints_MY_PROVIDER
        let provider_section_start =
            String::from_iter([TRACEPOINTS_SECTION_START_PREFIX, &provider_sym].into_iter());

        // __stop__eh_tracepoints_MY_PROVIDER
        let provider_section_stop =
            String::from_iter([TRACEPOINTS_SECTION_STOP_PREFIX, &provider_sym].into_iter());

        let options = if provider.group_name.is_empty() {
            String::new()
        } else {
            String::from_iter(["G", &provider.group_name].into_iter())
        };

        let prov_tokens = self
            .prov_tree
            // #[cfg(not(target_os = "linux"))]
            .add_cfg_not_linux()
            // static MY_PROVIDER: eh::Provider = unsafe { ... };
            .add_ident("static")
            .add_token(provider.symbol.clone())
            .add_punct(":")
            .add_path(PROVIDER_PATH)
            .add_punct("=")
            .add_ident("unsafe")
            .add_group_curly(
                self.tree1
                    // eh::_internal::provider_new( ... )
                    .add_path_call(
                        PROVIDER_NEW_PATH,
                        self.tree2
                            // b"ProviderName",
                            .add_literal(Literal::byte_string(provider.name.as_bytes()))
                            .add_punct(",")
                            // b"Ggroupname",
                            .add_literal(Literal::byte_string(options.as_bytes()))
                            .add_punct(",")
                            // &_start__eh_tracepoints_MY_PROVIDER as *const usize,
                            .add_path_call(NULL_PATH, [])
                            .add_punct(",")
                            // &_stop__eh_tracepoints_MY_PROVIDER as *const usize,
                            .add_path_call(NULL_PATH, [])
                            .add_punct(",")
                            .drain(),
                    )
                    .drain(),
            )
            .add_punct(";")
            // #[cfg(target_os = "linux")]
            .add_cfg_linux()
            // extern { _start; _stop; }
            .add_ident("extern")
            .add_group_curly(
                self.tree1
                    // #[link_name = "__start__eh_tracepoints_MY_PROVIDER"]
                    .add_punct("#")
                    .add_group_square(
                        self.tree2
                            .add_ident("link_name")
                            .add_punct("=")
                            .add_literal(Literal::string(&provider_section_start))
                            .drain(),
                    )
                    // static mut _start__eh_tracepoints_MY_PROVIDER: ::core::primitive::usize;
                    .add_ident("static")
                    .add_ident("mut")
                    .add_ident(provider_section_start.split_at(1).1)
                    .add_punct(":")
                    .add_path(USIZE_PATH)
                    .add_punct(";")
                    // #[link_name = "__stop__eh_tracepoints_MY_PROVIDER"] // linker-generated symbol
                    .add_punct("#")
                    .add_group_square(
                        self.tree2
                            .add_ident("link_name")
                            .add_punct("=")
                            .add_literal(Literal::string(&provider_section_stop))
                            .drain(),
                    )
                    // static mut _stop__eh_tracepoints_MY_PROVIDER: ::core::primitive::usize;
                    .add_ident("static")
                    .add_ident("mut")
                    .add_ident(provider_section_stop.split_at(1).1)
                    .add_punct(":")
                    .add_path(USIZE_PATH)
                    .add_punct(";")
                    .drain(),
            )
            // #[link_section = "_eh_tracepoints_MY_PROVIDER"]
            .add_punct("#")
            .add_group_square(
                self.tree1
                    .add_ident("link_section")
                    .add_punct("=")
                    .add_literal(Literal::string(&String::from_iter(
                        [TRACEPOINTS_SECTION_PREFIX, &provider_sym].into_iter(),
                    )))
                    .drain(),
            )
            // #[no_mangle]
            .add_punct("#")
            .add_group_square(self.tree1.add_ident("no_mangle").drain())
            // static mut _eh_define_provider_MY_PROVIDER: *const usize = ::core::ptr::null();
            .add_ident("static")
            .add_ident("mut")
            .add_ident(&String::from_iter(
                [PROVIDER_PTR_VAR_PREFIX, &provider_sym].into_iter(),
            ))
            .add_punct(":")
            .add_punct("*")
            .add_ident("const")
            .add_path(USIZE_PATH)
            .add_punct("=")
            .add_path_call(NULL_PATH, [])
            .add_punct(";")
            // #[cfg(target_os = "linux")]
            .add_cfg_linux()
            // static MY_PROVIDER: eh::Provider = unsafe { ... };
            .add_ident("static")
            .add_token(provider.symbol)
            .add_punct(":")
            .add_path(PROVIDER_PATH)
            .add_punct("=")
            .add_ident("unsafe")
            .add_group_curly(
                self.tree1
                    // eh::_internal::provider_new( ... )
                    .add_path_call(
                        PROVIDER_NEW_PATH,
                        self.tree2
                            // b"ProviderName",
                            .add_literal(Literal::byte_string(provider.name.as_bytes()))
                            .add_punct(",")
                            // b"Ggroupname",
                            .add_literal(Literal::byte_string(options.as_bytes()))
                            .add_punct(",")
                            // &_start__eh_tracepoints_MY_PROVIDER as *const usize,
                            .add_punct("&")
                            .add_ident(provider_section_start.split_at(1).1)
                            .add_ident("as")
                            .add_punct("*")
                            .add_ident("const")
                            .add_path(USIZE_PATH)
                            .add_punct(",")
                            // &_stop__eh_tracepoints_MY_PROVIDER as *const usize,
                            .add_punct("&")
                            .add_ident(provider_section_stop.split_at(1).1)
                            .add_ident("as")
                            .add_punct("*")
                            .add_ident("const")
                            .add_path(USIZE_PATH)
                            .add_punct(",")
                            .drain(),
                    )
                    .drain(),
            )
            .add_punct(";")
            .drain()
            .collect();

        if provider.debug {
            println!("{}", prov_tokens);
        }

        return prov_tokens;
    }
}
