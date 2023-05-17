// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use proc_macro::*;

use crate::errors::Errors;
use crate::parser::{ArgConstraints::*, Parser};

pub struct EnabledInfo {
    pub provider_symbol: Ident,
    pub level: TokenStream,
    pub keyword: TokenStream,
}

impl EnabledInfo {
    pub fn try_from_tokens(
        arg_span: Span,
        arg_tokens: TokenStream,
    ) -> Result<EnabledInfo, TokenStream> {
        let mut errors = Errors::new();
        let mut root_parser = Parser::new(&mut errors, arg_span, arg_tokens);
        let mut enabled = EnabledInfo {
            provider_symbol: Ident::new("x", arg_span),
            level: TokenStream::new(),
            keyword: TokenStream::new(),
        };

        // symbol name

        if let Some(ident) = root_parser.next_ident(
            RequiredNotLast,
            "expected identifier for provider symbol, e.g. MY_PROVIDER",
        ) {
            enabled.provider_symbol = ident;
        }

        // level

        enabled.level = root_parser.next_tokens(
            Required,
            "expected constant for level, e.g. Level::Verbose",
        );

        // keyword

        enabled.keyword = root_parser.next_tokens(
            RequiredLast,
            "expected constant for keyword, e.g. 1 or 0x1f",
        );

        return if errors.is_empty() {
            Ok(enabled)
        } else {
            Err(errors.into_expression())
        };
    }
}
