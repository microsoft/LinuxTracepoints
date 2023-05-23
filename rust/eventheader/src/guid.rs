// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

use core::borrow;
use core::convert::TryInto;
use core::fmt;
use core::str::from_utf8;

/// [UUID](https://en.wikipedia.org/wiki/Universally_unique_identifier)
/// with big-endian in-memory representation.
#[repr(C)]
#[derive(Clone, Copy, Default, PartialEq, Eq, Hash, Ord, PartialOrd)]
pub struct Guid([u8; 16]);

impl Guid {
    /// Returns a zeroed GUID, i.e. GUID_NULL.
    pub const fn zero() -> Self {
        return Self([0; 16]);
    }

    /// Returns a GUID generated from a case-insensitive hash of the specified trace
    /// provider name. The hash uses the same algorithm as many Windows tracing tools
    /// and APIs. Given the same name, it will always generate the same GUID.
    /// ```
    /// # use eventheader::Guid;
    /// assert_eq!(
    ///    Guid::from_name("MyProvider"),
    ///    Guid::from_u128(&0xb3864c38_4273_58c5_545b_8b3608343471));
    /// ```
    pub fn from_name(event_provider_name: &str) -> Self {
        let mut hasher = Sha1NonSecret::new();
        hasher.write(&[
            0x48, 0x2C, 0x2D, 0xB2, 0xC3, 0x90, 0x47, 0xC8, 0x87, 0xF8, 0x1A, 0x15, 0xBF, 0xC1,
            0x30, 0xFB,
        ]);

        // Hash name as uppercase UTF-16BE
        let mut u16buf = [0u16; 2];
        for upper_ch in event_provider_name.chars().flat_map(char::to_uppercase) {
            for upper_u16 in upper_ch.encode_utf16(&mut u16buf) {
                hasher.write(&upper_u16.to_be_bytes());
            }
        }

        let mut v = hasher.finish();
        v[7] = (v[7] & 0x0F) | 0x50;
        return Guid::from_bytes_le(v[0..16].try_into().unwrap());
    }

    /// Creates a GUID from field values.
    /// ```
    /// # use eventheader::Guid;
    /// assert_eq!(
    ///     Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]),
    ///     Guid::from_bytes_be(&[0xa3, 0xa2, 0xa1, 0xa0, 0xb1, 0xb0, 0xc1, 0xc0, 0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]));
    /// ```
    pub const fn from_fields(data1: u32, data2: u16, data3: u16, data4: [u8; 8]) -> Self {
        let b1 = data1.to_be_bytes();
        let b2 = data2.to_be_bytes();
        let b3 = data3.to_be_bytes();
        let b4 = &data4;
        return Self([
            b1[0], b1[1], b1[2], b1[3], b2[0], b2[1], b3[0], b3[1], b4[0], b4[1], b4[2], b4[3],
            b4[4], b4[5], b4[6], b4[7],
        ]);
    }

    /// Creates a GUID from bytes in big-endian (RFC) byte order.
    /// ```
    /// # use eventheader::Guid;
    /// assert_eq!(
    ///     Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]),
    ///     Guid::from_bytes_be(&[0xa3, 0xa2, 0xa1, 0xa0, 0xb1, 0xb0, 0xc1, 0xc0, 0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]));
    /// ```
    pub const fn from_bytes_be(bytes_be: &[u8; 16]) -> Self {
        return Self(*bytes_be);
    }

    /// Creates a GUID from bytes in little-endian byte order.
    /// ```
    /// # use eventheader::Guid;
    /// assert_eq!(
    ///     Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]),
    ///     Guid::from_bytes_le(&[0xa0, 0xa1, 0xa2, 0xa3, 0xb0, 0xb1, 0xc0, 0xc1, 0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]));
    /// ```
    pub const fn from_bytes_le(bytes_le: &[u8; 16]) -> Self {
        let b = bytes_le;
        return Self([
            b[3], b[2], b[1], b[0], b[5], b[4], b[7], b[6], b[8], b[9], b[10], b[11], b[12], b[13],
            b[14], b[15],
        ]);
    }

