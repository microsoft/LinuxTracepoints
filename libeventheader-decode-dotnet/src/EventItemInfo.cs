// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

using System;
using CultureInfo = System.Globalization.CultureInfo;
using Debug = System.Diagnostics.Debug;
using IPAddress = System.Net.IPAddress;
using TextEncoding = System.Text.Encoding;

namespace EventHeaderDecode
{
    /// <summary>
    /// Event item attributes (attributes of a value, array, or structure within the event).
    /// </summary>
    public struct EventItemInfo
    {
        /// <summary>
        /// UTF-8 encoded "FieldName" followed by 0 or more field attributes.
        /// Each attribute is ";AttribName=AttribValue".
        /// FieldName should not contain ';'.
        /// AttribName should not contain ';' or '='.
        /// AttribValue may contain ";;" which should be unescaped to ";".
        /// </summary>
        public ArraySegment<byte> NameBytes;

        /// <summary>
        /// Raw field value bytes.
        /// ValueBytes.Count is nonzero for Value items and for ArrayBegin of array of simple values.
        /// ValueBytes.Count is zero for everything else, including ArrayBegin of array of complex items.
        /// For strings, ValueBytes does not include length prefix or NUL termination.
        /// </summary>
        public ArraySegment<byte> ValueBytes;

        /// <summary>
        /// Array element index.
        /// For non-array, this is 0.
        /// For ArrayBegin, this is 0.
        /// For ArrayEnd, this is the same as ArrayCount.
        /// </summary>
        public ushort ArrayIndex;

        /// <summary>
        /// Array element count. For non-array, this is 1.
        /// This may be 0 in the case of variable-length array.
        /// </summary>
        public ushort ArrayCount;

        /// <summary>
        /// Nonzero for simple items (fixed-size non-struct).
        /// Zero for complex items (variable-size or struct).
        /// </summary>
        public byte ElementSize;

        /// <summary>
        /// Field's underlying encoding. The encoding indicates how to determine the field's
        /// size and the semantic type to use when Format = Default.
        /// </summary>
        public EventFieldEncoding Encoding;

        /// <summary>
        /// Field's semantic type. May be Default, in which case the semantic type should be
        /// determined based on the default format for the field's encoding.
        /// For StructBegin/StructEnd, this contains the struct field count.
        /// </summary>
        public EventFieldFormat Format;

        /// <summary>
        /// 0 if item is not an ArrayBegin, ArrayEnd, or array Value.
        /// FlagCArray or FlagVArray if item is an ArrayBegin, ArrayEnd, or array Value.
        /// </summary>
        public EventFieldEncoding ArrayFlags;

        /// <summary>
        /// Field tag, or 0 if none.
        /// </summary>
        public ushort FieldTag;

        /// <summary>
        /// Gets a new string (decoded from NameBytes) containing
        /// "FieldName" followed by 0 or more field attributes.
        /// Each attribute is ";AttribName=AttribValue".
        /// FieldName should not contain ';'.
        /// AttribName should not contain ';' or '='.
        /// AttribValue may contain ";;" which should be unescaped to ";".
        /// </summary>
        public string Name
        {
            get
            {
                return EventUtility.NameFromBytes(this.NameBytes);
            }
        }

        /// <summary>
        /// Attempts to convert a Unix time_t (signed seconds since 1970) to a DateTime.
        /// Returns false if the result is less than DateTime.MinValue or greater than
        /// DateTime.MaxValue.
        /// </summary>
        public static bool TryDateTimeFromUnixTimeSeconds(long secondsSince1970, out DateTime value)
        {
            const long UnixEpochSeconds = 62135596800;
            const long DaysToYear10000 = 3652059;
            const long SecondsPerDay = 60 * 60 * 24;
            const long MaxSeconds = DaysToYear10000 * SecondsPerDay - UnixEpochSeconds;

            if (secondsSince1970 < -UnixEpochSeconds ||
                secondsSince1970 >= MaxSeconds)
            {
                value = DateTime.MinValue;
                return false;
            }
            else
            {
                value = new DateTime((secondsSince1970 + UnixEpochSeconds) * 10000000, DateTimeKind.Utc);
                return true;
            }
        }

