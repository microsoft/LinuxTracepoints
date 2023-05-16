// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use proc_macro::*;

use crate::enums::{EnumToken, FieldEncoding};
use crate::expression::Expression;
use crate::field_info::FieldInfo;
use crate::field_option::{FieldOption, FieldStrategy};
use crate::ident_builder::IdentBuilder;
use crate::strings::*;
use crate::tree::Tree;

use crate::event_info::EventInfo;

pub struct EventGenerator {
    /// tokens for declaring the _EH_TAGn constants.
    tags_tree: Tree,
    /// tokens in the metadata [...] initializer.
    meta_tree: Tree,
    /// tokens in the _eh_write(...) function signature.
    func_args_tree: Tree,
    /// tokens in the _eh_write(...) function call.
    func_call_tree: Tree,
    /// tokens in the _eh_lengths = [...] array initializer.
    lengths_init_tree: Tree,
    /// tokens in the EventDataDescriptor &[...] array initializer.
    data_desc_init_tree: Tree,
    /// Code that runs if the provider is enabled.
    enabled_tree: Tree,
    /// scratch tree 1
    tree1: Tree,
    /// scratch tree 2
    tree2: Tree,
    /// scratch tree 3
    tree3: Tree,
    /// "_EH_TAGn"
    tag_n: IdentBuilder,
    /// "_eh_argN"
    arg_n: IdentBuilder,
    /// number of fields added so far
    field_count: u16,
    /// number of runtime lengths needed
    lengths_count: u16,
}

impl EventGenerator {
    pub fn new(span: Span) -> Self {
        return Self {
            tags_tree: Tree::new(span),
            meta_tree: Tree::new(span),
            func_args_tree: Tree::new(span),
            func_call_tree: Tree::new(span),
            lengths_init_tree: Tree::new(span),
            data_desc_init_tree: Tree::new(span),
            enabled_tree: Tree::new(span),
            tree1: Tree::new(span),
            tree2: Tree::new(span),
            tree3: Tree::new(span),
            tag_n: IdentBuilder::new(EH_TAG_CONST),
            arg_n: IdentBuilder::new(EH_ARG_VAR),
            field_count: 0,
            lengths_count: 0,
        };
    }

