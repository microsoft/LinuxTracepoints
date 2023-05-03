// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use eh::_internal::EventDataDescriptor;
use eventheader as eh;
use std::ffi;
use std::pin::pin;

fn main() {
    // Various use cases for the TracepointState struct.

    let tp1 = pin!(eh::_internal::TracepointState::new(0));
    let tp2 = pin!(eh::_internal::TracepointState::new(0));
    let value = 42u32;
    let mut vecs: [EventDataDescriptor; 2] = [
        EventDataDescriptor::default(),
        EventDataDescriptor::from_value(&value),
    ];

    println!("-------------------------");

    let result = unsafe {
        tp1.as_ref()
            .register(ffi::CStr::from_bytes_with_nul_unchecked(
                b"simple_tracepoint u32 field1\0",
            ))
    };
    println!("Register tp1 {}: {}", tp1.enabled(), result);

    let result = tp1.write(&mut vecs);
    println!("Write tp1 {}: {}", tp1.enabled(), result);
    let result = tp2.write(&mut vecs);
    println!("Write tp2 {}: {}", tp2.enabled(), result);

    let result = tp1.unregister();
    println!("Unregister tp1 {}: {}", tp1.enabled(), result);
    let result = tp2.unregister();
    println!("Unregister tp2 {}: {}", tp2.enabled(), result);

    let result = tp1.write(&mut vecs);
    println!("Write tp1 {}: {}", tp1.enabled(), result);
    let result = tp2.write(&mut vecs);
    println!("Write tp2 {}: {}", tp2.enabled(), result);

    let result = tp1.unregister();
    println!("Unregister tp1 {}: {}", tp1.enabled(), result);
    let result = tp2.unregister();
    println!("Unregister tp2 {}: {}", tp2.enabled(), result);

    let result = unsafe {
        tp1.as_ref()
            .register(ffi::CStr::from_bytes_with_nul_unchecked(
                b"simple_tracepoint u32 field1\0",
            ))
    };
    println!("Register tp1 {}: {}", tp1.enabled(), result);

    let result = tp1.write(&mut vecs);
    println!("Write tp1 {}: {}", tp1.enabled(), result);
    let result = tp2.write(&mut vecs);
    println!("Write tp2 {}: {}", tp2.enabled(), result);

    let result = tp1.unregister();
    println!("Unregister tp1 {}: {}", tp1.enabled(), result);
    let result = tp2.unregister();
    println!("Unregister tp2 {}: {}", tp2.enabled(), result);

    let result = tp1.write(&mut vecs);
    println!("Write tp1 {}: {}", tp1.enabled(), result);
    let result = tp2.write(&mut vecs);
    println!("Write tp2 {}: {}", tp2.enabled(), result);

    let result = tp1.unregister();
    println!("Unregister tp1 {}: {}", tp1.enabled(), result);
    let result = tp2.unregister();
    println!("Unregister tp2 {}: {}", tp2.enabled(), result);
}