        /// <summary>
        /// Converts a Unix time_t (signed seconds since 1970) to a DateTime.
        /// Throws ArgumentOutOfRangeException if the result is less than
        /// DateTime.MinValue or greater than DateTime.MaxValue.
        /// </summary>
        /// <exception cref="ArgumentOutOfRangeException">secondsSince1970 out of range.</exception>
        public static DateTime DateTimeFromUnixTimeSeconds(long secondsSince1970)
        {
            if (!TryDateTimeFromUnixTimeSeconds(secondsSince1970, out var value))
            {
                throw new ArgumentOutOfRangeException(nameof(secondsSince1970) + " out of range.");
            }

            return value;
        }

        /// <summary>
        /// Converts a Unix time_t (signed seconds since 1970) to a string.
        /// If year is in range 0001..9999, returns a value like "2020-02-02T02:02:02".
        /// If year is outside of 0001..9999, returns a value like "TIME(-1234567890)".
        /// </summary>
        public static string StringFromUnixTimeSeconds(long secondsSince1970)
        {
            return TryDateTimeFromUnixTimeSeconds(secondsSince1970, out var value)
                ? value.ToString("s", CultureInfo.InvariantCulture)
                : "TIME(" + secondsSince1970.ToString(CultureInfo.InvariantCulture) + ")";
        }