    /// Creates a GUID from a u128 value.
    /// ```
    /// # use eventheader::Guid;
    /// assert_eq!(
    ///     Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]),
    ///     Guid::from_u128(&0xa3a2a1a0_b1b0_c1c0_d7d6d5d4d3d2d1d0));
    /// ```
    pub const fn from_u128(value: &u128) -> Self {
        return Self(value.to_be_bytes());
    }

    /// Creates a GUID from a string with optional {} and optional '-'.
    /// Returns None if GUID could not be parsed from the input.
    /// ```
    /// # use eventheader::Guid;
    /// assert_eq!(
    ///     Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]),
    ///     Guid::try_parse("{a3a2a1a0-b1b0-c1c0-d7d6-d5d4d3d2d1d0}").unwrap());
    /// assert_eq!(
    ///     Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]),
    ///     Guid::try_parse("a3a2a1a0-b1b0-c1c0-d7d6-d5d4d3d2d1d0").unwrap());
    /// assert_eq!(
    ///     Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]),
    ///     Guid::try_parse("a3a2a1a0b1b0c1c0d7d6d5d4d3d2d1d0").unwrap());
    /// ```
    pub fn try_parse(value: &str) -> Option<Self> {
        return Self::try_parse_ascii(value.as_bytes());
    }

    /// Creates a GUID from a string with optional {} and optional '-'.
    /// Returns None if GUID could not be parsed from the input.
    /// ```
    /// # use eventheader::Guid;
    /// assert_eq!(
    ///     Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]),
    ///     Guid::try_parse_ascii(b"{a3a2a1a0-b1b0-c1c0-d7d6-d5d4d3d2d1d0}").unwrap());
    /// assert_eq!(
    ///     Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]),
    ///     Guid::try_parse_ascii(b"a3a2a1a0-b1b0-c1c0-d7d6-d5d4d3d2d1d0").unwrap());
    /// assert_eq!(
    ///     Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]),
    ///     Guid::try_parse_ascii(b"a3a2a1a0b1b0c1c0d7d6d5d4d3d2d1d0").unwrap());
    /// ```
    pub fn try_parse_ascii(value: &[u8]) -> Option<Self> {
        if value.len() < 32 {
            return None;
        }

        let mut state = GuidParseState {
            input: if value[0] != b'{' || value[value.len() - 1] != b'}' {
                value
            } else {
                &value[1..value.len() - 1]
            },
            pos: 0,
            dash: 0,
            highbits: 0,
        };

        if (state.input.len() == 36)
            & (state.input[8] == b'-')
            & (state.input[13] == b'-')
            & (state.input[18] == b'-')
            & (state.input[23] == b'-')
        {
            state.dash = 1;
        } else if state.input.len() != 32 {
            return None;
        }

        let b = [
            state.next(),
            state.next(),
            state.next(),
            state.next(),
            state.next_dash(),
            state.next(),
            state.next_dash(),
            state.next(),
            state.next_dash(),
            state.next(),
            state.next_dash(),
            state.next(),
            state.next(),
            state.next(),
            state.next(),
            state.next(),
        ];

        if (state.highbits & 0xFFFFFF00) != 0 {
            return None;
        }

        return Some(Self::from_bytes_be(&b));
    }

    /// Returns the field values of the GUID as a tuple.
    /// ```
    /// # use eventheader::Guid;
    /// assert_eq!(
    ///     Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]).to_fields(),
    ///     (0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]));
    /// ```
    #[allow(clippy::wrong_self_convention)]
    pub const fn to_fields(&self) -> (u32, u16, u16, [u8; 8]) {
        return (
            u32::from_be_bytes([self.0[0], self.0[1], self.0[2], self.0[3]]),
            u16::from_be_bytes([self.0[4], self.0[5]]),
            u16::from_be_bytes([self.0[6], self.0[7]]),
            [
                self.0[8], self.0[9], self.0[10], self.0[11], self.0[12], self.0[13], self.0[14],
                self.0[15],
            ],
        );
    }

