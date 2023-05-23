// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use proc_macro::*;

use crate::enums::FieldFormat;
use crate::errors::Errors;
use crate::expression::Expression;
use crate::field_info::FieldInfo;
use crate::field_option::FieldStrategy;
use crate::field_options::FIELD_OPTIONS;
use crate::parser::{ArgConstraints::*, ArgResult, Parser};
use crate::strings::*;
use crate::tree::Tree;

const METADATA_BYTES_MAX: u16 = u16::MAX; // EventHeader limit
const STRUCT_FIELDS_MAX: u8 = 127; // EventHeader limit

pub struct EventInfo {
    pub provider_symbol: Ident,
    pub name: String,
    pub id_tokens: TokenStream,
    pub version_tokens: TokenStream,
    pub opcode_tokens: TokenStream,
    pub level: Expression,
    pub keywords: Vec<Expression>,
    pub tag: Expression,
    pub activity_id: Expression,
    pub related_id: Expression,
    pub fields: Vec<FieldInfo>,
    pub debug: bool,

    // Set to 0 if we've already emitted an error message.
    metadata_bytes_used: u16,
}

impl EventInfo {
    pub fn try_from_tokens(
        arg_span: Span,
        arg_tokens: TokenStream,
    ) -> Result<EventInfo, TokenStream> {
        let mut event = EventInfo {
            provider_symbol: Ident::new("x", arg_span),
            name: String::new(),
            id_tokens: TokenStream::new(),
            version_tokens: TokenStream::new(),
            opcode_tokens: TokenStream::new(),
            level: Expression::empty(arg_span),
            keywords: Vec::new(),
            tag: Expression::empty(arg_span),
            activity_id: Expression::empty(arg_span),
            related_id: Expression::empty(arg_span),
            fields: Vec::new(),
            debug: false,
            metadata_bytes_used: 1,
        };
        let mut errors = Errors::new();
        let mut root_parser = Parser::new(&mut errors, arg_span, arg_tokens);
        let mut scratch_tree = Tree::new(arg_span);

        #[cfg(debug_assertions)]
        for i in 1..FIELD_OPTIONS.len() {
            debug_assert!(
                FIELD_OPTIONS[i - 1]
                    .option_name
                    .lt(FIELD_OPTIONS[i].option_name),
                "{} <=> {}",
                FIELD_OPTIONS[i - 1].option_name,
                FIELD_OPTIONS[i].option_name
            );
        }

        // provider

        if let Some(ident) = root_parser.next_ident(
            RequiredNotLast,
            "expected identifier for provider symbol, e.g. MY_PROVIDER",
        ) {
            event.provider_symbol = ident;
        }

        // event name

        if let Some((event_name, span)) = root_parser.next_string_literal(
            Required,
            "expected string literal for event name, e.g. write_event!(PROVIDER, \"EventName\", ...)",
        ) {
            event.name = event_name;
            event.add_estimated_metadata(root_parser.errors(), span, event.name.len() + 1);

            if event.name.contains('\0') {
                root_parser.errors().add(span, "event name must not contain '\\0'");
            }
        }

        // options

        event.parse_event_options(&mut root_parser, false, &mut scratch_tree);

        // Set defaults for optional values

        // id default: 0
        if event.id_tokens.is_empty() {
            event.id_tokens = scratch_tree
                .add_literal(Literal::u16_unsuffixed(0))
                .drain()
                .collect();
        }

        // version default: 0
        if event.version_tokens.is_empty() {
            event.version_tokens = scratch_tree
                .add_literal(Literal::u8_unsuffixed(0))
                .drain()
                .collect();
        }

        // level default: Level::Verbose
        if event.level.is_empty() {
            event.level = Expression::new(
                arg_span,
                scratch_tree.add_path(LEVEL_VERBOSE_PATH).drain().collect(),
            );
        }

        // opcode default: Opcode::Info
        if event.opcode_tokens.is_empty() {
            event.opcode_tokens = scratch_tree.add_path(OPCODE_INFO_PATH).drain().collect();
        }

        // keyword default: 1u64
        if event.keywords.is_empty() {
            event.keywords.push(Expression::new(
                arg_span,
                scratch_tree
                    .add_literal(Literal::u64_suffixed(1))
                    .drain()
                    .collect(),
            ));
        }

        // tag default: 0
        if event.tag.is_empty() {
            event.tag = Expression::new(
                arg_span,
                scratch_tree
                    .add_literal(Literal::u32_unsuffixed(0))
                    .drain()
                    .collect(),
            );
        }

        // Done.

        return if errors.is_empty() {
            Ok(event)
        } else {
            Err(errors.into_expression())
        };
    }