        /// <summary>
        /// Returns the specified Linux errno value formatted as a string.
        /// If value is a known errno value, returns a value like "EPERM(1)".
        /// If value is not a known errno value, returns a value like "ERRNO(404)".
        /// </summary>
        public static string StringFromLinuxErrno(int errno)
        {
            switch (errno)
            {
                default: return "ERRNO(" + errno.ToString(CultureInfo.InvariantCulture) + ")";
                case 1: return "EPERM(1)";
                case 2: return "ENOENT(2)";
                case 3: return "ESRCH(3)";
                case 4: return "EINTR(4)";
                case 5: return "EIO(5)";
                case 6: return "ENXIO(6)";
                case 7: return "E2BIG(7)";
                case 8: return "ENOEXEC(8)";
                case 9: return "EBADF(9)";
                case 10: return "ECHILD(10)";
                case 11: return "EAGAIN(11)";
                case 12: return "ENOMEM(12)";
                case 13: return "EACCES(13)";
                case 14: return "EFAULT(14)";
                case 15: return "ENOTBLK(15)";
                case 16: return "EBUSY(16)";
                case 17: return "EEXIST(17)";
                case 18: return "EXDEV(18)";
                case 19: return "ENODEV(19)";
                case 20: return "ENOTDIR(20)";
                case 21: return "EISDIR(21)";
                case 22: return "EINVAL(22)";
                case 23: return "ENFILE(23)";
                case 24: return "EMFILE(24)";
                case 25: return "ENOTTY(25)";
                case 26: return "ETXTBSY(26)";
                case 27: return "EFBIG(27)";
                case 28: return "ENOSPC(28)";
                case 29: return "ESPIPE(29)";
                case 30: return "EROFS(30)";
                case 31: return "EMLINK(31)";
                case 32: return "EPIPE(32)";
                case 33: return "EDOM(33)";
                case 34: return "ERANGE(34)";
                case 35: return "EDEADLK(35)";
                case 36: return "ENAMETOOLONG(36)";
                case 37: return "ENOLCK(37)";
                case 38: return "ENOSYS(38)";
                case 39: return "ENOTEMPTY(39)";
                case 40: return "ELOOP(40)";
                case 42: return "ENOMSG(42)";
                case 43: return "EIDRM(43)";
                case 44: return "ECHRNG(44)";
                case 45: return "EL2NSYNC(45)";
                case 46: return "EL3HLT(46)";
                case 47: return "EL3RST(47)";
                case 48: return "ELNRNG(48)";
                case 49: return "EUNATCH(49)";
                case 50: return "ENOCSI(50)";
                case 51: return "EL2HLT(51)";
                case 52: return "EBADE(52)";
                case 53: return "EBADR(53)";
                case 54: return "EXFULL(54)";
                case 55: return "ENOANO(55)";
                case 56: return "EBADRQC(56)";
                case 57: return "EBADSLT(57)";
                case 59: return "EBFONT(59)";
                case 60: return "ENOSTR(60)";
                case 61: return "ENODATA(61)";
                case 62: return "ETIME(62)";
                case 63: return "ENOSR(63)";
                case 64: return "ENONET(64)";
                case 65: return "ENOPKG(65)";
                case 66: return "EREMOTE(66)";
                case 67: return "ENOLINK(67)";
                case 68: return "EADV(68)";
                case 69: return "ESRMNT(69)";
                case 70: return "ECOMM(70)";
                case 71: return "EPROTO(71)";
                case 72: return "EMULTIHOP(72)";
                case 73: return "EDOTDOT(73)";
                case 74: return "EBADMSG(74)";
                case 75: return "EOVERFLOW(75)";
                case 76: return "ENOTUNIQ(76)";
                case 77: return "EBADFD(77)";
                case 78: return "EREMCHG(78)";
                case 79: return "ELIBACC(79)";
                case 80: return "ELIBBAD(80)";
                case 81: return "ELIBSCN(81)";
                case 82: return "ELIBMAX(82)";
                case 83: return "ELIBEXEC(83)";
                case 84: return "EILSEQ(84)";
                case 85: return "ERESTART(85)";
                case 86: return "ESTRPIPE(86)";
                case 87: return "EUSERS(87)";
                case 88: return "ENOTSOCK(88)";
                case 89: return "EDESTADDRREQ(89)";
                case 90: return "EMSGSIZE(90)";
                case 91: return "EPROTOTYPE(91)";
                case 92: return "ENOPROTOOPT(92)";
                case 93: return "EPROTONOSUPPORT(93)";
                case 94: return "ESOCKTNOSUPPORT(94)";
                case 95: return "EOPNOTSUPP(95)";
                case 96: return "EPFNOSUPPORT(96)";
                case 97: return "EAFNOSUPPORT(97)";
                case 98: return "EADDRINUSE(98)";
                case 99: return "EADDRNOTAVAIL(99)";
                case 100: return "ENETDOWN(100)";
                case 101: return "ENETUNREACH(101)";
                case 102: return "ENETRESET(102)";
                case 103: return "ECONNABORTED(103)";
                case 104: return "ECONNRESET(104)";
                case 105: return "ENOBUFS(105)";
                case 106: return "EISCONN(106)";
                case 107: return "ENOTCONN(107)";
                case 108: return "ESHUTDOWN(108)";
                case 109: return "ETOOMANYREFS(109)";
                case 110: return "ETIMEDOUT(110)";
                case 111: return "ECONNREFUSED(111)";
                case 112: return "EHOSTDOWN(112)";
                case 113: return "EHOSTUNREACH(113)";
                case 114: return "EALREADY(114)";
                case 115: return "EINPROGRESS(115)";
                case 116: return "ESTALE(116)";
                case 117: return "EUCLEAN(117)";
                case 118: return "ENOTNAM(118)";
                case 119: return "ENAVAIL(119)";
                case 120: return "EISNAM(120)";
                case 121: return "EREMOTEIO(121)";
                case 122: return "EDQUOT(122)";
                case 123: return "ENOMEDIUM(123)";
                case 124: return "EMEDIUMTYPE(124)";
                case 125: return "ECANCELED(125)";
                case 126: return "ENOKEY(126)";
                case 127: return "EKEYEXPIRED(127)";
                case 128: return "EKEYREVOKED(128)";
                case 129: return "EKEYREJECTED(129)";
                case 130: return "EOWNERDEAD(130)";
                case 131: return "ENOTRECOVERABLE(131)";
                case 132: return "ERFKILL(132)";
                case 133: return "EHWPOISON(133)";
            }
        }