    /// Returns this implementation's in-memory byte representation.
    /// ```
    /// # use eventheader::Guid;
    /// assert_eq!(
    ///     *Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]).as_bytes_raw(),
    ///     [0xa3, 0xa2, 0xa1, 0xa0, 0xb1, 0xb0, 0xc1, 0xc0, 0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]);
    /// ```
    pub const fn as_bytes_raw(&self) -> &[u8; 16] {
        return &self.0;
    }

    /// Returns the bytes of the GUID in RFC byte order (big-endian).
    /// ```
    /// # use eventheader::Guid;
    /// assert_eq!(
    ///     Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]).to_bytes_be(),
    ///     [0xa3, 0xa2, 0xa1, 0xa0, 0xb1, 0xb0, 0xc1, 0xc0, 0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]);
    /// ```
    #[allow(clippy::wrong_self_convention)]
    pub const fn to_bytes_be(&self) -> [u8; 16] {
        return self.0;
    }

    /// Returns the bytes of the GUID in Windows byte order (little-endian).
    /// ```
    /// # use eventheader::Guid;
    /// assert_eq!(
    ///     Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]).to_bytes_le(),
    ///     [0xa0, 0xa1, 0xa2, 0xa3, 0xb0, 0xb1, 0xc0, 0xc1, 0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]);
    /// ```
    #[allow(clippy::wrong_self_convention)]
    pub const fn to_bytes_le(&self) -> [u8; 16] {
        return [
            self.0[3], self.0[2], self.0[1], self.0[0], self.0[5], self.0[4], self.0[7], self.0[6],
            self.0[8], self.0[9], self.0[10], self.0[11], self.0[12], self.0[13], self.0[14],
            self.0[15],
        ];
    }

    /// Returns the GUID as a u128 value.
    /// ```
    /// use eventheader::Guid;
    /// assert_eq!(
    ///     Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]).to_u128(),
    ///     0xa3a2a1a0_b1b0_c1c0_d7d6d5d4d3d2d1d0);
    /// ```
    #[allow(clippy::wrong_self_convention)]
    pub const fn to_u128(&self) -> u128 {
        return u128::from_be_bytes([
            self.0[0], self.0[1], self.0[2], self.0[3], self.0[4], self.0[5], self.0[6], self.0[7],
            self.0[8], self.0[9], self.0[10], self.0[11], self.0[12], self.0[13], self.0[14],
            self.0[15],
        ]);
    }