    /// Parses options. Returns the number of logical fields added to the event.
    fn parse_event_options(
        &mut self,
        parent_parser: &mut Parser,
        in_struct: bool,
        scratch_tree: &mut Tree,
    ) -> u8 {
        let mut logical_fields_added: u8 = 0;
        let mut channel_set = false;
        let mut task_set = false;

        while let ArgResult::Option(option_ident, mut option_parser) = parent_parser.next_arg(false)
        {
            let errors = option_parser.errors();
            let option_name = option_ident.to_string();

            if let Ok(field_option_index) =
                FIELD_OPTIONS.binary_search_by(|o| o.option_name.cmp(&option_name))
            {
                let mut field = FieldInfo {
                    type_name_span: option_ident.span(),
                    option: &FIELD_OPTIONS[field_option_index],
                    name: String::new(),
                    value_tokens: TokenStream::new(),
                    encoding_tokens: TokenStream::new(),
                    format_or_field_count_expr: Expression::empty(option_ident.span()),
                    format_or_field_count_int: FIELD_OPTIONS[field_option_index].format as u8,
                    tag: Expression::empty(option_ident.span()),
                };

                let field_has_metadata = field.option.strategy.has_metadata();

                if !field_has_metadata {
                    // No metadata, so don't try to parse a field name.
                } else if let Some((field_name, field_span)) = option_parser.next_string_literal(
                    RequiredNotLast,
                    "expected field name (must be a string literal, e.g. \"field name\")",
                ) {
                    field.name = field_name;
                    if field.name.contains('\0') {
                        option_parser
                            .errors()
                            .add(field_span, "field name must not contain '\\0'");
                    }
                }

                let field_accepts_tag;
                let field_accepts_format;
                let field_wants_struct;

                match field.option.strategy {
                    FieldStrategy::Scalar
                    | FieldStrategy::SystemTime
                    | FieldStrategy::CStr
                    | FieldStrategy::Str
                    | FieldStrategy::Slice => {
                        field_accepts_tag = true;
                        field_accepts_format = true;
                        field_wants_struct = false;
                    }
                    FieldStrategy::Struct => {
                        field_accepts_tag = true;
                        field_accepts_format = false;
                        field_wants_struct = true;
                    }
                    FieldStrategy::RawField
                    | FieldStrategy::RawFieldSlice
                    | FieldStrategy::RawMeta
                    | FieldStrategy::RawMetaSlice => {
                        field_accepts_tag = true;
                        field_accepts_format = true;
                        field_wants_struct = false;

                        field.encoding_tokens = filter_enum_tokens(
                            option_parser.next_tokens(
                                Required,
                                &expected_enum_message("FieldEncoding", "Value32", 4),
                            ),
                            "FieldEncoding",
                            ENCODING_ENUMS,
                            option_ident.span(),
                            scratch_tree,
                        );
                    }
                    FieldStrategy::RawStruct | FieldStrategy::RawStructSlice => {
                        field_accepts_tag = true;
                        field_accepts_format = false;
                        field_wants_struct = false;

                        if in_struct {
                            option_parser
                                .errors()
                                .add(option_ident.span(), "RawStruct not allowed within Struct");
                        }

                        let tokens = option_parser
                            .next_tokens(Required, "expected struct field count value, e.g. 2");
                        field.format_or_field_count_expr = Expression::new(
                            option_ident.span(),
                            scratch_tree
                                .push_span(option_ident.span())
                                .add_path_call(FORMAT_FROM_INT_PATH, tokens)
                                .pop_span()
                                .drain()
                                .collect(),
                        );
                    }
                    FieldStrategy::RawData => {
                        field_accepts_tag = false;
                        field_accepts_format = false;
                        field_wants_struct = false;
                    }
                }

                if field.option.strategy.data_count() != 0 {
                    field.value_tokens =
                        option_parser.next_tokens(Required, "expected field value");
                }

                loop {
                    match option_parser.next_arg(field_wants_struct) {
                        ArgResult::None => {
                            self.push_field(option_parser.errors(), field);
                            break;
                        }
                        ArgResult::Struct(mut struct_parser) => {
                            let struct_index = self.fields.len();
                            let field_type_name_span = field.type_name_span;

                            field.format_or_field_count_int = 1; // For metadata estimate, assume fields present.
                            self.push_field(struct_parser.errors(), field);

                            let field_count =
                                self.parse_event_options(&mut struct_parser, true, scratch_tree);
                            if field_count == 0 {
                                struct_parser
                                    .errors()
                                    .add(field_type_name_span, "struct must have at least 1 field");
                            }
                            self.fields[struct_index].format_or_field_count_int =
                                field_count & FieldFormat::ValueMask;
                            break;
                        }
                        ArgResult::Option(field_option_ident, mut field_option_parser) => {
                            let errors = field_option_parser.errors();
                            let field_option_name = field_option_ident.to_string();

                            match field_option_name.as_str() {
                                "tag" if field_accepts_tag => {
                                    if !field.tag.is_empty() {
                                        errors.add(field_option_ident.span(), "tag already set");
                                    }
                                    field.tag = Expression::new(
                                        field_option_ident.span(),
                                        field_option_parser.next_tokens(
                                            RequiredLast,
                                            "expected Tag value, e.g. 1 or 0xFFFF",
                                        ),
                                    );
                                }
                                "format" if field_accepts_format => {
                                    if !field.format_or_field_count_expr.is_empty() {
                                        errors.add(field_option_ident.span(), "format already set");
                                    }
                                    field.format_or_field_count_expr = Expression::new(
                                        field_option_ident.span(),
                                        filter_enum_tokens(
                                            field_option_parser.next_tokens(
                                                RequiredLast,
                                                &expected_enum_message("FieldFormat", "String", 2),
                                            ),
                                            "FieldFormat",
                                            FORMAT_ENUMS,
                                            field_option_ident.span(),
                                            scratch_tree,
                                        ),
                                    );
                                }
                                _ => {
                                    errors.add(field_option_ident.span(), "unrecognized option");
                                }
                            }
                        }
                    }
                }

                if field_has_metadata {
                    if in_struct && logical_fields_added == STRUCT_FIELDS_MAX {
                        option_parser
                            .errors()
                            .add(option_ident.span(), "too many fields in struct (limit 127)");
                    } else if !in_struct && logical_fields_added == 254 {
                        option_parser
                            .errors()
                            .add(option_ident.span(), "too many fields in event (limit 254)");
                    }

                    logical_fields_added = logical_fields_added.saturating_add(1);
                }
            } else {
                match option_name.as_str() {
                    "debug" if !in_struct => {
                        self.debug = true;
                        continue;
                    }
                    "id_version" if !in_struct => {
                        if !self.id_tokens.is_empty() {
                            errors.add(option_ident.span(), "id_version already set");
                        }
                        self.id_tokens = option_parser
                            .next_tokens(RequiredNotLast, "expected Id value, e.g. 1 or 0x200F");
                        self.version_tokens = option_parser
                            .next_tokens(RequiredLast, "expected Version value, e.g. 0 or 0x1F");
                    }
                    "channel" if !in_struct => {
                        if channel_set {
                            errors.add(option_ident.span(), "channel already set");
                        }
                        channel_set = true;
                        option_parser.next_tokens(
                            RequiredLast,
                            &expected_enum_message("Channel", "TraceLogging", 11),
                        );
                    }
                    "level" if !in_struct => {
                        if !self.level.is_empty() {
                            errors.add(option_ident.span(), "level already set");
                        }
                        self.level = Expression::new(
                            option_ident.span(),
                            filter_enum_tokens(
                                option_parser.next_tokens(
                                    RequiredLast,
                                    &expected_enum_message("Level", "Verbose", 5),
                                ),
                                "Level",
                                LEVEL_ENUMS,
                                option_ident.span(),
                                scratch_tree,
                            ),
                        );
                    }
                    "opcode" if !in_struct => {
                        if !self.opcode_tokens.is_empty() {
                            errors.add(option_ident.span(), "opcode already set");
                        }
                        self.opcode_tokens = filter_enum_tokens(
                            option_parser.next_tokens(
                                RequiredLast,
                                &expected_enum_message("Opcode", "Info", 0),
                            ),
                            "Opcode",
                            OPCODE_ENUMS,
                            option_ident.span(),
                            scratch_tree,
                        );
                    }
                    "task" if !in_struct => {
                        if task_set {
                            errors.add(option_ident.span(), "task already set");
                        }
                        task_set = true;
                        option_parser
                            .next_tokens(RequiredLast, "expected Task value, e.g. 1 or 0x2001");
                    }
                    "keyword" if !in_struct => {
                        self.keywords.push(Expression::new(
                            option_ident.span(),
                            option_parser
                                .next_tokens(RequiredLast, "expected Keyword value, e.g. 0x100F"),
                        ));
                    }
                    "tag" if !in_struct => {
                        if !self.tag.is_empty() {
                            errors.add(option_ident.span(), "tag already set");
                        }
                        self.tag = Expression::new(
                            option_ident.span(),
                            option_parser
                                .next_tokens(RequiredLast, "expected Tag value, e.g. 1 or 0xFFFF"),
                        );
                    }
                    "activity_id" if !in_struct => {
                        if !self.activity_id.is_empty() {
                            errors.add(option_ident.span(), "activity_id already set");
                        }
                        self.activity_id = Expression::new(
                            option_ident.span(),
                            option_parser
                                .next_tokens(RequiredLast, "expected Activity Id variable"),
                        );
                    }
                    "related_id" if !in_struct => {
                        if !self.related_id.is_empty() {
                            errors.add(option_ident.span(), "related_id already set");
                        }
                        self.related_id = Expression::new(
                            option_ident.span(),
                            option_parser.next_tokens(RequiredLast, "expected Related Id variable"),
                        );
                    }
                    _ => {
                        errors.add(option_ident.span(), "unrecognized option");
                        continue;
                    }
                }
            }
        }

        return logical_fields_added;
    }