        /// <summary>
        /// Converts an integer value from a boolean field into a string.
        /// If value is 0/1, returns "False"/"True".
        /// Otherwise, returns value formatted as a signed integer.
        /// Note: input value is UInt32 because Bool8 and Bool16 should not be
        /// sign-extended, i.e. value should come from a call to TryGetUInt32,
        /// not a call to TryGetInt32.
        /// </summary>
        public static string StringFromBoolean(UInt32 value)
        {
            switch (value)
            {
                case 0: return false.ToString(CultureInfo.InvariantCulture);
                case 1: return true.ToString(CultureInfo.InvariantCulture);
                default: return unchecked((int)value).ToString(CultureInfo.InvariantCulture);
            }
        }

        /// <summary>
        /// Gets ValueBytes interpreted as a signed integer.
        /// Returns false if ValueBytes.Count is not 1, 2, or 4.
        /// </summary>
        public bool TryGetInt32(out Int32 value)
        {
            var a = this.ValueBytes.Array;
            var o = this.ValueBytes.Offset;
            switch (this.ValueBytes.Count)
            {
                case 1: value = unchecked((SByte)a[o]); return true;
                case 2: value = BitConverter.ToInt16(a, o); return true;
                case 4: value = BitConverter.ToInt32(a, o); return true;
                default: value = 0; return false;
            }
        }

        /// <summary>
        /// Gets ValueBytes interpreted as an unsigned integer.
        /// Returns false if ValueBytes.Count is not 1, 2, or 4.
        /// </summary>
        public bool TryGetUInt32(out UInt32 value)
        {
            var a = this.ValueBytes.Array;
            var o = this.ValueBytes.Offset;
            switch (this.ValueBytes.Count)
            {
                case 1: value = a[o]; return true;
                case 2: value = BitConverter.ToUInt16(a, o); return true;
                case 4: value = BitConverter.ToUInt32(a, o); return true;
                default: value = 0; return false;
            }
        }

        /// <summary>
        /// Gets ValueBytes interpreted as a signed integer.
        /// Returns false if ValueBytes.Count is not 1, 2, 4, or 8.
        /// </summary>
        public bool TryGetInt64(out Int64 value)
        {
            var a = this.ValueBytes.Array;
            var o = this.ValueBytes.Offset;
            switch (this.ValueBytes.Count)
            {
                case 1: value = unchecked((SByte)a[o]); return true;
                case 2: value = BitConverter.ToInt16(a, o); return true;
                case 4: value = BitConverter.ToInt32(a, o); return true;
                case 8: value = BitConverter.ToInt64(a, o); return true;
                default: value = 0; return false;
            }
        }

        /// <summary>
        /// Gets ValueBytes interpreted as an unsigned integer.
        /// Returns false if ValueBytes.Count is not 1, 2, 4, or 8.
        /// </summary>
        public bool TryGetUInt64(out UInt64 value)
        {
            var a = this.ValueBytes.Array;
            var o = this.ValueBytes.Offset;
            switch (this.ValueBytes.Count)
            {
                case 1: value = a[o]; return true;
                case 2: value = BitConverter.ToUInt16(a, o); return true;
                case 4: value = BitConverter.ToUInt32(a, o); return true;
                case 8: value = BitConverter.ToUInt64(a, o); return true;
                default: value = 0; return false;
            }
        }