    /// Convert GUID to utf8 string bytes.
    /// To get a &str, use: `str::from_utf8(&guid.to_utf8_bytes()).unwrap()`.
    /// ```
    /// use eventheader::Guid;
    /// assert_eq!(
    ///     Guid::from_fields(0xa3a2a1a0, 0xb1b0, 0xc1c0, [0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0]).to_utf8_bytes(),
    ///     *b"a3a2a1a0-b1b0-c1c0-d7d6-d5d4d3d2d1d0");
    /// ```
    #[allow(clippy::wrong_self_convention)]
    pub const fn to_utf8_bytes(&self) -> [u8; 36] {
        const HEX_DIGITS: &[u8] = b"0123456789abcdef";
        return [
            HEX_DIGITS[(self.0[0] >> 4) as usize],
            HEX_DIGITS[(self.0[0] & 0xf) as usize],
            HEX_DIGITS[(self.0[1] >> 4) as usize],
            HEX_DIGITS[(self.0[1] & 0xf) as usize],
            HEX_DIGITS[(self.0[2] >> 4) as usize],
            HEX_DIGITS[(self.0[2] & 0xf) as usize],
            HEX_DIGITS[(self.0[3] >> 4) as usize],
            HEX_DIGITS[(self.0[3] & 0xf) as usize],
            b'-',
            HEX_DIGITS[(self.0[4] >> 4) as usize],
            HEX_DIGITS[(self.0[4] & 0xf) as usize],
            HEX_DIGITS[(self.0[5] >> 4) as usize],
            HEX_DIGITS[(self.0[5] & 0xf) as usize],
            b'-',
            HEX_DIGITS[(self.0[6] >> 4) as usize],
            HEX_DIGITS[(self.0[6] & 0xf) as usize],
            HEX_DIGITS[(self.0[7] >> 4) as usize],
            HEX_DIGITS[(self.0[7] & 0xf) as usize],
            b'-',
            HEX_DIGITS[(self.0[8] >> 4) as usize],
            HEX_DIGITS[(self.0[8] & 0xf) as usize],
            HEX_DIGITS[(self.0[9] >> 4) as usize],
            HEX_DIGITS[(self.0[9] & 0xf) as usize],
            b'-',
            HEX_DIGITS[(self.0[10] >> 4) as usize],
            HEX_DIGITS[(self.0[10] & 0xf) as usize],
            HEX_DIGITS[(self.0[11] >> 4) as usize],
            HEX_DIGITS[(self.0[11] & 0xf) as usize],
            HEX_DIGITS[(self.0[12] >> 4) as usize],
            HEX_DIGITS[(self.0[12] & 0xf) as usize],
            HEX_DIGITS[(self.0[13] >> 4) as usize],
            HEX_DIGITS[(self.0[13] & 0xf) as usize],
            HEX_DIGITS[(self.0[14] >> 4) as usize],
            HEX_DIGITS[(self.0[14] & 0xf) as usize],
            HEX_DIGITS[(self.0[15] >> 4) as usize],
            HEX_DIGITS[(self.0[15] & 0xf) as usize],
        ];
    }
}

impl fmt::Debug for Guid {
    /// Format the GUID, e.g. "a3a2a1a0-b1b0-c1c0-d7d6-d5d4d3d2d1d0".
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        return f.write_str(from_utf8(&self.to_utf8_bytes()).unwrap());
    }
}

impl borrow::Borrow<[u8; 16]> for Guid {
    /// Returns this implementation's in-memory byte representation.
    fn borrow(&self) -> &[u8; 16] {
        return &self.0;
    }
}

struct GuidParseState<'a> {
    input: &'a [u8],
    pos: usize,
    dash: usize,
    highbits: u32,
}

impl GuidParseState<'_> {
    fn next(&mut self) -> u8 {
        let val1 = Self::hex_to_u4(self.input[self.pos]);
        let val2 = Self::hex_to_u4(self.input[self.pos + 1]);
        self.pos += 2;
        let val = (val1 << 4) | val2;
        self.highbits |= val;
        return val as u8;
    }

    fn next_dash(&mut self) -> u8 {
        self.pos += self.dash;
        return self.next();
    }

    fn hex_to_u4(ch: u8) -> u32 {
        let hexval;
        let mut index = ch.wrapping_sub(48);
        if index < 10 {
            hexval = index as u32;
        } else {
            index = (ch | 32).wrapping_sub(97);
            hexval = if index < 6 { (index + 10) as u32 } else { 256 };
        }
        return hexval;
    }
}

/// Single-use SHA1 hasher (finish() is destructive). Note that this implementation
/// is for hashing public information. Do not use this code to hash private data
/// as this implementation does not take any steps to avoid information disclosure
/// (i.e. does not scrub its buffers).
struct Sha1NonSecret {
    chunk: [u8; 64],  // Each chunk is 64 bytes.
    chunk_count: u32, // Implementation limited to 2^32-1 chunks = 255GB.
    chunk_pos: u8,
    results: [u32; 5],
}

impl Sha1NonSecret {
    pub fn new() -> Sha1NonSecret {
        return Self {
            chunk: [0; 64],
            chunk_count: 0,
            chunk_pos: 0,
            results: [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0],
        };
    }

