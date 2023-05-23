// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use eventheader_dynamic::*;

#[test]
fn provider() {
    println!("{:?}", Provider::new_options().group_name("mygroup"));

    let provider = Provider::new(
        "MyCompany_MyComponent",
        Provider::new_options().group_name("mygroup"),
    );
    assert_eq!(provider.name(), "MyCompany_MyComponent");
    assert_eq!(provider.options(), "Gmygroup");

    let mut provider = Provider::new("Hello", &Provider::new_options());
    assert_eq!(provider.name(), "Hello");
    assert_eq!(provider.options(), "");

    provider.unregister();

    let mut provider = Provider::new("MyCompany_MyComponent", &Provider::new_options());
    let es_l5k123 = provider.register_set(Level::Verbose, 0x123);
    _ = es_l5k123.enabled();

    provider.unregister();
    assert!(!es_l5k123.enabled());

    let mut b = EventBuilder::new();

    let mut provider = Provider::new(
        "EventHeaderDynamicTest",
        Provider::new_options().group_name("testgroup"),
    );

    let aid = uuid::uuid!("7ae27afb-11cf-4edd-8b15-9997ec20e0fc");
    let rid = &uuid::uuid!("848292a8-7cae-45b8-b3e6-ad51a6433d11");

    let es_l5k1 = provider.register_set(Level::Verbose, 0x1);
    b.reset("GroupEvent-Start", 0)
        .opcode(Opcode::ActivityStart)
        .write(&es_l5k1, Some(aid.as_bytes()), Some(rid.as_bytes()));
    b.reset("GroupEvent-Stop", 0)
        .opcode(Opcode::ActivityStop)
        .write(&es_l5k1, Some(aid.as_bytes()), None);
}