        /// <summary>
        /// Gets ValueBytes interpreted as a 32-bit float.
        /// Returns false if ValueBytes.Count is not 4.
        /// </summary>
        public bool TryGetSingle(out Single value)
        {
            var a = this.ValueBytes.Array;
            var o = this.ValueBytes.Offset;
            switch (this.ValueBytes.Count)
            {
                case sizeof(Single): value = BitConverter.ToSingle(a, o); return true;
                default: value = 0; return false;
            }
        }

        /// <summary>
        /// Gets ValueBytes interpreted as a 32-bit or 64-bit float.
        /// Returns false if ValueBytes.Count is not 4 or 8.
        /// </summary>
        public bool TryGetDouble(out Double value)
        {
            var a = this.ValueBytes.Array;
            var o = this.ValueBytes.Offset;
            switch (this.ValueBytes.Count)
            {
                case sizeof(Single): value = BitConverter.ToSingle(a, o); return true;
                case sizeof(Double): value = BitConverter.ToDouble(a, o); return true;
                default: value = 0; return false;
            }
        }

        /// <summary>
        /// Gets ValueBytes interpreted as a big-endian Guid.
        /// Returns false if ValueBytes.Count is not 16.
        /// </summary>
        public bool TryGetGuid(out Guid value)
        {
            var a = this.ValueBytes.Array;
            var o = this.ValueBytes.Offset;
            switch (this.ValueBytes.Count)
            {
                case 16: value = EventUtility.GuidFromBytes(a, o); return true;
                default: value = new Guid(); return false;
            }
        }

        /// <summary>
        /// Gets ValueBytes interpreted as a big-endian 16-bit integer.
        /// Returns false if ValueBytes.Count is not 2.
        /// </summary>
        public bool TryGetPort(out int value)
        {
            var a = this.ValueBytes.Array;
            var o = this.ValueBytes.Offset;
            switch (this.ValueBytes.Count)
            {
                case sizeof(UInt16): value = (a[o] << 8) | a[o + 1]; return true;
                default: value = 0; return false;
            }
        }

        /// <summary>
        /// Returns ValueBytes interpreted as a Unix time_t (signed seconds since 1970).
        /// Returns false if ValueBytes.Count is not 4 or 8, if time is less than
        /// DateTime.MinValue, or if time is greater than DateTime.MaxValue.
        /// </summary>
        public bool TryGetDateTime(out DateTime value)
        {
            Int64 seconds;
            var a = this.ValueBytes.Array;
            var o = this.ValueBytes.Offset;
            switch (this.ValueBytes.Count)
            {
                case 4: seconds = BitConverter.ToInt32(a, o); break;
                case 8: seconds = BitConverter.ToInt64(a, o); break;
                default: value = new DateTime(); return false;
            }

            return TryDateTimeFromUnixTimeSeconds(seconds, out value);
        }

        /// <summary>
        /// Returns ValueBytes interpreted as an IPAddress.
        /// Returns false if ValueBytes.Count is not 4 (IPv4) or 8 (IPv6).
        /// </summary>
        public bool TryGetIPAddress(out IPAddress value)
        {
            var a = this.ValueBytes.Array;
            var o = this.ValueBytes.Offset;
            var c = this.ValueBytes.Count;

            if (c != 4 && c != 16)
            {
                value = null;
                return false;
            }
            else
            {
                byte[] bytes = new byte[c];
                Array.Copy(a, o, bytes, 0, c);
                value = new IPAddress(bytes);
                return true;
            }
        }

        /// <summary>
        /// Returns ValueBytes interpreted as a signed integer.
        /// Requires ValueBytes.Count is 1, 2, or 4.
        /// </summary>
        /// <exception cref="InvalidOperationException">ValueBytes.Count is not 1, 2, or 4.</exception>
        public Int32 GetInt32()
        {
            Int32 value;
            if (!this.TryGetInt32(out value))
            {
                throw new InvalidOperationException(nameof(GetInt32) + " with bad value size");
            }

            return value;
        }