    pub fn write_u8(&mut self, val: u8) {
        self.chunk[self.chunk_pos as usize] = val;
        self.chunk_pos = (self.chunk_pos + 1) & 63;
        if self.chunk_pos == 0 {
            self.drain();
        }
    }

    pub fn write(&mut self, bytes: &[u8]) {
        for i in bytes {
            self.write_u8(*i);
        }
    }

    pub fn finish(&mut self) -> [u8; 20] {
        // Need to capture chunk_count before we add end-bit and zerofill.
        let total_bit_count = (self.chunk_count as u64 * 512) + (self.chunk_pos as u64 * 8);

        // Add end-bit
        self.write_u8(0x80);

        // Zero-fill until almost to end of chunk.
        while self.chunk_pos != 56 {
            self.write_u8(0);
        }

        // End chunk with total bit count.
        self.write(&total_bit_count.to_be_bytes());
        debug_assert_eq!(self.chunk_pos, 0, "Bug: write should have drained");

        let mut sha1 = [0u8; 20];
        for i in 0..5 {
            sha1[(i * 4)..(i * 4 + 4)].copy_from_slice(&self.results[i].to_be_bytes());
        }

        return sha1;
    }

    fn drain(&mut self) {
        let mut w = [0u32; 80];

        let mut wpos = 0;
        while wpos != 16 {
            w[wpos] = u32::from_be_bytes([
                self.chunk[wpos * 4],
                self.chunk[wpos * 4 + 1],
                self.chunk[wpos * 4 + 2],
                self.chunk[wpos * 4 + 3],
            ]);
            wpos += 1;
        }

        while wpos != 80 {
            w[wpos] = (w[wpos - 3] ^ w[wpos - 8] ^ w[wpos - 14] ^ w[wpos - 16]).rotate_left(1);
            wpos += 1;
        }

        let mut a = self.results[0];
        let mut b = self.results[1];
        let mut c = self.results[2];
        let mut d = self.results[3];
        let mut e = self.results[4];

        wpos = 0;
        while wpos != 20 {
            const K: u32 = 0x5A827999;
            let f = (b & c) | (!b & d);
            let temp = a
                .rotate_left(5)
                .wrapping_add(f)
                .wrapping_add(e)
                .wrapping_add(K)
                .wrapping_add(w[wpos]);
            e = d;
            d = c;
            c = b.rotate_left(30);
            b = a;
            a = temp;
            wpos += 1;
        }

        while wpos != 40 {
            const K: u32 = 0x6ED9EBA1;
            let f = b ^ c ^ d;
            let temp = a
                .rotate_left(5)
                .wrapping_add(f)
                .wrapping_add(e)
                .wrapping_add(K)
                .wrapping_add(w[wpos]);
            e = d;
            d = c;
            c = b.rotate_left(30);
            b = a;
            a = temp;
            wpos += 1;
        }

        while wpos != 60 {
            const K: u32 = 0x8F1BBCDC;
            let f = (b & c) | (b & d) | (c & d);
            let temp = a
                .rotate_left(5)
                .wrapping_add(f)
                .wrapping_add(e)
                .wrapping_add(K)
                .wrapping_add(w[wpos]);
            e = d;
            d = c;
            c = b.rotate_left(30);
            b = a;
            a = temp;
            wpos += 1;
        }

        while wpos != 80 {
            const K: u32 = 0xCA62C1D6;
            let f = b ^ c ^ d;
            let temp = a
                .rotate_left(5)
                .wrapping_add(f)
                .wrapping_add(e)
                .wrapping_add(K)
                .wrapping_add(w[wpos]);
            e = d;
            d = c;
            c = b.rotate_left(30);
            b = a;
            a = temp;
            wpos += 1;
        }

        self.results[0] = self.results[0].wrapping_add(a);
        self.results[1] = self.results[1].wrapping_add(b);
        self.results[2] = self.results[2].wrapping_add(c);
        self.results[3] = self.results[3].wrapping_add(d);
        self.results[4] = self.results[4].wrapping_add(e);
        self.chunk_count += 1;
    }
}