    pub fn generate(&mut self, mut event: EventInfo) -> TokenStream {
        let provider_symbol_string = event.provider_symbol.to_string();
        let provider_symbol_span = event.provider_symbol.span();

        self.field_count = 0;
        self.lengths_count = 0;

        // Before-field stuff:

        self.append_meta_name(&event.name);

        self.data_desc_init_tree
            // ::eventheader::_internal::EventDataDescriptor::zero(), // headers
            .add_path_call(DATADESC_ZERO_PATH, [])
            .add_punct(",")
            // ::eventheader::_internal::EventDataDescriptor::zero(), // metadata
            .add_path_call(DATADESC_ZERO_PATH, [])
            .add_punct(",");

        // always-present args for the helper function's prototype
        self.func_args_tree
            // _eh_tracepoint: &EventHeaderTracepoint
            .add_ident(EH_TRACEPOINT_VAR)
            .add_punct(":")
            .add_punct("&")
            .add_path(EVENTHEADERTRACEPOINT_PATH)
            // , activity_id: Option<&[u8; 16]>
            .add_punct(",")
            .add_ident(EH_ACTIVITY_ID_VAR)
            .add_punct(":")
            .add_path(OPTION_PATH)
            .add_punct("<")
            .add_punct("&")
            .add_group_square(
                self.tree1
                    .add_path(U8_PATH)
                    .add_punct(";")
                    .add_literal(Literal::usize_unsuffixed(16))
                    .drain(),
            )
            .add_punct(">")
            // , related_id: Option<&[u8; 16]>
            .add_punct(",")
            .add_ident(EH_RELATED_ID_VAR)
            .add_punct(":")
            .add_path(OPTION_PATH)
            .add_punct("<")
            .add_punct("&")
            .add_group_square(
                self.tree1
                    .add_path(U8_PATH)
                    .add_punct(";")
                    .add_literal(Literal::usize_unsuffixed(16))
                    .drain(),
            )
            .add_punct(">");

        // always-present args for the helper function's call site
        self.func_call_tree
            // &_EH_TRACEPOINT
            .add_punct("&")
            .add_ident(EH_TRACEPOINT_STATIC)
            // , None-or-Some(borrow(activity_id_tokens...))
            .add_punct(",")
            .push_span(event.activity_id.context)
            .add_borrowed_option_from_tokens(&mut self.tree1, event.activity_id.tokens)
            .pop_span()
            // , None-or-Some(borrow(related_id_tokens...))
            .add_punct(",")
            .push_span(event.related_id.context)
            .add_borrowed_option_from_tokens(&mut self.tree1, event.related_id.tokens)
            .pop_span();

        // Add the per-field stuff:

        for field in event.fields.drain(..) {
            self.add_field(field);
        }

        // code that runs if the provider is enabled:
        /*
        fn _eh_write(tp, aid, rid, func_args_tree...) -> u32 {
            let _eh_lengths = [lengths_init_tree...];
            tp.write_eventheader(aid, rid, &mut [data_desc_init_tree...]);
        }
        _eh_write(func_call_tree)
        */

        self.enabled_tree
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
            // #[allow(clippy::too_many_arguments)]
            .add_outer_attribute(
                "allow",
                self.tree1
                    .add_ident("clippy")
                    .add_punct("::")
                    .add_ident("too_many_arguments")
                    .drain(),
            )
            // Make a helper function and then call it. This does the following:
            // - Keep temporaries alive (this could also be done with a match expression).
            // - Give the optimizer the option to merge identical helpers.
            // fn _eh_write(tp, aid, rid, func_args_tree...) -> { ... }
            .add_ident("fn")
            .add_ident(EH_WRITE_FUNC)
            .add_group_paren(self.func_args_tree.drain())
            .add_punct("->")
            .add_path(U32_PATH)
            .add_group_curly(
                self.tree1
                    // let _eh_lengths: [u16; N] = [...];
                    .add_ident("let")
                    .add_ident(EH_LENGTHS_VAR)
                    .add_punct(":")
                    .add_group_square(
                        self.tree2
                            .add_path(U16_PATH)
                            .add_punct(";")
                            .add_literal(Literal::u16_unsuffixed(self.lengths_count))
                            .drain(),
                    )
                    .add_punct("=")
                    .add_group_square(self.lengths_init_tree.drain())
                    .add_punct(";")
                    // _eh_tracepoint.write_eventheader(aid, rid, &mut [data...]) as u32
                    .add_ident(EH_TRACEPOINT_VAR)
                    .add_punct(".")
                    .add_ident(EH_TRACEPOINT_WRITE_EVENTHEADER)
                    .add_group_paren(
                        self.tree2
                            .add_ident(EH_ACTIVITY_ID_VAR)
                            .add_punct(",")
                            .add_ident(EH_RELATED_ID_VAR)
                            .add_punct(",")
                            .add_punct("&")
                            .add_ident("mut")
                            .add_group_square(self.data_desc_init_tree.drain())
                            .drain(),
                    )
                    .add_ident("as")
                    .add_path(U32_PATH)
                    .drain(),
            )
            // _eh_write(tp, aid, rid, values...)
            .add_ident(EH_WRITE_FUNC)
            .add_group_paren(self.func_call_tree.drain());

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

        let event_tree = &mut self.tree3; // Alias tree3 to save a tree.

        // _EH_KEYWORD
        if event.keywords.len() == 1 {
            // Generate simple output if only one keyword.
            // const _EH_KEYWORD: u64 = KEYWORDS[0];
            let keyword = event.keywords.pop().unwrap();
            event_tree
                .push_span(keyword.context)
                .add_const_from_tokens(EH_KEYWORD_CONST, U64_PATH, keyword.tokens)
                .pop_span();
        } else {
            // More-complex output needed in other cases.
            //
            // We have suboptimal results if we combine the subexpressions ourselves,
            // e.g. doing "const X = (KEYWORDS0) | (KEYWORDS1);"" would result in
            // suboptimal error reporting for syntax errors in the user-supplied
            // expressions as well as warnings for unnecessary parentheses. Instead,
            // evaluate the subexpressions separately then combine the resulting
            // constants. This works for any number of keywords.
            //
            // const _EH_KEYWORD0: u64 = KEYWORDS0;
            // const _EH_KEYWORD1: u64 = KEYWORDS1;
            // const _EH_KEYWORD: u64 = 0u64 | _EH_KEYWORD0 | _EH_KEYWORD1;

            let mut keyword_n = IdentBuilder::new(EH_KEYWORD_CONST);

            // Build up "const _EH_KEYWORDn: u64 = KEYWORDSn; ..."" in event_tree.
            // Build up "0u64 | _EH_KEYWORD0 | _EH_KEYWORD1 ..." in tree1.

            // tree1 += "0u64"
            self.tree1.add_literal(Literal::u64_suffixed(0));

            for (n, keyword) in event.keywords.drain(..).enumerate() {
                // event_tree += "const _EH_KEYWORDn: u64 = KEYWORDSn;"
                event_tree
                    .push_span(keyword.context)
                    .add_const_from_tokens(keyword_n.set_suffix(n), U64_PATH, keyword.tokens)
                    .pop_span();

                // tree1 += "| _EH_KEYWORDn"
                self.tree1.add_punct("|").add_ident(keyword_n.current());
            }

            // event_tree += "const _EH_KEYWORD: u64 = 0u64 | _EH_KEYWORD0 | _EH_KEYWORD1;"
            event_tree.add_const_from_tokens(EH_KEYWORD_CONST, U64_PATH, self.tree1.drain());
        }

        event_tree
            // const _EH_TAGn: u16 = TAG;
            .add_tokens(self.tags_tree.drain())
            // identity::<&Provider>(&PROVIDER) // ensure compile error for bad provider symbol
            .add_identity_call(
                &mut self.tree2,
                PROVIDER_PATH,
                0,
                self.tree1
                    .add_punct("&")
                    .add_token(event.provider_symbol)
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
                            .add_tokens(event.version_tokens)
                            .add_punct(",")
                            .add_tokens(event.id_tokens)
                            .add_punct(",")
                            .add_tokens(event.tag.tokens)
                            .add_punct(",")
                            .add_tokens(event.opcode_tokens)
                            .add_punct(",")
                            .add_tokens(event.level.tokens)
                            .drain(),
                    )
                    .add_punct(",")
                    // _EH_KEYWORD,
                    .add_ident(EH_KEYWORD_CONST)
                    .add_punct(",")
                    // &[metadata]
                    .add_punct("&")
                    .add_group_square(self.meta_tree.drain())
                    .drain(),
            )
            .add_punct(";")
            // if !_EH_TRACEPOINT.enabled() { 0u32 }
            .add_ident("if")
            .add_punct("!")
            .add_ident(EH_TRACEPOINT_STATIC)
            .add_punct(".")
            .add_ident(EH_TRACEPOINT_ENABLED)
            .add_group_paren([])
            .add_group_curly(self.tree1.add_literal(Literal::u32_suffixed(0)).drain())
            // else { enabled_tree... }
            .add_ident("else")
            .add_group_curly(self.enabled_tree.drain());