        /// <summary>
        /// Returns ValueBytes interpreted as an unsigned integer.
        /// Requires ValueBytes.Count is 1, 2, or 4.
        /// </summary>
        /// <exception cref="InvalidOperationException">ValueBytes.Count is not 1, 2, or 4.</exception>
        public UInt32 GetUInt32()
        {
            UInt32 value;
            if (!this.TryGetUInt32(out value))
            {
                throw new InvalidOperationException(nameof(GetUInt32) + " with bad value size");
            }

            return value;
        }

        /// <summary>
        /// Returns ValueBytes interpreted as a signed integer.
        /// Requires ValueBytes.Count is 1, 2, 4, or 8.
        /// </summary>
        /// <exception cref="InvalidOperationException">ValueBytes.Count is not 1, 2, 4, or 8.</exception>
        public Int64 GetInt64()
        {
            Int64 value;
            if (!this.TryGetInt64(out value))
            {
                throw new InvalidOperationException(nameof(GetInt64) + " with bad value size");
            }

            return value;
        }

        /// <summary>
        /// Returns ValueBytes interpreted as an unsigned integer.
        /// Requires ValueBytes.Count is 1, 2, 4, or 8.
        /// </summary>
        /// <exception cref="InvalidOperationException">ValueBytes.Count is not 1, 2, 4, or 8.</exception>
        public UInt64 GetUInt64()
        {
            UInt64 value;
            if (!this.TryGetUInt64(out value))
            {
                throw new InvalidOperationException(nameof(GetUInt64) + " with bad value size");
            }

            return value;
        }

        /// <summary>
        /// Returns ValueBytes interpreted as a 32-bit float.
        /// Requires ValueBytes.Count is 4.
        /// </summary>
        /// <exception cref="InvalidOperationException">ValueBytes.Count is not 4.</exception>
        public Single GetSingle()
        {
            Single value;
            if (!this.TryGetSingle(out value))
            {
                throw new InvalidOperationException(nameof(GetSingle) + " with bad value size");
            }

            return value;
        }

        /// <summary>
        /// Returns ValueBytes interpreted as a 32-bit or 64-bit float.
        /// Requires ValueBytes.Count is 4 or 8.
        /// </summary>
        /// <exception cref="InvalidOperationException">ValueBytes.Count is not 4 or 8.</exception>
        public Double GetDouble()
        {
            Double value;
            if (!this.TryGetDouble(out value))
            {
                throw new InvalidOperationException(nameof(GetDouble) + " with bad value size");
            }

            return value;
        }

        /// <summary>
        /// Returns ValueBytes interpreted as a big-endian Guid.
        /// Requires ValueBytes.Count is 16.
        /// </summary>
        /// <exception cref="InvalidOperationException">ValueBytes.Count is not 16.</exception>
        public Guid GetGuid()
        {
            Guid value;
            if (!this.TryGetGuid(out value))
            {
                throw new InvalidOperationException(nameof(GetGuid) + " with bad value size");
            }

            return value;
        }

        /// <summary>
        /// Returns ValueBytes interpreted as a big-endian UInt16.
        /// Requires ValueBytes.Count is 2.
        /// </summary>
        /// <exception cref="InvalidOperationException">ValueBytes.Count is not 2.</exception>
        public int GetPort()
        {
            int value;
            if (!this.TryGetPort(out value))
            {
                throw new InvalidOperationException(nameof(GetPort) + " with bad value size");
            }

            return value;
        }

        /// <summary>
        /// Returns ValueBytes interpreted as a Unix time_t (signed seconds since 1970).
        /// If time is less than DateTime.MinValue, returns DateTime.MinValue.
        /// If time is greater than DateTime.MaxValue, returns DateTime.MaxValue.
        /// Requires ValueBytes.Count is 4 or 8.
        /// </summary>
        /// <exception cref="InvalidOperationException">
        /// ValueBytes.Count is not 4 or 8, or value is out of range for DateTime.
        /// </exception>
        public DateTime GetDateTime()
        {
            DateTime value;
            if (!this.TryGetDateTime(out value))
            {
                throw new InvalidOperationException(nameof(GetDateTime) + " with bad value size");
            }

            return value;
        }

