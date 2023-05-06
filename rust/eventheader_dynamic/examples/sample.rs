// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use ehd::FieldFormat;
use eventheader_dynamic as ehd;

fn main() {
    let mut prov1 = ehd::Provider::new(
        "MyProv_1",
        ehd::Provider::new_options().group_name("mygroup"),
    );
    println!(
        "prov1: name=\"{}\", options=\"{}\", dbg={:?}",
        prov1.name(),
        prov1.options(),
        prov1
    );

    let e1 = prov1.find_set(ehd::Level::Verbose, 1);
    println!("e1: {:?}", e1);
    let e1 = prov1.register_set(ehd::Level::Verbose, 1);
    println!("e1: {:?}", e1);
    let e1 = prov1.find_set(ehd::Level::Verbose, 1).unwrap();
    println!("e1: {:?}", e1);
    ehd::EventBuilder::new()
        .reset("MyEvent1", 123)
        .add_str("MyField", "Value", FieldFormat::String8, 1234)
        .add_value("F", 0xF, FieldFormat::HexBytes, 0)
        .write(&e1, None, None);

    prov1.unregister();

    println!("e1: {:?}", e1);
    ehd::EventBuilder::new().write(&e1, None, None);

    let prov2 = ehd::Provider::new("MyProv_2", &ehd::Provider::new_options());
    println!(
        "prov2: name=\"{}\", options=\"{}\", dbg={:?}",
        prov2.name(),
        prov2.options(),
        prov2
    );
}