    fn push_field(&mut self, errors: &mut Errors, field: FieldInfo) {
        let metadata_size = field.name.len()
            + 1 // name nul-termination
            + if !field.tag.is_empty() {
                4 // encoding + format + tag
            } else if field.format_or_field_count_int != 0 {
                2 // encoding + format
            } else {
                1 // encoding
            };
        self.add_estimated_metadata(errors, field.type_name_span, metadata_size);
        self.fields.push(field);
    }

    fn add_estimated_metadata(&mut self, errors: &mut Errors, span: Span, size: usize) {
        if self.metadata_bytes_used == 0 {
            // Already emitted an error for this. Don't emit another.
        } else if (METADATA_BYTES_MAX - self.metadata_bytes_used) as usize >= size {
            self.metadata_bytes_used += size as u16;
        } else {
            self.metadata_bytes_used = 0; // Don't give any additional size errors.
            errors.add(
                span,
                "event metadata is too large (includes event name string, field name strings, and field type codes; limit is 65535 bytes)");
        }
    }
}

fn expected_enum_message(
    enum_name: &str,
    suggested_string_value: &str,
    suggested_integer_value: u8,
) -> String {
    return format!(
        "expected {0} value, e.g. {1}, tracelogging::{0}::{1}, or {2}",
        enum_name, suggested_string_value, suggested_integer_value,
    );
}

fn filter_enum_tokens(
    tokens: TokenStream,
    enum_name: &str,
    known_values: &[&str],
    option_name_span: Span,
    scratch_tree: &mut Tree,
) -> TokenStream {
    #[cfg(debug_assertions)]
    for i in 1..known_values.len() {
        debug_assert!(known_values[i - 1] < known_values[i]);
    }

    let str = tokens.to_string();
    return if !str.is_empty() && str.as_bytes()[0].is_ascii_digit() {
        // If it starts with a number, wrap it in from_int.
        scratch_tree
            .push_span(option_name_span)
            .add_path_call(&["eventheader", enum_name, "from_int"], tokens)
            .pop_span()
            .drain()
            .collect()
    } else if known_values.binary_search(&str.as_str()).is_ok() {
        // If it's an unqualified known enum value, fully-qualify it.
        scratch_tree
            .push_span(option_name_span)
            .add_path(&["eventheader", enum_name, &str])
            .pop_span()
            .drain()
            .collect()
    } else {
        tokens
    };
}