        /// <summary>
        /// Returns ValueBytes interpreted as an IPAddress.
        /// Requires ValueBytes.Count is 4 (IPv4) or 16 (IPv6).
        /// </summary>
        /// <exception cref="InvalidOperationException">ValueBytes.Count is not 4 or 16.</exception>
        public IPAddress GetIPAddress()
        {
            IPAddress value;
            if (!this.TryGetIPAddress(out value))
            {
                throw new InvalidOperationException(nameof(GetIPAddress) + " with bad value size");
            }

            return value;
        }

        /// <summary>
        /// Returns ValueBytes interpreted as a String.
        /// Character encoding is determined from Format and Encoding.
        /// </summary>
        public String GetString()
        {
            var a = this.ValueBytes.Array;
            var o = this.ValueBytes.Offset;
            var c = this.ValueBytes.Count;

            TextEncoding encoding;
            switch (this.Format)
            {
                case EventFieldFormat.String8:
                    encoding = EventUtility.EncodingString8;
                    break;
                case EventFieldFormat.StringUtfBom:
                case EventFieldFormat.StringXml:
                case EventFieldFormat.StringJson:
                    if (c >= 4 &&
                        a[o + 0] == 0xFF &&
                        a[o + 1] == 0xFE &&
                        a[o + 2] == 0x00 &&
                        a[o + 3] == 0x00)
                    {
                        o += 4;
                        c -= 4;
                        encoding = TextEncoding.UTF32;
                    }
                    else if (c >= 4 &&
                        a[o + 0] == 0x00 &&
                        a[o + 1] == 0x00 &&
                        a[o + 2] == 0xFE &&
                        a[o + 3] == 0xFF)
                    {
                        o += 4;
                        c -= 4;
                        encoding = EventUtility.EncodingUTF32BE;
                    }
                    else if (c >= 3 &&
                        a[o + 0] == 0xEF &&
                        a[o + 1] == 0xBB &&
                        a[o + 2] == 0xBF)
                    {
                        o += 3;
                        c -= 3;
                        encoding = TextEncoding.UTF8;
                    }
                    else if (c >= 2 &&
                        a[o + 0] == 0xFF &&
                        a[o + 1] == 0xFE)
                    {
                        o += 2;
                        c -= 2;
                        encoding = TextEncoding.Unicode;
                    }
                    else if (c >= 2 &&
                        a[o + 0] == 0xFE &&
                        a[o + 1] == 0xFF)
                    {
                        o += 2;
                        c -= 2;
                        encoding = TextEncoding.BigEndianUnicode;
                    }
                    else
                    {
                        goto StringUtf;
                    }
                    break;
                case EventFieldFormat.StringUtf:
                default:
                StringUtf:
                    switch (this.Encoding)
                    {
                        default:
                            encoding = EventUtility.EncodingString8;
                            break;
                        case EventFieldEncoding.Value8:
                        case EventFieldEncoding.ZStringChar8:
                        case EventFieldEncoding.StringLength16Char8:
                            encoding = TextEncoding.UTF8;
                            break;
                        case EventFieldEncoding.Value16:
                        case EventFieldEncoding.ZStringChar16:
                        case EventFieldEncoding.StringLength16Char16:
                            encoding = TextEncoding.Unicode;
                            break;
                        case EventFieldEncoding.Value32:
                        case EventFieldEncoding.ZStringChar32:
                        case EventFieldEncoding.StringLength16Char32:
                            encoding = TextEncoding.UTF32;
                            break;
                    }
                    break;
            }

            return encoding.GetString(a, o, c);
        }

