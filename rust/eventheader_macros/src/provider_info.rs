// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use proc_macro::*;

use crate::errors::Errors;
use crate::parser::{ArgConstraints::*, ArgResult, Parser};

/// Maximum length of a Tracepoint name "ProviderName_Attributes\0" (includes nul).
const EVENTHEADER_NAME_MAX: usize = 256;

const NAMES_MAX: usize = EVENTHEADER_NAME_MAX - "_LffKffffffffffffffffG".len();

pub struct ProviderInfo {
    pub symbol: Ident,
    pub name: String,
    pub group_name: String,
    pub debug: bool,
}

impl ProviderInfo {
    pub fn try_from_tokens(
        arg_span: Span,
        arg_tokens: TokenStream,
    ) -> Result<ProviderInfo, TokenStream> {
        let mut prov_id_set = false;
        let mut group_id_set = false;
        let mut errors = Errors::new();
        let mut root_parser = Parser::new(&mut errors, arg_span, arg_tokens);
        let mut prov = ProviderInfo {
            symbol: Ident::new("x", arg_span),
            name: String::new(),
            group_name: String::new(),
            debug: false,
        };

        // symbol name

        if let Some(ident) = root_parser.next_ident(
            RequiredNotLast,
            "expected identifier for provider symbol, e.g. MY_PROVIDER",
        ) {
            prov.symbol = ident;
        }

        // provider name

        if let Some((prov_name, span)) = root_parser.next_string_literal(
            Required,
            "expected string literal for provider name, e.g. define_provider!(MY_PROVIDER, \"MyCompany_MyComponent\")",
        ) {
            prov.name = prov_name;
            if prov.name.len() >= NAMES_MAX {
                root_parser.errors().add(span, "provider name.len() must be less than 234 chars");
            }
            if prov.name.contains('\0') {
                root_parser.errors().add(span, "provider name must not contain '\\0'");
            }
            if prov.name.contains(' ') {
                root_parser.errors().add(span, "provider name must not contain ' '");
            }
            if prov.name.contains(':') {
                root_parser.errors().add(span, "provider name must not contain ':'");
            }
        }

        // provider options (id or group_id)

        while let ArgResult::Option(option_name_ident, mut option_args_parser) =
            root_parser.next_arg(false)
        {
            const EXPECTED_GUID: &str =
                "expected \"GUID\", e.g. \"20cf46dd-3b90-476c-94e9-4e74bbc30e31\"";

            let errors = option_args_parser.errors();
            match option_name_ident.to_string().as_str() {
                "debug" => {
                    prov.debug = true;
                }
                "id" => {
                    if prov_id_set {
                        errors.add(option_name_ident.span(), "id already set");
                    }
                    prov_id_set = true;
                    option_args_parser.next_string_literal(RequiredLast, EXPECTED_GUID);
                }
                "group_id" | "groupid" => {
                    if group_id_set {
                        errors.add(option_name_ident.span(), "group_id already set");
                    }
                    group_id_set = true;
                    option_args_parser.next_string_literal(RequiredLast, EXPECTED_GUID);
                }
                "group_name" | "groupname" => {
                    if !prov.group_name.is_empty() {
                        errors.add(option_name_ident.span(), "group_name already set");
                    }
                    if let Some((id_str, id_span)) = option_args_parser
                        .next_string_literal(RequiredLast, "expected \"groupname\"")
                    {
                        for ch in id_str.chars() {
                            if !ch.is_ascii_lowercase() && !ch.is_ascii_digit() {
                                option_args_parser.errors().add(id_span, "group_name must contain only lowercase ASCII letters and ASCII digits");
                                break;
                            }
                        }
                        if id_str.len() >= NAMES_MAX {
                            root_parser
                                .errors()
                                .add(id_span, "group_name.len() must be less than 234 chars");
                        } else if prov.name.len() + id_str.len() >= NAMES_MAX {
                            root_parser.errors().add(id_span, "provider name.len() + group_name.len() must be less than 234 chars");
                        }
                        prov.group_name = id_str;
                    }
                }
                _ => {
                    errors.add(
                        option_name_ident.span(),
                        "expected option(\"value\"), e.g. group_name(\"groupname\")",
                    );
                }
            };
        }

        return if errors.is_empty() {
            Ok(prov)
        } else {
            Err(errors.into_items())
        };
    }
}