#[test]
fn builder() {
    let mut p = Provider::new("EventHeaderDynamicTest", &Provider::new_options());
    let mut b = EventBuilder::new();
    println!("{:?}", b);

    let es_l5k1 = p.register_set(Level::Verbose, 0x1);

    b.reset("Default", 0).write(&es_l5k1, None, None);

    let es_l3k11 = p.register_set(Level::Warning, 0x11);
    b.reset("4v2o6l3k11", 0)
        .id_version(4, 2)
        .opcode(Opcode::Reply)
        .write(&es_l3k11, None, None);

    b.reset("tag0xFE", 0xFE).write(&es_l5k1, None, None);
    b.reset("tag0xFEDC", 0xFEDC).write(&es_l5k1, None, None);

    b.reset("fieldtag", 0)
        .add_value("0xFE", 0u8, FieldFormat::Default, 0xFE)
        .add_value("0xFEDC", 0u8, FieldFormat::Default, 0xFEDC)
        .write(&es_l5k1, None, None);

    b.reset("outtypes", 0)
        .add_value("default100", 100u8, FieldFormat::Default, 0)
        .add_value("string65", 65u8, FieldFormat::String8, 0)
        .add_value("bool1", 1u8, FieldFormat::Boolean, 0)
        .add_value("hex100", 100u8, FieldFormat::HexInt, 0)
        .write(&es_l5k1, None, None);

    b.reset("structs", 0)
        .add_value("start", 0u8, FieldFormat::Default, 0)
        .add_struct("struct1", 2, 0xFE)
        .add_value("nested1", 1u8, FieldFormat::Default, 0)
        .add_struct("struct2", 1, 0)
        .add_value("nested2", 2u8, FieldFormat::Default, 0)
        .write(&es_l5k1, None, None);

    b.reset("cstrs", 0)
        .add_value("A", 65u8, FieldFormat::String8, 0)
        .add_cstr("cstr16-", to_utf16("").as_slice(), FieldFormat::Default, 0)
        .add_cstr(
            "cstr16-a",
            to_utf16("a").as_slice(),
            FieldFormat::Default,
            0,
        )
        .add_cstr(
            "cstr16-0",
            to_utf16("\0").as_slice(),
            FieldFormat::Default,
            0,
        )
        .add_cstr(
            "cstr16-a0",
            to_utf16("a\0").as_slice(),
            FieldFormat::Default,
            0,
        )
        .add_cstr(
            "cstr16-0a",
            to_utf16("\0a").as_slice(),
            FieldFormat::Default,
            0,
        )
        .add_cstr(
            "cstr16-a0a",
            to_utf16("a\0a").as_slice(),
            FieldFormat::Default,
            0,
        )
        .add_cstr("cstr8-", "".as_bytes(), FieldFormat::Default, 0)
        .add_cstr("cstr8-a", "a".as_bytes(), FieldFormat::Default, 0)
        .add_cstr("cstr8-0", "\0".as_bytes(), FieldFormat::Default, 0)
        .add_cstr("cstr8-a0", "a\0".as_bytes(), FieldFormat::Default, 0)
        .add_cstr("cstr8-0a", "\0a".as_bytes(), FieldFormat::Default, 0)
        .add_cstr("cstr8-a0a", "a\0a".as_bytes(), FieldFormat::Default, 0)
        .add_value("A", 65u8, FieldFormat::String8, 0)
        .write(&es_l5k1, None, None);

    validate(
        &p,
        &mut b,
        "UnicodeString",
        to_utf16("zutf16").as_slice(),
        |b, n, v, o, t| {
            b.add_cstr(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_cstr_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "AnsiString",
        "zutf8".as_bytes(),
        |b, n, v, o, t| {
            b.add_cstr(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_cstr_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "Int8",
        -8i8,
        |b, n, v, o, t| {
            b.add_value(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_value_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "UInt8",
        8u8,
        |b, n, v, o, t| {
            b.add_value(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_value_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "Int16",
        -16i16,
        |b, n, v, o, t| {
            b.add_value(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_value_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "UInt16",
        16u16,
        |b, n, v, o, t| {
            b.add_value(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_value_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "Int32",
        -32i32,
        |b, n, v, o, t| {
            b.add_value(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_value_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "UInt32",
        32u32,
        |b, n, v, o, t| {
            b.add_value(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_value_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "Int64",
        -64i64,
        |b, n, v, o, t| {
            b.add_value(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_value_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "UInt64",
        64u64,
        |b, n, v, o, t| {
            b.add_value(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_value_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "IntPtr",
        -3264isize,
        |b, n, v, o, t| {
            b.add_value(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_value_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "UIntPtr",
        3264usize,
        |b, n, v, o, t| {
            b.add_value(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_value_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "Float32",
        3.2f32,
        |b, n, v, o, t| {
            b.add_value(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_value_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "Float64",
        6.4f64,
        |b, n, v, o, t| {
            b.add_value(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_value_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "Bool8",
        false,
        |b, n, v, o, t| {
            b.add_value(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_value_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "Bool8",
        true,
        |b, n, v, o, t| {
            b.add_value(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_value_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "Guid",
        *uuid::uuid!("848292a8-7cae-45b8-b3e6-ad51a6433d11").as_bytes(),
        |b, n, v, o, t| {
            b.add_value(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_value_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "CountedString",
        to_utf16("utf16").as_slice(),
        |b, n, v, o, t| {
            b.add_str(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_str_sequence(n, v, o, t);
        },
    );
    validate(
        &p,
        &mut b,
        "CountedAnsiString",
        "utf8".as_bytes(),
        |b, n, v, o, t| {
            b.add_str(n, v, o, t);
        },
        |b, n, v, o, t| {
            b.add_str_sequence(n, v, o, t);
        },
    );
}

fn to_utf16(s: &str) -> Vec<u16> {
    Vec::from_iter(s.encode_utf16())
}

fn validate<T: Copy>(
    p: &Provider,
    b: &mut EventBuilder,
    name: &str,
    value: T,
    scalar: impl Fn(&mut EventBuilder, &str, T, FieldFormat, u16),
    array: impl Fn(&mut EventBuilder, &str, &[T], FieldFormat, u16),
) {
    let values = [value, value];
    let es = p.find_set(Level::Verbose, 0x1).unwrap();
    b.reset(name, 0);
    b.add_value("A", b'A', FieldFormat::String8, 0);
    scalar(b, "scalar", value, FieldFormat::Default, 0);
    array(b, "a0", &values[0..0], FieldFormat::Default, 0);
    array(b, "a1", &values[0..1], FieldFormat::Default, 0);
    array(b, "a2", &values, FieldFormat::Default, 0);
    b.add_value("A", b'A', FieldFormat::String8, 0);
    b.write(&es, None, None);
}