        /// <summary>
        /// Formats ValueBytes as a string based on Encoding and Format.
        /// </summary>
        public string FormatValue()
        {
            Debug.Assert(this.Encoding > EventFieldEncoding.Struct);
            Debug.Assert(this.Encoding < EventFieldEncoding.Max);

            switch (this.Format)
            {
                default:
                    switch (this.Encoding)
                    {
                        case EventFieldEncoding.Value8:
                        case EventFieldEncoding.Value16:
                        case EventFieldEncoding.Value32:
                        case EventFieldEncoding.Value64:
                            goto UnsignedInt;
                        case EventFieldEncoding.ZStringChar8:
                        case EventFieldEncoding.ZStringChar16:
                        case EventFieldEncoding.ZStringChar32:
                        case EventFieldEncoding.StringLength16Char8:
                        case EventFieldEncoding.StringLength16Char16:
                        case EventFieldEncoding.StringLength16Char32:
                            goto StringUtf;
                    }
                    break;
                case EventFieldFormat.UnsignedInt:
                UnsignedInt:
                    {
                        if (this.TryGetUInt64(out var value))
                        {
                            return value.ToString(CultureInfo.InvariantCulture);
                        }
                    }
                    break;
                case EventFieldFormat.SignedInt:
                    {
                        if (this.TryGetInt64(out var value))
                        {
                            return value.ToString(CultureInfo.InvariantCulture);
                        }
                    }
                    break;
                case EventFieldFormat.HexInt:
                    {
                        if (this.TryGetUInt64(out var value))
                        {
                            return "0x" + value.ToString("X", CultureInfo.InvariantCulture);
                        }
                    }
                    break;
                case EventFieldFormat.Errno:
                    {
                        if (this.TryGetInt32(out var value))
                        {
                            return StringFromLinuxErrno(value);
                        }
                    }
                    break;
                case EventFieldFormat.Pid:
                    {
                        if (this.TryGetInt32(out var value))
                        {
                            return value.ToString(CultureInfo.InvariantCulture);
                        }
                    }
                    break;
                case EventFieldFormat.Time:
                    {
                        if (this.TryGetInt64(out var value))
                        {
                            return StringFromUnixTimeSeconds(value);
                        }
                    }
                    break;
                case EventFieldFormat.Boolean:
                    {
                        // Use UInt32 so we don't sign-extend bool8 or bool16.
                        if (this.TryGetUInt32(out var value))
                        {
                            return StringFromBoolean(value);
                        }
                    }
                    break;
                case EventFieldFormat.Float:
                    switch (this.ValueBytes.Count)
                    {
                        case 4:
                            return BitConverter.ToSingle(this.ValueBytes.Array, this.ValueBytes.Offset)
                                .ToString(CultureInfo.InvariantCulture);
                        case 8:
                            return BitConverter.ToDouble(this.ValueBytes.Array, this.ValueBytes.Offset)
                                .ToString(CultureInfo.InvariantCulture);
                    }
                    break;
                case EventFieldFormat.HexBinary:
                    break;
                case EventFieldFormat.String8:
                case EventFieldFormat.StringUtf:
                case EventFieldFormat.StringUtfBom:
                case EventFieldFormat.StringXml:
                case EventFieldFormat.StringJson:
                StringUtf:
                    return this.GetString();
                case EventFieldFormat.Uuid:
                    {
                        if (this.TryGetGuid(out var value))
                        {
                            return value.ToString("D", CultureInfo.InvariantCulture);
                        }
                    }
                    break;
                case EventFieldFormat.Port:
                    {
                        if (this.TryGetPort(out var value))
                        {
                            return value.ToString(CultureInfo.InvariantCulture);
                        }
                    }
                    break;
                case EventFieldFormat.IPv4:
                case EventFieldFormat.IPv6:
                    {
                        if (this.TryGetIPAddress(out var value))
                        {
                            return value.ToString();
                        }
                    }
                    break;
            }

            // Fallback: HexBinary.
            return BitConverter.ToString(this.ValueBytes.Array, this.ValueBytes.Offset, this.ValueBytes.Count);
        }
    }
}
