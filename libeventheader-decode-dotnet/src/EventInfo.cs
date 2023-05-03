// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

using System;
using Debug = System.Diagnostics.Debug;

namespace EventHeaderDecode
{
    /// <summary>
    /// Event attributes.
    /// </summary>
    public struct EventInfo
    {
        /// <summary>
        /// UTF-8 encoded "EventName" followed by 0 or more field attributes.
        /// Each attribute is ";AttribName=AttribValue".
        /// EventName should not contain ';'.
        /// AttribName should not contain ';' or '='.
        /// AttribValue may contain ";;" which should be unescaped to ";".
        /// </summary>
        public ArraySegment<byte> NameBytes;

        /// <summary>
        /// TracepointName, e.g. "ProviderName_LnKnnnOptions".
        /// </summary>
        public string TracepointName;

        /// <summary>
        /// Big-endian activity id bytes. 0 bytes for none,
        /// 16 bytes for activity id only, 32 bytes for activity id and related id.
        /// </summary>
        public ArraySegment<byte> ActivityIdBytes;

        /// <summary>
        /// Flags, Version, Id, Tag, Opcode, Level.
        /// </summary>
        public EventHeader Header;

        /// <summary>
        /// Event category bits.
        /// </summary>
        public ulong Keyword;

        /// <summary>
        /// Gets a new string (decoded from NameBytes) containing
        /// "EventName" followed by 0 or more field attributes.
        /// Each attribute is ";AttribName=AttribValue".
        /// EventName should not contain ';'.
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
        /// Gets a new string containing ProviderName, i.e. the part of TracepointName
        /// before level and keyword, e.g. if TracepointName is
        /// "ProviderName_LnKnnnOptions", returns "ProviderName".
        /// </summary>
        public string ProviderName
        {
            get
            {
                return this.TracepointName.Substring(0, this.TracepointName.LastIndexOf('_'));
            }
        }

        /// <summary>
        /// Gets a new string containing Options, i.e. the part of TracepointName after
        /// level and keyword, e.g. if TracepointName is "ProviderName_LnKnnnOptions",
        /// returns "Options".
        /// </summary>
        public string Options
        {
            get
            {
                var n = this.TracepointName;
                for (var i = n.LastIndexOf('_') + 1; i < n.Length; i += 1)
                {
                    char ch = n[i];
                    if ('A' <= ch && ch <= 'Z' && ch != 'L' && ch != 'K')
                    {
                        return n.Substring(i);
                    }
                }

                return "";
            }
        }

        /// <summary>
        /// 128-bit activity id decoded from ActivityIdBytes, or NULL if no activity id.
        /// </summary>
        public Guid? ActivityId
        {
            get
            {
                Debug.Assert((this.ActivityIdBytes.Count & 0xF) == 0);
                return this.ActivityIdBytes.Count < 16
                    ? new Guid?()
                    : EventUtility.GuidFromBytes(this.ActivityIdBytes.Array, this.ActivityIdBytes.Offset);
            }
        }

        /// <summary>
        /// 128-bit related id decoded from ActivityIdBytes, or NULL if no related id.
        /// </summary>
        public Guid? RelatedActivityId
        {
            get
            {
                Debug.Assert((this.ActivityIdBytes.Count & 0xF) == 0);
                return this.ActivityIdBytes.Count < 32
                    ? new Guid?()
                    : EventUtility.GuidFromBytes(this.ActivityIdBytes.Array, this.ActivityIdBytes.Offset + 16);
            }
        }
    }
}