        // Wrap the event in "{...}":
        let event_tokens = TokenStream::from(TokenTree::Group(Group::new(
            Delimiter::Brace,
            event_tree.drain().collect(),
        )));

        if event.debug {
            println!("{}", event_tokens);
        }

        return event_tokens;
    }

    fn add_field(&mut self, field: FieldInfo) {
        // Metadata

        if field.option.strategy.has_metadata() {
            self.append_meta_name(&field.name);

            let has_out = !field.format_or_field_count_expr.is_empty()
                || field.format_or_field_count_int != 0;
            let has_tag = !field.tag.is_empty();

            let inflags = (if has_out || has_tag { 0x80 } else { 0 })
                | (if field.option.strategy.is_slice() {
                    FieldEncoding::VArrayFlag
                } else {
                    0
                });
            self.add_typecode_meta(
                ENCODING_PATH,
                field.encoding_tokens,
                field.type_name_span,
                field.option.encoding.to_token(),
                inflags,
            );

            if has_out || has_tag {
                let outflags = if has_tag { 0x80 } else { 0 };
                self.add_typecode_meta(
                    FORMAT_PATH,
                    field.format_or_field_count_expr.tokens,
                    field.format_or_field_count_expr.context,
                    EnumToken::U8(field.format_or_field_count_int),
                    outflags,
                );
            }

            if has_tag {
                self.tag_n.set_suffix(self.field_count as usize);
                self.add_tag(field.tag);
            }
        }

        // Data

        self.data_desc_init_tree.push_span(field.type_name_span);
        self.arg_n.set_suffix(self.field_count as usize);

        match field.option.strategy {
            FieldStrategy::Scalar => {
                self.tree1
                    // , identity::<&VALUE_TYPE>(value_tokens...)
                    .push_span(field.type_name_span) // Use identity(...) as a target for error messages.
                    .add_identity_call(
                        &mut self.tree2,
                        field.option.value_type,
                        field.option.value_array_count,
                        field.value_tokens,
                    )
                    .pop_span();

                // Prototype: , _eh_argN: &value_type
                // Call site: , identity::<&value_type>(value_tokens...)
                self.add_func_scalar_arg(field.option); // consumes tree1

                // EventDataDescriptor::from_value(_eh_argN),
                self.add_data_desc_for_arg_n(DATADESC_FROM_VALUE_PATH);
            }

            FieldStrategy::SystemTime => {
                self.tree1
                    // match SystemTime::duration_since(value_tokens, SystemTime::UNIX_EPOCH) { ... }
                    .push_span(field.type_name_span) // Use duration_since(...) as a target for error messages.
                    .add_punct("&")
                    .add_ident("match")
                    .add_path_call(
                        SYSTEMTIME_DURATION_SINCE_PATH,
                        self.tree2
                            .add_tokens(field.value_tokens)
                            .add_punct(",")
                            .add_path(SYSTEMTIME_UNIX_EPOCH_PATH)
                            .drain(),
                    )
                    .add_group_curly(
                        self.tree2
                            // Ok(_eh_dur) => filetime_from_duration_after_1970(_eh_dur),
                            .add_path(RESULT_OK_PATH)
                            .add_group_paren(self.tree3.add_ident(EH_DUR_VAR).drain())
                            .add_punct("=>")
                            .add_path_call(
                                TIME_FROM_DURATION_AFTER_PATH,
                                self.tree3.add_ident(EH_DUR_VAR).drain(),
                            )
                            .add_punct(",")
                            // Err(_eh_dur) => filetime_from_duration_before_1970(_eh_dur.duration()),
                            .add_path(RESULT_ERR_PATH)
                            .add_group_paren(self.tree3.add_ident(EH_DUR_VAR).drain())
                            .add_punct("=>")
                            .add_path_call(
                                TIME_FROM_DURATION_BEFORE_PATH,
                                self.tree3
                                    .add_ident(EH_DUR_VAR)
                                    .add_punct(".")
                                    .add_ident("duration")
                                    .add_group_paren([])
                                    .drain(),
                            )
                            .add_punct(",")
                            .drain(),
                    )
                    .pop_span();

                // Prototype: , _eh_argN: &i64
                // Call site: , match SystemTime::duration_since(value_tokens, SystemTime::UNIX_EPOCH) { ... }
                self.add_func_scalar_arg(field.option); // consumes tree1

                // EventDataDescriptor::from_value(_eh_argN),
                self.add_data_desc_for_arg_n(DATADESC_FROM_VALUE_PATH);
            }

            FieldStrategy::RawData | FieldStrategy::RawField | FieldStrategy::RawFieldSlice => {
                // Prototype: , _eh_argN: &[value_type]
                // Call site: , AsRef::<[value_type]>::as_ref(value_tokens...)
                self.add_func_slice_arg(field.option, field.type_name_span, field.value_tokens);

                // EventDataDescriptor::from_counted(_eh_argN),
                self.add_data_desc_for_arg_n(DATADESC_FROM_SLICE_PATH);
            }

            FieldStrategy::CStr => {
                // Prototype: , _eh_argN: &[value_type]
                // Call site: , AsRef::<[value_type]>::as_ref(value_tokens...)
                self.add_func_slice_arg(field.option, field.type_name_span, field.value_tokens);

                // EventDataDescriptor::from_cstr(_eh_argN),
                self.add_data_desc_for_arg_n(DATADESC_FROM_CSTR_PATH);

                self.data_desc_init_tree
                    // EventDataDescriptor::from_value<value_type>(&0),
                    .add_path(DATADESC_FROM_VALUE_PATH)
                    .add_punct("::")
                    .add_punct("<")
                    .add_path(field.option.value_type) // value_type is u8 or u16
                    .add_punct(">")
                    .add_group_paren(
                        self.tree1
                            .add_punct("&")
                            .add_literal(Literal::u8_unsuffixed(0))
                            .drain(),
                    )
                    .add_punct(",");
            }

            FieldStrategy::Str => {
                if field.option.value_array_count == 0 {
                    // Prototype: , _eh_argN: &[value_type]
                    // Call site: , AsRef::<[value_type]>::as_ref(value_tokens...)
                    self.add_func_slice_arg(field.option, field.type_name_span, field.value_tokens);
                } else {
                    // e.g. ipv6 takes a fixed-length array, not a variable-length slice
                    self.tree1
                        // , identity::<&value_type>(value_tokens...)
                        .push_span(field.type_name_span) // Use identity(...) as a target for error messages.
                        .add_identity_call(
                            &mut self.tree2,
                            field.option.value_type,
                            field.option.value_array_count,
                            field.value_tokens,
                        )
                        .pop_span();

                    // Prototype: , _eh_argN: &[value_type; value_array_count]
                    // Call site: , identity::<&[value_type; value_array_count]>(value_tokens...)
                    self.add_func_scalar_arg(field.option); // consumes tree1
                }

                // EventDataDescriptor::from_value(&_eh_lengths[N]),
                // EventDataDescriptor::from_counted(_eh_argN),
                self.add_data_desc_with_length(SLICE_COUNT_PATH, DATADESC_FROM_SLICE_PATH);
            }

            FieldStrategy::Slice => {
                self.add_func_slice_arg(field.option, field.type_name_span, field.value_tokens);

                // EventDataDescriptor::from_value(&_eh_lengths[N]),
                // EventDataDescriptor::from_slice(_eh_argN),
                self.add_data_desc_with_length(SLICE_COUNT_PATH, DATADESC_FROM_SLICE_PATH);
            }

            FieldStrategy::Struct
            | FieldStrategy::RawStruct
            | FieldStrategy::RawStructSlice
            | FieldStrategy::RawMeta
            | FieldStrategy::RawMetaSlice => {}
        }

        self.data_desc_init_tree.pop_span();

        // Common

        self.field_count += 1;
    }

    fn add_data_desc_for_arg_n(&mut self, new_desc_path: &[&str]) {
        self.data_desc_init_tree
            // EventDataDescriptor::new_desc_path(_eh_argN),
            .add_path_call(
                new_desc_path,
                self.tree1.add_ident(self.arg_n.current()).drain(),
            )
            .add_punct(",");
    }

    fn add_data_desc_with_length(&mut self, get_length_path: &[&str], new_desc_path: &[&str]) {
        // get_length_path(_eh_argN),
        self.lengths_init_tree
            .add_path_call(
                get_length_path,
                self.tree1.add_ident(self.arg_n.current()).drain(),
            )
            .add_punct(",");

        // EventDataDescriptor::from_value(&_eh_lengths[N]),
        // EventDataDescriptor::new_desc_path(_eh_argN),
        self.data_desc_init_tree
            .add_path_call(
                DATADESC_FROM_VALUE_PATH,
                self.tree1
                    .add_punct("&")
                    .add_ident(EH_LENGTHS_VAR)
                    .add_group_square(
                        self.tree2
                            .add_literal(Literal::u16_unsuffixed(self.lengths_count))
                            .drain(),
                    )
                    .drain(),
            )
            .add_punct(",");
        self.add_data_desc_for_arg_n(new_desc_path);

        self.lengths_count += 1;
    }

    // We wrap all input expressions in adapter<T>(expression) because it allows
    // us to get MUCH better error messages. We attribute the adapter<T>() tokens
    // to the type_name_span so that if the expression is the wrong type, the
    // error message says "your expression didn't match the type expected by -->"
    // and the arrow points at the type_name, which is great. In cases where
    // as_ref() can be used, we use as_ref() as the adapter. Otherwise, we use
    // identity().

    /// Prototype: , _eh_argN: &VALUE_TYPE
    /// Call site: , tree1_tokens...
    fn add_func_scalar_arg(&mut self, field_option: &FieldOption) {
        // , _eh_argN: &VALUE_TYPE
        self.func_args_tree
            .add_punct(",")
            .add_ident(self.arg_n.current())
            .add_punct(":")
            .add_punct("&")
            .add_scalar_type_path(
                &mut self.tree2,
                field_option.value_type,
                field_option.value_array_count,
            );

        // We do not apply AsRef for non-slice types. AsRef provides a no-op mapping
        // for slices (i.e. AsRef<[u8]>::as_ref(&u8_slice) returns &u8_slice), but
        // there is not a no-op mapping for non-slice types (i.e.
        // AsRef<u8>::as_ref(&u8_val) will be a compile error). While this is a bit
        // inconsistent, I don't think it's a problem in practice. The non-slice
        // types don't get much value from as_ref. Most of their needs are handled
        // by the Deref trait, which the compiler applies automatically.

        // , value_tokens...
        self.func_call_tree
            .add_punct(",")
            .add_tokens(self.tree1.drain());
    }

    /// Prototype: , _eh_argN: &[VALUE_TYPE]
    /// Call site: , AsRef::<[VALUE_TYPE]>::as_ref(value_tokens...)
    fn add_func_slice_arg(
        &mut self,
        field_option: &FieldOption,
        field_type_name_span: Span,
        field_value_tokens: TokenStream,
    ) {
        // , _eh_argN: &[VALUE_TYPE]
        self.func_args_tree
            .add_punct(",")
            .add_ident(self.arg_n.current())
            .add_punct(":")
            .add_punct("&")
            .add_group_square(
                self.tree1
                    .add_scalar_type_path(
                        &mut self.tree2,
                        field_option.value_type,
                        field_option.value_array_count,
                    )
                    .drain(),
            );

        // For cases where the expected input is a slice &[T], we apply the
        // core::convert::AsRef<[T]> trait to unwrap the provided value. This is
        // most important for strings because otherwise the str functions would only
        // accept &[u8] (they wouldn't be able to accept &str or &String). This also
        // applies to 3rd-party types, e.g. widestring's U16String implements
        // AsRef<[u16]> so it just works as a value for the str16 field types.

        // , AsRef::<[VALUE_TYPE]>::as_ref(value_tokens...)
        self.func_call_tree
            .add_punct(",")
            .push_span(field_type_name_span) // Use as_ref(...) as a target for error messages.
            .add_path(ASREF_PATH)
            .add_punct("::")
            .add_punct("<")
            .add_group_square(
                self.tree1
                    .add_scalar_type_path(
                        &mut self.tree2,
                        field_option.value_type,
                        field_option.value_array_count,
                    )
                    .drain(),
            )
            .add_punct(">")
            .add_punct("::")
            .add_ident("as_ref")
            .add_group_paren(field_value_tokens)
            .pop_span();
    }

    fn add_typecode_meta(
        &mut self,
        enum_type_path: &[&str],
        tokens: TokenStream,
        span: Span,
        type_token: EnumToken,
        flags: u8,
    ) {
        if tokens.is_empty() {
            match type_token {
                EnumToken::U8(enum_int) => {
                    self.append_meta(enum_int | flags);
                    return;
                }

                // EnumVal
                EnumToken::Str(enum_name) => {
                    self.meta_tree
                        .push_span(span)
                        .add_path(enum_type_path)
                        .add_punct("::")
                        .add_ident(enum_name);
                }
            };
        } else {
            // identity::<EnumType>(...)
            self.meta_tree
                .push_span(span)
                .add_path(IDENTITY_PATH) // Use identity(...) as a target for error messages.
                .add_punct("::")
                .add_punct("<")
                .add_path(enum_type_path)
                .add_punct(">")
                .add_group_paren(tokens);
        }

        // .as_int()
        self.meta_tree
            .add_punct(".")
            .add_ident("as_int")
            .add_group_paren([]);

        // | flags
        if flags != 0 {
            self.meta_tree
                .add_punct("|")
                .add_literal(Literal::u8_unsuffixed(flags));
        }

        self.meta_tree.add_punct(",");
        self.meta_tree.pop_span();
    }

    fn add_tag(&mut self, expression: Expression) {
        // Implicitly uses self.tag_const as the name for the tag's constant.

        // const _EH_TAGn: u16 = TAG;
        self.tags_tree
            .add_const_from_tokens(self.tag_n.current(), U16_PATH, expression.tokens);

        // tag_byte0(_EH_TAGn), tag_byte1(_EH_TAGn),
        self.meta_tree
            .add_path_call(
                TAG_BYTE0_PATH,
                self.tree1
                    .push_span(expression.context)
                    .add_ident(self.tag_n.current())
                    .pop_span()
                    .drain(),
            )
            .add_punct(",")
            .add_path_call(
                TAG_BYTE1_PATH,
                self.tree1
                    .push_span(expression.context)
                    .add_ident(self.tag_n.current())
                    .pop_span()
                    .drain(),
            )
            .add_punct(",");
    }

    fn append_meta_name(&mut self, name: &str) {
        for &ch in name.as_bytes() {
            self.append_meta(ch);
        }
        self.append_meta(0);
    }

    fn append_meta(&mut self, byte: u8) {
        self.meta_tree
            .add_literal(Literal::u8_unsuffixed(byte))
            .add_punct(",");
    }
}
