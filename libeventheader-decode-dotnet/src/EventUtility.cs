// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

using System;
using Encoding = System.Text.Encoding;

namespace EventHeaderDecode
{
    internal static class EventUtility
    {
        private static Encoding encodingString8;
        private static Encoding encodingUTF32BE;

        public static Encoding EncodingString8
        {
            get
            {
                var value = encodingString8;
                if (value == null)
                {
                    value = Encoding.GetEncoding(1252);
                    encodingString8 = value;
                }

                return value;
            }
        }

        public static Encoding EncodingUTF32BE
        {
            get
            {
                var value = encodingUTF32BE;
                if (value == null)
                {
                    value = Encoding.GetEncoding(12001);
                    encodingUTF32BE = value;
                }

                return value;
            }
        }

        public static Guid GuidFromBytes(byte[] array, int offset)
        {
            unchecked
            {
                var a = (int)(
                    array[offset + 0] << 24 |
                    array[offset + 1] << 16 |
                    array[offset + 2] << 8 |
                    array[offset + 3] << 0);
                var b = (short)(
                    array[offset + 4] << 8 |
                    array[offset + 5] << 0);
                var c = (short)(
                    array[offset + 6] << 8 |
                    array[offset + 7] << 0);
                return new Guid(a, b, c,
                    array[offset + 8],
                    array[offset + 9],
                    array[offset + 10],
                    array[offset + 11],
                    array[offset + 12],
                    array[offset + 13],
                    array[offset + 14],
                    array[offset + 15]);
            }
        }

        public static string NameFromBytes(ArraySegment<byte> bytes)
        {
            return Encoding.UTF8.GetString(bytes.Array, bytes.Offset, bytes.Count);
        }
    }
}
