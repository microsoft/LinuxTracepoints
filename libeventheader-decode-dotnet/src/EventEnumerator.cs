// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

using System;
using Debug = System.Diagnostics.Debug;

namespace EventHeaderDecode
{
    /// <summary>
    /// Helper for decoding EventHeader events.
    /// </summary>
    public class EventEnumerator
    {
        private const EventFieldEncoding EncodingCountMask = (
            EventFieldEncoding.FlagVArray | EventFieldEncoding.FlagCArray);

        private const EventFieldEncoding ReadFieldError = EventFieldEncoding.Invalid;

        /// <summary>
        /// Substate allows us to flatten
        /// "switch (state)    { case X: if (condition) ... }" to
        /// "switch (substate) { case X_condition: ... }"
        /// which potentially improves performance.
        /// </summary>
        private enum SubState : byte
        {
            None,
            Error,
            AfterLastItem,
            BeforeFirstItem,
            Value_Metadata,
            Value_Scalar,
            Value_SimpleArrayElement,
            Value_ComplexArrayElement,
            ArrayBegin,
            ArrayEnd,
            StructBegin,
            StructEnd,
        }

        private struct StackEntry
        {
            public int NextOffset; // m_buf[NextOffset] starts next field's name.
            public int NameOffset; // m_buf[NameOffset] starts current field's name.
            public ushort NameSize; // m_buf[NameOffset + NameSize + 1] starts current field's type.
            public ushort ArrayIndex;
            public ushort ArrayCount;
            public byte RemainingFieldCount; // Number of NextProperty() calls before popping stack.
            public EventFieldEncoding ArrayFlags; // Encoding & EncodingCountMask
        }

        private struct FieldType
        {
            public EventFieldEncoding Encoding;
            public EventFieldFormat Format;
            public ushort Tag;
        }

        // Set by StartEvent:
        private EventHeader m_header;
        private ulong m_keyword;
        private int m_metaBegin; // Relative to m_buf
        private int m_metaEnd;
        private int m_activityIdBegin; // Relative to m_buf
        private byte m_activityIdSize;
        private ushort m_eventNameSize; // Name starts at m_buf[m_metaBegin]
        private int m_dataBegin; // Relative to m_buf
        private int m_dataEnd;
        private byte[] m_buf;
        private string m_tracepointName;

        // Values change during enumeration:
        private int m_dataPosRaw;
        private int m_moveNextRemaining;
        private StackEntry m_stackTop;
        private byte m_stackIndex; // Number of items currently on stack.
        private EventEnumeratorState m_state = EventEnumeratorState.None;
        private SubState m_subState = SubState.None;
        private EventEnumeratorError m_lastError = EventEnumeratorError.Success;

        private byte m_elementSize; // 0 if item is variable-size or complex.
        private FieldType m_fieldType; // Note: fieldType.Encoding is cooked.
        private int m_dataPosCooked;
        private int m_itemSizeRaw;
        private int m_itemSizeCooked;

        // Limit events to 8 levels of nested structures.
        private readonly StackEntry[] m_stack = new StackEntry[8];

        public const int MoveNextLimitDefault = 4096;

        /// <summary>
        /// Initializes a new instance of EventEnumerator. Sets State to None.
        /// </summary>
        public EventEnumerator()
        {
            return;
        }

        /// <summary>
        /// Returns the current state.
        /// </summary>
        public EventEnumeratorState State
        {
            get
            {
                return m_state;
            }
        }

        /// <summary>
        /// Gets status for the most recent call to StartEvent, MoveNext, or MoveNextSibling.
        /// </summary>
        public EventEnumeratorError LastError
        {
            get
            {
                return m_lastError;
            }
        }

        /// <summary>
        /// Sets State to None.
        /// </summary>
        public void Clear()
        {
            SetNoneState(EventEnumeratorError.Success);
        }

        /// <summary>
        /// <para>
        /// Starts decoding the specified EventHeader event: decodes the header and
        /// positions the enumerator before the first item.
        /// </para><para>
        /// On success, changes the state to BeforeFirstItem and returns true.
        /// On failure, changes the state to None (not Error) and returns false.
        /// </para><para>
        /// Note that the enumerator stores a reference to the eventData array but does
        /// not copy it, so the referenced data must remain valid and unchanged while
        /// you are processing the data with this enumerator (i.e. do not overwrite the
        /// buffer until you are done with this event).
        /// </para>
        /// </summary>
        /// <param name="tracepointName">
        /// Set to tep_event->name, e.g. "MyProvider_L4K1".
        /// Must follow the tracepoint name rules described in EventHeader.h.
        /// </param>
        /// <param name="eventData">
        /// Set to pointer to the event data (the EventHeaderFlags field of the event header),
        /// usually something like tep_record->data + tep_event->format.fields[0].offset.
        /// </param>
        /// <param name="moveNextLimit">
        /// Set to the maximum number of MoveNext calls to allow when processing this event (to
        /// guard against DoS attacks from a maliciously-crafted event).
        /// </param>
        /// <returns>Returns false for failure. Check LastError for details.</returns>
        public bool StartEvent(
            string tracepointName,
            ArraySegment<byte> eventData,
            int moveNextLimit = MoveNextLimitDefault)
        {
            // .NET version does not support big-endian host or big-endian events.
            Debug.Assert(BitConverter.IsLittleEndian);

            const int EventHeaderTracepointNameMax = 256;
            const int SizeofEventHeader = 8;
            const int SizeofEventHeaderExtension = 4;

            const EventHeaderFlags HostEndianFlag =
                EventHeaderFlags.LittleEndian;

            const EventHeaderFlags KnownFlags = (
                EventHeaderFlags.Pointer64 | EventHeaderFlags.LittleEndian | EventHeaderFlags.Extension);

            var eventBuf = eventData.Array;
            var eventPos = eventData.Offset;
            var eventEnd = eventPos + eventData.Count;

            if (eventEnd - eventPos < SizeofEventHeader ||
                tracepointName.Length >= EventHeaderTracepointNameMax)
            {
                // Event has no header or TracepointName too long.
                return SetNoneState(EventEnumeratorError.InvalidParameter);
            }

            // Get event header and validate it.

            m_header.Flags = (EventHeaderFlags)eventBuf[eventPos];
            eventPos += 1;
            m_header.Version = eventBuf[eventPos];
            eventPos += 1;
            m_header.Id = BitConverter.ToUInt16(eventBuf, eventPos);
            eventPos += 2;
            m_header.Tag = BitConverter.ToUInt16(eventBuf, eventPos);
            eventPos += 2;
            m_header.Opcode = (EventOpcode)eventBuf[eventPos];
            eventPos += 1;
            m_header.Level = (EventLevel)eventBuf[eventPos];
            eventPos += 1;

            if (m_header.Flags != (m_header.Flags & KnownFlags) ||
                HostEndianFlag != (m_header.Flags & HostEndianFlag))
            {
                // Not a supported event: unsupported flags or big-endian.
                return SetNoneState(EventEnumeratorError.NotSupported);
            }

            // Validate Tracepoint name (e.g. "ProviderName_L1K2..."), extract keyword.

            int attribPos = tracepointName.LastIndexOf('_');
            if (attribPos < 0)
            {
                // Not a supported event: no underscore in name.
                return SetNoneState(EventEnumeratorError.NotSupported);
            }

            attribPos += 1; // Skip underscore.

            if (attribPos >= tracepointName.Length ||
                'L' != tracepointName[attribPos])
            {
                // Not a supported event: no Level in name.
                return SetNoneState(EventEnumeratorError.NotSupported);
            }

            ulong attribLevel;
            attribPos = LowercaseHexToInt(tracepointName, attribPos + 1, out attribLevel);
            if (attribLevel != (byte)m_header.Level)
            {
                // Not a supported event: name's level != header's level.
                return SetNoneState(EventEnumeratorError.NotSupported);
            }

            if (attribPos >= tracepointName.Length ||
                'K' != tracepointName[attribPos])
            {
                // Not a supported event: no Keyword in name.
                return SetNoneState(EventEnumeratorError.NotSupported);
            }

            attribPos = LowercaseHexToInt(tracepointName, attribPos + 1, out m_keyword);

            // Validate but ignore any other attributes.

            while (attribPos < tracepointName.Length)
            {
                char ch;
                ch = tracepointName[attribPos];
                if (ch < 'A' || 'Z' < ch)
                {
                    // Invalid attribute start character.
                    return SetNoneState(EventEnumeratorError.NotSupported);
                }

                // Skip attribute value chars.
                for (attribPos += 1; attribPos < tracepointName.Length; attribPos += 1)
                {
                    ch = tracepointName[attribPos];
                    if ((ch < '0' || '9' < ch) && (ch < 'a' || 'z' < ch))
                    {
                        break;
                    }
                }
            }

            // Parse header extensions.

            m_metaBegin = 0;
            m_metaEnd = 0;
            m_activityIdBegin = 0;
            m_activityIdSize = 0;

            if (0 != (m_header.Flags & EventHeaderFlags.Extension))
            {
                EventHeaderExtension ext;
                do
                {
                    if (eventEnd - eventPos < SizeofEventHeaderExtension)
                    {
                        return SetNoneState(EventEnumeratorError.InvalidData);
                    }

                    ext.Size = BitConverter.ToUInt16(eventBuf, eventPos);
                    eventPos += 2;
                    ext.Kind = (EventHeaderExtensionKind)BitConverter.ToUInt16(eventBuf, eventPos);
                    eventPos += 2;

                    if (eventEnd - eventPos < ext.Size)
                    {
                        return SetNoneState(EventEnumeratorError.InvalidData);
                    }

                    switch (ext.Kind & EventHeaderExtensionKind.ValueMask)
                    {
                        case EventHeaderExtensionKind.Invalid: // Invalid extension type.
                            return SetNoneState(EventEnumeratorError.InvalidData);

                        case EventHeaderExtensionKind.Metadata:
                            if (m_metaBegin != 0)
                            {
                                // Multiple Metadata extensions.
                                return SetNoneState(EventEnumeratorError.InvalidData);
                            }

                            m_metaBegin = eventPos;
                            m_metaEnd = m_metaBegin + ext.Size;
                            break;

                        case EventHeaderExtensionKind.ActivityId:
                            if (m_activityIdBegin != 0 ||
                                (ext.Size != 16 && ext.Size != 32))
                            {
                                // Multiple ActivityId extensions, or bad activity id size.
                                return SetNoneState(EventEnumeratorError.InvalidData);
                            }

                            m_activityIdBegin = eventPos;
                            m_activityIdSize = (byte)ext.Size;
                            break;

                        default:
                            break; // Ignore other extension types.
                    }

                    eventPos += ext.Size;
                }
                while (0 != (ext.Kind & EventHeaderExtensionKind.FlagChain));
            }

            if (m_metaBegin == 0)
            {
                // Not a supported event - no metadata extension.
                return SetNoneState(EventEnumeratorError.NotSupported);
            }

            int eventNameEndPos = Array.IndexOf<byte>(eventBuf, 0, m_metaBegin, m_metaEnd - m_metaBegin);
            if (eventNameEndPos < 0)
            {
                // Event name not nul-terminated.
                return SetNoneState(EventEnumeratorError.InvalidData);
            }

            m_eventNameSize = (ushort)(eventNameEndPos - m_metaBegin);
            m_dataBegin = eventPos;
            m_dataEnd = eventEnd;
            m_buf = eventBuf;
            m_tracepointName = tracepointName;

            ResetImpl(moveNextLimit);
            return true;
        }

        /// <summary>
        /// <para>
        /// Positions the enumerator before the first item.
        /// </para><para>
        /// PRECONDITION: Can be called when State != None, i.e. at any time after a
        /// successful call to StartEvent, until a call to Clear.
        /// </para>
        /// </summary>
        /// <exception cref="InvalidOperationException">Called in invalid State.</exception>
        public void Reset(int moveNextLimit = MoveNextLimitDefault)
        {
            if (m_state == EventEnumeratorState.None)
            {
                throw new InvalidOperationException(); // PRECONDITION
            }
            else
            {
                ResetImpl(moveNextLimit);
            }
        }

        /// <summary>
        /// <para>
        /// Moves the enumerator to the next item in the current event, or to the end
        /// of the event if no more items. Returns true if moved to a valid item,
        /// false if no more items or decoding error.
        /// </para><para>
        /// PRECONDITION: Can be called when State >= BeforeFirstItem, i.e. after a
        /// successful call to StartEvent, until MoveNext returns false.
        /// </para><para>
        /// Typically called in a loop until it returns false, e.g.:
        /// </para><code>
        /// if (!e.StartEvent(...)) return e.LastError;
        /// while (e.MoveNext())
        /// {
        ///     EventItemInfo item = e.GetItemInfo();
        ///     switch (e.State)
        ///     {
        ///     case EventEnumeratorState.Value:
        ///         DoValue(item);
        ///         break;
        ///     case EventEnumeratorState.StructBegin:
        ///         DoStructBegin(item);
        ///         break;
        ///     case EventEnumeratorState.StructEnd:
        ///         DoStructEnd(item);
        ///         break;
        ///     case EventEnumeratorState.ArrayBegin:
        ///         DoArrayBegin(item);
        ///         break;
        ///     case EventEnumeratorState.ArrayEnd:
        ///         DoArrayEnd(item);
        ///         break;
        ///     }
        /// }
        /// return e.LastError;
        /// </code>
        /// </summary>
        /// <returns>
        /// Returns true if moved to a valid item.
        /// Returns false and sets state to AfterLastItem if no more items.
        /// Returns false and sets state to Error for decoding error.
        /// Check LastError for details.
        /// </returns>
        /// <exception cref="InvalidOperationException">Called in invalid State.</exception>
        public bool MoveNext()
        {
            if (m_state < EventEnumeratorState.BeforeFirstItem)
            {
                throw new InvalidOperationException(); // PRECONDITION
            }

            if (m_moveNextRemaining == 0)
            {
                return SetErrorState(EventEnumeratorError.ImplementationLimit);
            }

            m_moveNextRemaining -= 1;

            bool movedToItem;
            switch (m_subState)
            {
                default:

                    Debug.Fail("Unexpected substate.");
                    throw new InvalidOperationException();

                case SubState.BeforeFirstItem:

                    Debug.Assert(m_state == EventEnumeratorState.BeforeFirstItem);
                    movedToItem = NextProperty();
                    break;

                case SubState.Value_Metadata:
                    throw new InvalidOperationException();

                case SubState.Value_Scalar:

                    Debug.Assert(m_state == EventEnumeratorState.Value);
                    Debug.Assert(m_fieldType.Encoding != EventFieldEncoding.Struct);
                    Debug.Assert(m_stackTop.ArrayFlags == 0);
                    Debug.Assert(m_dataEnd - m_dataPosRaw >= m_itemSizeRaw);

                    m_dataPosRaw += m_itemSizeRaw;
                    movedToItem = NextProperty();
                    break;

                case SubState.Value_SimpleArrayElement:

                    Debug.Assert(m_state == EventEnumeratorState.Value);
                    Debug.Assert(m_fieldType.Encoding != EventFieldEncoding.Struct);
                    Debug.Assert(m_stackTop.ArrayFlags != 0);
                    Debug.Assert(m_stackTop.ArrayIndex < m_stackTop.ArrayCount);
                    Debug.Assert(m_elementSize != 0); // Eligible for fast path.
                    Debug.Assert(m_dataEnd - m_dataPosRaw >= m_itemSizeRaw);

                    m_dataPosRaw += m_itemSizeRaw;
                    m_stackTop.ArrayIndex += 1;

                    if (m_stackTop.ArrayCount == m_stackTop.ArrayIndex)
                    {
                        // End of array.
                        SetEndState(EventEnumeratorState.ArrayEnd, SubState.ArrayEnd);
                    }
                    else
                    {
                        // Middle of array - get next element.
                        StartValueSimple(); // Fast path for simple array elements.
                    }

                    movedToItem = true;
                    break;

                case SubState.Value_ComplexArrayElement:

                    Debug.Assert(m_state == EventEnumeratorState.Value);
                    Debug.Assert(m_fieldType.Encoding != EventFieldEncoding.Struct);
                    Debug.Assert(m_stackTop.ArrayFlags != 0);
                    Debug.Assert(m_stackTop.ArrayIndex < m_stackTop.ArrayCount);
                    Debug.Assert(m_elementSize == 0); // Not eligible for fast path.
                    Debug.Assert(m_dataEnd - m_dataPosRaw >= m_itemSizeRaw);

                    m_dataPosRaw += m_itemSizeRaw;
                    m_stackTop.ArrayIndex += 1;

                    if (m_stackTop.ArrayCount == m_stackTop.ArrayIndex)
                    {
                        // End of array.
                        SetEndState(EventEnumeratorState.ArrayEnd, SubState.ArrayEnd);
                        movedToItem = true;
                    }
                    else
                    {
                        // Middle of array - get next element.
                        movedToItem = StartValue(); // Normal path for complex array elements.
                    }

                    break;

                case SubState.ArrayBegin:

                    Debug.Assert(m_state == EventEnumeratorState.ArrayBegin);
                    Debug.Assert(m_stackTop.ArrayFlags != 0);
                    Debug.Assert(m_stackTop.ArrayIndex == 0);

                    if (m_stackTop.ArrayCount == 0)
                    {
                        // 0-length array.
                        SetEndState(EventEnumeratorState.ArrayEnd, SubState.ArrayEnd);
                        movedToItem = true;
                    }
                    else if (m_elementSize != 0)
                    {
                        // First element of simple array.
                        Debug.Assert(m_fieldType.Encoding != EventFieldEncoding.Struct);
                        m_itemSizeCooked = m_elementSize;
                        m_itemSizeRaw = m_elementSize;
                        SetState(EventEnumeratorState.Value, SubState.Value_SimpleArrayElement);
                        StartValueSimple();
                        movedToItem = true;
                    }
                    else if (m_fieldType.Encoding != EventFieldEncoding.Struct)
                    {
                        // First element of complex array.
                        SetState(EventEnumeratorState.Value, SubState.Value_ComplexArrayElement);
                        movedToItem = StartValue();
                    }
                    else
                    {
                        // First element of array of struct.
                        StartStruct();
                        movedToItem = true;
                    }

                    break;

                case SubState.ArrayEnd:

                    Debug.Assert(m_state == EventEnumeratorState.ArrayEnd);
                    Debug.Assert(m_stackTop.ArrayFlags != 0);
                    Debug.Assert(m_stackTop.ArrayCount == m_stackTop.ArrayIndex);

                    // 0-length array of struct means we won't naturally traverse
                    // the child struct's metadata. Since m_stackTop.NextOffset
                    // won't get updated naturally, we need to update it manually.
                    if (m_fieldType.Encoding == EventFieldEncoding.Struct &&
                        m_stackTop.ArrayCount == 0 &&
                        !SkipStructMetadata())
                    {
                        movedToItem = false;
                    }
                    else
                    {
                        movedToItem = NextProperty();
                    }

                    break;

                case SubState.StructBegin:

                    Debug.Assert(m_state == EventEnumeratorState.StructBegin);
                    if (m_stackIndex >= m_stack.Length)
                    {
                        movedToItem = SetErrorState(EventEnumeratorError.StackOverflow);
                    }
                    else
                    {
                        m_stack[m_stackIndex] = m_stackTop;
                        m_stackIndex += 1;

                        m_stackTop.RemainingFieldCount = (byte)m_fieldType.Format;
                        // Parent's NextOffset is the correct starting point for the struct.
                        movedToItem = NextProperty();
                    }

                    break;

                case SubState.StructEnd:

                    Debug.Assert(m_state == EventEnumeratorState.StructEnd);
                    Debug.Assert(m_fieldType.Encoding == EventFieldEncoding.Struct);
                    Debug.Assert(m_itemSizeRaw == 0);

                    m_stackTop.ArrayIndex += 1;

                    if (m_stackTop.ArrayCount != m_stackTop.ArrayIndex)
                    {
                        Debug.Assert(m_stackTop.ArrayFlags != 0);
                        Debug.Assert(m_stackTop.ArrayIndex < m_stackTop.ArrayCount);

                        // Middle of array - get next element.
                        StartStruct();
                        movedToItem = true;
                    }
                    else if (m_stackTop.ArrayFlags != 0)
                    {
                        // End of array.
                        SetEndState(EventEnumeratorState.ArrayEnd, SubState.ArrayEnd);
                        movedToItem = true;
                    }
                    else
                    {
                        // End of property - move to next property.
                        movedToItem = NextProperty();
                    }

                    break;
            }

            return movedToItem;
        }

        /// <summary>
        /// <para>
        /// Moves the enumerator to the next sibling of the current item, or to the end
        /// of the event if no more items. Returns true if moved to a valid item, false
        /// if no more items or decoding error.
        /// </para><para>
        /// PRECONDITION: Can be called when State >= BeforeFirstItem, i.e. after a
        /// successful call to StartEvent, until MoveNext returns false.
        /// </para><list type="bullet"><item>
        /// If the current item is ArrayBegin or StructBegin, this efficiently moves
        /// enumeration to AFTER the corresponding ArrayEnd or StructEnd.
        /// </item><item>
        /// Otherwise, this is the same as MoveNext.
        /// </item></list><para>
        /// Typical use for this method is to efficiently skip past an array of fixed-size
        /// items (i.e. an array where ElementSize is nonzero) when you process all of the
        /// array items within the ArrayBegin state.
        /// </para><code>
        /// if (!e.StartEvent(...)) return e.LastError;
        /// if (!e.MoveNext()) return e.LastError;  // AfterLastItem or Error.
        /// while (true)
        /// {
        ///     EventItemInfo item = e.GetItemInfo();
        ///     switch (e.State)
        ///     {
        ///     case EventEnumeratorState.Value:
        ///         DoValue(item);
        ///         break;
        ///     case EventEnumeratorState.StructBegin:
        ///         DoStructBegin(item);
        ///         break;
        ///     case EventEnumeratorState.StructEnd:
        ///         DoStructEnd(item);
        ///         break;
        ///     case EventEnumeratorState.ArrayBegin:
        ///         if (item.ElementSize == 0)
        ///         {
        ///             DoComplexArrayBegin(item);
        ///         }
        ///         else
        ///         {
        ///             // Process the entire array directly without using the enumerator.
        ///             DoSimpleArrayBegin(item);
        ///             for (int i = 0; i != item.ArrayCount; i++)
        ///             {
        ///                 DoSimpleArrayElement(item, i);
        ///             }
        ///             DoSimpleArrayEnd(item);
        /// 
        ///             // Skip the entire array at once.
        ///             if (!e.MoveNextSibling()) // Instead of MoveNext().
        ///             {
        ///                 return e.LastError;
        ///             }
        ///             continue; // Skip the MoveNext().
        ///         }
        ///         break;
        ///     case EventEnumeratorState.ArrayEnd:
        ///         DoComplexArrayEnd(item);
        ///         break;
        ///     }
        /// 
        ///     if (!e.MoveNext())
        ///     {
        ///         return e.LastError;
        ///     }
        /// }
        /// </code>
        /// </summary>
        /// <returns>
        /// Returns true if moved to a valid item.
        /// Returns false and sets state to AfterLastItem if no more items.
        /// Returns false and sets state to Error for decoding error.
        /// Check LastError for details.
        /// </returns>
        /// <exception cref="InvalidOperationException">Called in invalid State.</exception>
        public bool MoveNextSibling()
        {
            bool movedToItem;
            int depth = 0; // May reach -1 if we start on ArrayEnd/StructEnd.
            do
            {
                switch (m_state)
                {
                    default:
                        // Same as MoveNext.
                        break;

                    case EventEnumeratorState.ArrayEnd:
                    case EventEnumeratorState.StructEnd:
                        depth -= 1;
                        break;

                    case EventEnumeratorState.StructBegin:
                        depth += 1;
                        break;

                    case EventEnumeratorState.ArrayBegin:
                        if (m_elementSize == 0 || m_moveNextRemaining == 0)
                        {
                            // Use MoveNext for full processing.
                            depth += 1;
                            break;
                        }
                        else
                        {
                            // Array of simple elements - jump directly to next sibling.
                            Debug.Assert(m_subState == SubState.ArrayBegin);
                            Debug.Assert(m_fieldType.Encoding != EventFieldEncoding.Struct);
                            Debug.Assert(m_stackTop.ArrayFlags != 0);
                            Debug.Assert(m_stackTop.ArrayIndex == 0);
                            m_dataPosRaw += m_stackTop.ArrayCount * m_elementSize;
                            m_moveNextRemaining -= 1;
                            movedToItem = NextProperty();
                            continue; // Skip MoveNext().
                        }
                }

                movedToItem = MoveNext();
            } while (movedToItem && depth > 0);

            return movedToItem;
        }

        /// <summary>
        /// <para>
        /// Advanced scenarios. This method is for extracting type information from an
        /// event without looking at value information. Moves the enumerator to the next
        /// field declaration (not the next field value). Returns true if moved to a valid
        /// item, false if no more items or decoding error.
        /// </para><para>
        /// PRECONDITION: Can be called after a successful call to StartEvent, until
        /// MoveNextMetadata returns false.
        /// </para><para>
        /// Note that metadata enumeration gives a flat view of arrays and structures.
        /// There are only Value items, no BeginArray, EndArray, BeginStruct, EndStruct.
        /// A struct shows up as a value with Encoding = Struct (Format holds field count).
        /// An array shows up as a value with ArrayFlags != 0, and ArrayCount is either zero
        /// (indicating a runtime-variable array length) or nonzero (indicating a compile-time
        /// constant array length). An array of struct is a field with Encoding = Struct and
        /// ArrayFlags != 0. ValueBytes will always be empty. ArrayIndex and ElementSize
        /// will always be zero.
        /// </para><para>
        /// Note that when enumerating metadata for a structure, the enumeration may end before
        /// the expected number of fields are seen. This is a supported scenario and is not an
        /// error in the event. A large field count just means "this structure contains all the
        /// remaining fields in the event".
        /// </para><para>
        /// Typically called in a loop until it returns false.
        /// </para><code>
        /// if (!e.StartEvent(...)) return e.LastError;
        /// while (e.MoveNextMetadata())
        /// {
        ///     DoFieldDeclaration(e.GetItemInfo());
        /// }
        /// return e.LastError;
        /// </code>
        /// </summary>
        /// <returns>
        /// Returns true if moved to a valid item.
        /// Returns false and sets state to AfterLastItem if no more items.
        /// Returns false and sets state to Error for decoding error.
        /// Check LastError for details.
        /// </returns>
        public bool MoveNextMetadata()
        {
            if (m_subState != SubState.Value_Metadata)
            {
                if (m_state != EventEnumeratorState.BeforeFirstItem)
                {
                    throw new InvalidOperationException(); // PRECONDITION
                }

                Debug.Assert(m_subState == SubState.BeforeFirstItem);
                m_stackTop.ArrayIndex = 0;
                m_dataPosCooked = m_dataEnd;
                m_itemSizeCooked = 0;
                m_elementSize = 0;
                SetState(EventEnumeratorState.Value, SubState.Value_Metadata);
            }

            Debug.Assert(m_state == EventEnumeratorState.Value);

            bool movedToItem;
            if (m_stackTop.NextOffset != m_metaEnd)
            {
                m_stackTop.NameOffset = m_stackTop.NextOffset;

                m_fieldType = ReadFieldNameAndType();
                if (m_fieldType.Encoding == ReadFieldError)
                {
                    movedToItem = SetErrorState(EventEnumeratorError.InvalidData);
                }
                else if (
                    EventFieldEncoding.Struct == (m_fieldType.Encoding & EventFieldEncoding.ValueMask) &&
                    m_fieldType.Format == 0)
                {
                    // Struct must have at least 1 field (potential for DoS).
                    movedToItem = SetErrorState(EventEnumeratorError.InvalidData);
                }
                else if (0 == (m_fieldType.Encoding & EncodingCountMask))
                {
                    // Non-array.

                    m_stackTop.ArrayCount = 1;
                    m_stackTop.ArrayFlags = 0;
                    movedToItem = true;
                }
                else if (EventFieldEncoding.FlagVArray == (m_fieldType.Encoding & EncodingCountMask))
                {
                    // Runtime-variable array length.

                    m_fieldType.Encoding &= EventFieldEncoding.ValueMask;
                    m_stackTop.ArrayCount = 0;
                    m_stackTop.ArrayFlags = EventFieldEncoding.FlagVArray;
                    movedToItem = true;
                }
                else if (EventFieldEncoding.FlagCArray == (m_fieldType.Encoding & EncodingCountMask))
                {
                    // Compile-time-constant array length.

                    if (m_metaEnd - m_stackTop.NextOffset < 2)
                    {
                        movedToItem = SetErrorState(EventEnumeratorError.InvalidData);
                    }
                    else
                    {
                        m_stackTop.ArrayCount = BitConverter.ToUInt16(m_buf, m_stackTop.NextOffset);
                        m_stackTop.ArrayFlags = EventFieldEncoding.FlagCArray;
                        m_fieldType.Encoding &= EventFieldEncoding.ValueMask;
                        m_stackTop.NextOffset += 2;

                        if (m_stackTop.ArrayCount == 0)
                        {
                            // Constant-length array cannot have length of 0 (potential for DoS).
                            movedToItem = SetErrorState(EventEnumeratorError.InvalidData);
                        }
                        else
                        {
                            movedToItem = true;
                        }
                    }
                }
                else
                {
                    movedToItem = SetErrorState(EventEnumeratorError.NotSupported);
                }
            }
            else
            {
                // End of event.

                SetEndState(EventEnumeratorState.AfterLastItem, SubState.AfterLastItem);
                movedToItem = false; // No more items.
            }

            return movedToItem;
        }

        /// <summary>
        /// <para>
        /// Gets information that applies to the current event, e.g. the event name,
        /// provider name, options, level, keyword, etc.
        /// </para><para>
        /// PRECONDITION: Can be called when State != None, i.e. at any time after a
        /// successful call to StartEvent, until a call to Clear.
        /// </para>
        /// </summary>
        /// <exception cref="InvalidOperationException">Called in invalid State.</exception>
        public EventInfo GetEventInfo()
        {
            if (m_state == EventEnumeratorState.None)
            {
                throw new InvalidOperationException(); // PRECONDITION
            }

            return new EventInfo {
                NameBytes = new ArraySegment<byte>(m_buf, m_metaBegin, m_eventNameSize),
                TracepointName = m_tracepointName,
                ActivityIdBytes = new ArraySegment<byte>(m_buf, m_activityIdBegin, m_activityIdSize),
                Header = m_header,
                Keyword = m_keyword,
            };
        }

        /// <summary>
        /// <para>
        /// Gets information that applies to the current item, e.g. the item's name,
        /// the item's type (integer, string, float, etc.), data pointer, data size.
        /// The current item changes each time MoveNext() is called.
        /// </para><para>
        /// PRECONDITION: Can be called when State > BeforeFirstItem, i.e. after MoveNext
        /// returns true.
        /// </para>
        /// </summary>
        /// <exception cref="InvalidOperationException">Called in invalid State.</exception>
        public EventItemInfo GetItemInfo()
        {
            if (m_state <= EventEnumeratorState.BeforeFirstItem)
            {
                throw new InvalidOperationException(); // PRECONDITION
            }

            return new EventItemInfo {
                NameBytes = new ArraySegment<byte>(
                    m_buf,
                    m_stackTop.NameOffset,
                    m_stackTop.NameSize),
                ValueBytes = new ArraySegment<byte>(
                    m_buf,
                    m_dataPosCooked,
                    m_itemSizeCooked),
                ArrayIndex = m_stackTop.ArrayIndex,
                ArrayCount = m_stackTop.ArrayCount,
                ElementSize = m_elementSize,
                Encoding = m_fieldType.Encoding,
                Format = m_fieldType.Format,
                ArrayFlags = m_stackTop.ArrayFlags,
                FieldTag = m_fieldType.Tag,
            };
        }

        /// <summary>
        /// <para>
        /// Gets the remaining event payload, i.e. the event data that has not yet
        /// been decoded. The data position can change each time MoveNext is called.
        /// </para><para>
        /// PRECONDITION: Can be called when State != None, i.e. at any time after a
        /// successful call to StartEvent, until a call to Clear.
        /// </para><para>
        /// This can be useful after enumeration has completed to to determine
        /// whether the event contains any trailing data (data not described by the
        /// decoding information). Up to 3 bytes of trailing data is normal (padding
        /// between events), but 4 or more bytes of trailing data might indicate some
        /// kind of encoding problem or data corruption.
        /// </para>
        /// </summary>
        /// <exception cref="InvalidOperationException">Called in invalid State.</exception>
        public ArraySegment<byte> GetRawDataPosition()
        {
            if (m_state == EventEnumeratorState.None)
            {
                throw new InvalidOperationException(); // PRECONDITION
            }

            return new ArraySegment<byte>(m_buf, m_dataPosRaw, m_dataEnd - m_dataPosRaw);
        }

        private void ResetImpl(int moveNextLimit)
        {
            m_dataPosRaw = m_dataBegin;
            m_moveNextRemaining = moveNextLimit;
            m_stackTop.NextOffset = m_metaBegin + m_eventNameSize + 1;
            m_stackTop.RemainingFieldCount = 255; // Go until we reach end of metadata.
            m_stackIndex = 0;
            SetState(EventEnumeratorState.BeforeFirstItem, SubState.BeforeFirstItem);
            m_lastError = EventEnumeratorError.Success;
        }

        private bool SkipStructMetadata()
        {
            Debug.Assert(m_fieldType.Encoding == EventFieldEncoding.Struct);

            bool ok;
            for (uint remainingFieldCount = (byte)m_fieldType.Format; ;
                remainingFieldCount -= 1)
            {
                // It's a bit unusual but completely legal and fully supported to reach
                // end-of-metadata before remainingFieldCount == 0.
                if (remainingFieldCount == 0 || m_stackTop.NextOffset == m_metaEnd)
                {
                    ok = true;
                    break;
                }

                m_stackTop.NameOffset = m_stackTop.NextOffset;

                // Minimal validation, then skip the field:

                var type = ReadFieldNameAndType();
                if (type.Encoding == ReadFieldError)
                {
                    ok = SetErrorState(EventEnumeratorError.InvalidData);
                    break;
                }

                if (EventFieldEncoding.Struct == (type.Encoding & EventFieldEncoding.ValueMask))
                {
                    remainingFieldCount += (byte)type.Format;
                }

                if (0 == (type.Encoding & EventFieldEncoding.FlagCArray))
                {
                    // Scalar or runtime length. We're done with the field.
                }
                else if (0 == (type.Encoding & EventFieldEncoding.FlagVArray))
                {
                    // FlagCArray is set, FlagVArray is unset.
                    // Compile-time-constant array length.
                    // Skip the array length in metadata.

                    if (m_metaEnd - m_stackTop.NextOffset < 2)
                    {
                        ok = SetErrorState(EventEnumeratorError.InvalidData);
                        break;
                    }

                    m_stackTop.NextOffset += 2;
                }
                else
                {
                    // Both FlagCArray and FlagVArray are set (reserved encoding).
                    ok = SetErrorState(EventEnumeratorError.NotSupported);
                    break;
                }
            }

            return ok;
        }

        private bool NextProperty()
        {
            bool movedToItem;
            if (m_stackTop.RemainingFieldCount != 0 &&
                m_stackTop.NextOffset != m_metaEnd)
            {
                m_stackTop.RemainingFieldCount -= 1;
                m_stackTop.ArrayIndex = 0;
                m_stackTop.NameOffset = m_stackTop.NextOffset;

                // Decode a field:

                m_fieldType = ReadFieldNameAndType();
                if (m_fieldType.Encoding == ReadFieldError)
                {
                    movedToItem = SetErrorState(EventEnumeratorError.InvalidData);
                }
                else if (0 == (m_fieldType.Encoding & EncodingCountMask))
                {
                    // Non-array.

                    m_stackTop.ArrayCount = 1;
                    m_stackTop.ArrayFlags = 0;
                    if (EventFieldEncoding.Struct != m_fieldType.Encoding)
                    {
                        SetState(EventEnumeratorState.Value, SubState.Value_Scalar);
                        movedToItem = StartValue();
                    }
                    else if (m_fieldType.Format == 0)
                    {
                        // Struct must have at least 1 field (potential for DoS).
                        movedToItem = SetErrorState(EventEnumeratorError.InvalidData);
                    }
                    else
                    {
                        StartStruct();
                        movedToItem = true;
                    }
                }
                else if (EventFieldEncoding.FlagVArray == (m_fieldType.Encoding & EncodingCountMask))
                {
                    // Runtime-variable array length.

                    if (m_dataEnd - m_dataPosRaw < 2)
                    {
                        movedToItem = SetErrorState(EventEnumeratorError.InvalidData);
                    }
                    else
                    {
                        m_stackTop.ArrayCount = BitConverter.ToUInt16(m_buf, m_dataPosRaw);
                        m_dataPosRaw += 2;

                        movedToItem = StartArray(); // StartArray will set Flags.
                    }
                }
                else if (EventFieldEncoding.FlagCArray == (m_fieldType.Encoding & EncodingCountMask))
                {
                    // Compile-time-constant array length.

                    if (m_metaEnd - m_stackTop.NextOffset < 2)
                    {
                        movedToItem = SetErrorState(EventEnumeratorError.InvalidData);
                    }
                    else
                    {
                        m_stackTop.ArrayCount = BitConverter.ToUInt16(m_buf, m_stackTop.NextOffset);
                        m_stackTop.NextOffset += 2;

                        if (m_stackTop.ArrayCount == 0)
                        {
                            // Constant-length array cannot have length of 0 (potential for DoS).
                            movedToItem = SetErrorState(EventEnumeratorError.InvalidData);
                        }
                        else
                        {
                            movedToItem = StartArray(); // StartArray will set Flags.
                        }
                    }
                }
                else
                {
                    movedToItem = SetErrorState(EventEnumeratorError.NotSupported);
                }
            }
            else if (m_stackIndex != 0)
            {
                // End of struct.
                // It's a bit unusual but completely legal and fully supported to reach
                // end-of-metadata before RemainingFieldCount == 0.

                // Pop child from stack.
                m_stackIndex -= 1;
                var childMetadataOffset = m_stackTop.NextOffset;
                m_stackTop = m_stack[m_stackIndex];

                m_fieldType = ReadFieldType(
                    m_stackTop.NameOffset + m_stackTop.NameSize + 1);
                Debug.Assert(EventFieldEncoding.Struct == (m_fieldType.Encoding & EventFieldEncoding.ValueMask));
                m_fieldType.Encoding = EventFieldEncoding.Struct; // Mask off array flags.
                m_elementSize = 0;

                // Unless parent is in the middle of an array, we need to set the
                // "next field" position to the child's metadata position.
                Debug.Assert(m_stackTop.ArrayIndex < m_stackTop.ArrayCount);
                if (m_stackTop.ArrayIndex + 1 == m_stackTop.ArrayCount)
                {
                    m_stackTop.NextOffset = childMetadataOffset;
                }

                SetEndState(EventEnumeratorState.StructEnd, SubState.StructEnd);
                movedToItem = true;
            }
            else
            {
                // End of event.

                if (m_stackTop.NextOffset != m_metaEnd)
                {
                    // Event has metadata for more than MaxTopLevelProperties.
                    movedToItem = SetErrorState(EventEnumeratorError.NotSupported);
                }
                else
                {
                    SetEndState(EventEnumeratorState.AfterLastItem, SubState.AfterLastItem);
                    movedToItem = false; // No more items.
                }
            }

            return movedToItem;
        }

        /// <summary>
        /// Requires m_metaEnd >= m_stackTop.NameOffset.
        /// Reads name, encoding, format, tag starting at m_stackTop.NameOffset.
        /// Updates m_stackTop.NameSize, m_stackTop.NextOffset.
        /// On failure, returns Encoding = ReadFieldError.
        /// </summary>
        private FieldType ReadFieldNameAndType()
        {
            var pos = m_stackTop.NameOffset;
            Debug.Assert(m_metaEnd >= pos);

            pos = Array.IndexOf<byte>(m_buf, 0, pos, m_metaEnd - pos);

            if (pos < 0 || m_metaEnd - pos < 2)
            {
                // Missing nul termination or missing encoding.
                return new FieldType { Encoding = ReadFieldError };
            }
            else
            {
                m_stackTop.NameSize = (ushort)(pos - m_stackTop.NameOffset);
                return ReadFieldType(pos + 1);
            }
        }

        /// <summary>
        /// Requires m_metaEnd > typeOffset.
        /// Reads encoding, format, tag starting at m_stackTop.NameOffset.
        /// Updates m_stackTop.NextOffset.
        /// On failure, returns Encoding = ReadFieldError.
        /// </summary>
        private FieldType ReadFieldType(int typeOffset)
        {
            int pos = typeOffset;
            Debug.Assert(m_metaEnd > pos);

            var encoding = (EventFieldEncoding)m_buf[pos];
            pos += 1;

            var format = EventFieldFormat.Default;
            ushort tag = 0;
            if (0 != (encoding & EventFieldEncoding.FlagChain))
            {
                if (m_metaEnd == pos)
                {
                    // Missing format.
                    encoding = ReadFieldError;
                }
                else
                {
                    format = (EventFieldFormat)m_buf[pos];
                    pos += 1;
                    if (0 != (format & EventFieldFormat.FlagChain))
                    {
                        if (m_metaEnd - pos < 2)
                        {
                            // Missing tag.
                            encoding = ReadFieldError;
                        }
                        else
                        {
                            tag = BitConverter.ToUInt16(m_buf, pos);
                            pos += 2;
                        }
                    }
                }
            }

            m_stackTop.NextOffset = pos;
            return new FieldType {
                Encoding = encoding & ~EventFieldEncoding.FlagChain,
                Format = format & ~EventFieldFormat.FlagChain,
                Tag = tag };
        }

        private bool StartArray()
        {
            m_stackTop.ArrayFlags = m_fieldType.Encoding & EncodingCountMask;
            m_fieldType.Encoding = m_fieldType.Encoding & EventFieldEncoding.ValueMask;
            m_elementSize = 0;
            m_itemSizeRaw = 0;
            m_dataPosCooked = m_dataPosRaw;
            m_itemSizeCooked = 0;
            SetState(EventEnumeratorState.ArrayBegin, SubState.ArrayBegin);

            // Determine the m_elementSize value.
            bool movedToItem;
            switch (m_fieldType.Encoding)
            {
                case EventFieldEncoding.Struct:
                    movedToItem = true;
                    goto Done;

                case EventFieldEncoding.Value8:
                    m_elementSize = 1;
                    break;

                case EventFieldEncoding.Value16:
                    m_elementSize = 2;
                    break;

                case EventFieldEncoding.Value32:
                    m_elementSize = 4;
                    break;

                case EventFieldEncoding.Value64:
                    m_elementSize = 8;
                    break;

                case EventFieldEncoding.Value128:
                    m_elementSize = 16;
                    break;

                case EventFieldEncoding.ZStringChar8:
                case EventFieldEncoding.ZStringChar16:
                case EventFieldEncoding.ZStringChar32:
                case EventFieldEncoding.StringLength16Char8:
                case EventFieldEncoding.StringLength16Char16:
                case EventFieldEncoding.StringLength16Char32:
                    movedToItem = true;
                    goto Done;

                case EventFieldEncoding.Invalid:
                    movedToItem = SetErrorState(EventEnumeratorError.InvalidData);
                    goto Done;

                default:
                    movedToItem = SetErrorState(EventEnumeratorError.NotSupported);
                    goto Done;
            }

            // For simple array element types, validate that Count * m_elementSize <= RemainingSize.
            // That way we can skip per-element validation and we can safely expose the array data
            // during ArrayBegin.
            var cbRemaining = m_dataEnd - m_dataPosRaw;
            var cbArray = m_stackTop.ArrayCount * m_elementSize;
            if (cbRemaining < cbArray)
            {
                movedToItem = SetErrorState(EventEnumeratorError.InvalidData);
            }
            else
            {
                m_itemSizeRaw = m_itemSizeCooked = cbArray;
                movedToItem = true;
            }

        Done:

            return movedToItem;
        }

        private void StartStruct()
        {
            Debug.Assert(m_fieldType.Encoding == EventFieldEncoding.Struct);
            m_elementSize = 0;
            m_itemSizeRaw = 0;
            m_dataPosCooked = m_dataPosRaw;
            m_itemSizeCooked = 0;
            SetState(EventEnumeratorState.StructBegin, SubState.StructBegin);
        }

        private bool StartValue()
        {
            var cbRemaining = m_dataEnd - m_dataPosRaw;

            Debug.Assert(m_state == EventEnumeratorState.Value);
            Debug.Assert(m_fieldType.Encoding == 
                ((EventFieldEncoding)m_buf[m_stackTop.NameOffset + m_stackTop.NameSize + 1] & EventFieldEncoding.ValueMask));
            m_dataPosCooked = m_dataPosRaw;
            m_elementSize = 0;

            bool movedToItem;
            switch (m_fieldType.Encoding)
            {
                case EventFieldEncoding.Value8:
                    m_itemSizeRaw = m_itemSizeCooked = m_elementSize = 1;
                    if (m_itemSizeRaw <= cbRemaining)
                    {
                        movedToItem = true;
                        goto Done;
                    }
                    break;

                case EventFieldEncoding.Value16:
                    m_itemSizeRaw = m_itemSizeCooked = m_elementSize = 2;
                    if (m_itemSizeRaw <= cbRemaining)
                    {
                        movedToItem = true;
                        goto Done;
                    }
                    break;

                case EventFieldEncoding.Value32:
                    m_itemSizeRaw = m_itemSizeCooked = m_elementSize = 4;
                    if (m_itemSizeRaw <= cbRemaining)
                    {
                        movedToItem = true;
                        goto Done;
                    }
                    break;

                case EventFieldEncoding.Value64:
                    m_itemSizeRaw = m_itemSizeCooked = m_elementSize = 8;
                    if (m_itemSizeRaw <= cbRemaining)
                    {
                        movedToItem = true;
                        goto Done;
                    }
                    break;

                case EventFieldEncoding.Value128:
                    m_itemSizeRaw = m_itemSizeCooked = m_elementSize = 16;
                    if (m_itemSizeRaw <= cbRemaining)
                    {
                        movedToItem = true;
                        goto Done;
                    }
                    break;

                case EventFieldEncoding.ZStringChar8:
                    StartValueStringNul8();
                    break;

                case EventFieldEncoding.ZStringChar16:
                    StartValueStringNul16();
                    break;

                case EventFieldEncoding.ZStringChar32:
                    StartValueStringNul32();
                    break;

                case EventFieldEncoding.StringLength16Char8:
                    StartValueStringLength16(0);
                    break;

                case EventFieldEncoding.StringLength16Char16:
                    StartValueStringLength16(1);
                    break;

                case EventFieldEncoding.StringLength16Char32:
                    StartValueStringLength16(2);
                    break;

                case EventFieldEncoding.Invalid:
                case EventFieldEncoding.Struct: // Should never happen.
                default:
                    Debug.Assert(m_fieldType.Encoding != EventFieldEncoding.Struct);
                    m_itemSizeRaw = m_itemSizeCooked = 0;
                    movedToItem = SetErrorState(EventEnumeratorError.InvalidData);
                    goto Done;
            }

            if (cbRemaining < m_itemSizeRaw)
            {
                m_itemSizeRaw = m_itemSizeCooked = 0;
                movedToItem = SetErrorState(EventEnumeratorError.InvalidData);
            }
            else
            {
                movedToItem = true;
            }

        Done:

            return movedToItem;
        }

        private void StartValueSimple()
        {
            Debug.Assert(m_stackTop.ArrayIndex < m_stackTop.ArrayCount);
            Debug.Assert(m_stackTop.ArrayFlags != 0);
            Debug.Assert(m_fieldType.Encoding != EventFieldEncoding.Struct);
            Debug.Assert(m_elementSize != 0);
            Debug.Assert(m_itemSizeCooked == m_elementSize);
            Debug.Assert(m_itemSizeRaw == m_elementSize);
            Debug.Assert(m_dataEnd >= m_dataPosRaw + m_itemSizeRaw);
            Debug.Assert(m_state == EventEnumeratorState.Value);
            m_dataPosCooked = m_dataPosRaw;
        }

        private void StartValueStringNul8()
        {
            // cch = strnlen(value, cchRemaining)
            var max = m_dataEnd - m_dataPosRaw;
            var endPos = Array.IndexOf<byte>(m_buf, 0, m_dataPosRaw, max);
            m_itemSizeCooked = (endPos < 0 ? max : endPos - m_dataPosRaw) * sizeof(byte);
            m_itemSizeRaw = m_itemSizeCooked + sizeof(byte);
        }

        private void StartValueStringNul16()
        {
            var endPos = m_dataEnd - sizeof(UInt16);
            for (var pos = m_dataPosRaw; pos <= endPos; pos += sizeof(UInt16))
            {
                if (0 == BitConverter.ToUInt16(m_buf, pos))
                {
                    m_itemSizeCooked = pos - m_dataPosRaw;
                    m_itemSizeRaw = m_itemSizeCooked + sizeof(UInt16);
                    return;
                }
            }

            m_itemSizeCooked = m_dataEnd;
            m_itemSizeRaw = m_dataEnd;
        }

        private void StartValueStringNul32()
        {
            var endPos = m_dataEnd - sizeof(UInt32);
            for (var pos = m_dataPosRaw; pos <= endPos; pos += sizeof(UInt32))
            {
                if (0 == BitConverter.ToUInt32(m_buf, pos))
                {
                    m_itemSizeCooked = pos - m_dataPosRaw;
                    m_itemSizeRaw = m_itemSizeCooked + sizeof(UInt32);
                    return;
                }
            }

            m_itemSizeCooked = m_dataEnd;
            m_itemSizeRaw = m_dataEnd;
        }

        private void StartValueStringLength16(byte charSizeShift)
        {
            var cbRemaining = m_dataEnd - m_dataPosRaw;
            if (cbRemaining < sizeof(ushort))
            {
                m_itemSizeRaw = sizeof(ushort);
            }
            else
            {
                m_dataPosCooked = m_dataPosRaw + sizeof(ushort);

                var cch = BitConverter.ToUInt16(m_buf, m_dataPosRaw);
                m_itemSizeCooked = cch << charSizeShift;
                m_itemSizeRaw = m_itemSizeCooked + sizeof(ushort);
            }
        }

        private void SetState(EventEnumeratorState newState, SubState newSubState)
        {
            m_state = newState;
            m_subState = newSubState;
        }

        private void SetEndState(EventEnumeratorState newState, SubState newSubState)
        {
            m_dataPosCooked = m_dataPosRaw;
            m_itemSizeRaw = 0;
            m_itemSizeCooked = 0;
            m_state = newState;
            m_subState = newSubState;
        }

        private bool SetNoneState(EventEnumeratorError error)
        {
            m_buf = null;
            m_tracepointName = null;
            m_state = EventEnumeratorState.None;
            m_subState = SubState.None;
            m_lastError = error;
            return false;
        }

        private bool SetErrorState(EventEnumeratorError error)
        {
            m_state = EventEnumeratorState.Error;
            m_subState = SubState.Error;
            m_lastError = error;
            return false;
        }

        private static int LowercaseHexToInt(string str, int pos, out ulong val)
        {
            val = 0;
            for (; pos < str.Length; pos += 1)
            {
                uint nibble;
                char ch = str[pos];
                if ('0' <= ch && ch <= '9')
                {
                    nibble = (uint)(ch - '0');
                }
                else if ('a' <= ch && ch <= 'f')
                {
                    nibble = (uint)(ch - 'a' + 10);
                }
                else
                {
                    break;
                }

                val = (val << 4) + nibble;
            }

            return pos;
        }
    }

    /// <summary>
    /// Enumeration states.
    /// </summary>
    public enum EventEnumeratorState : byte
    {
        /// <summary>
        /// After construction, a call to Clear, or a failed StartEvent.
        /// </summary>
        None,

        /// <summary>
        /// After an error has been returned by MoveNext.
        /// </summary>
        Error,

        /// <summary>
        /// Positioned after the last item in the event.
        /// </summary>
        AfterLastItem,

        // MoveNext() is an invalid operation for all states above this line.
        // MoveNext() is a valid operation for all states below this line.

        /// <summary>
        /// Positioned before the first item in the event.
        /// </summary>
        BeforeFirstItem,

        // GetItemInfo() is an invalid operation for all states above this line.
        // GetItemInfo() is a valid operation for all states below this line.

        /// <summary>
        /// Positioned at an item with data (a field or an array element).
        /// </summary>
        Value,

        /// <summary>
        /// Positioned before the first item in an array.
        /// </summary>
        ArrayBegin,

        /// <summary>
        /// Positioned after the last item in an array.
        /// </summary>
        ArrayEnd,

        /// <summary>
        /// Positioned before the first item in a struct.
        /// </summary>
        StructBegin,

        /// <summary>
        /// Positioned after the last item in a struct.
        /// </summary>
        StructEnd,
    }

    /// <summary>
    /// Values for the LastError property.
    /// </summary>
    public enum EventEnumeratorError : byte
    {
        /// <summary>
        /// No error.
        /// </summary>
        Success,

        /// <summary>
        /// Event is smaller than 8 bytes or larger than 2GB,
        /// or tracepointName is longer than 255 characters.
        /// </summary>
        InvalidParameter,

        /// <summary>
        /// Event does not follow the EventHeader naming/layout rules,
        /// is big-endian, has unrecognized flags, or unrecognized types.
        /// </summary>
        NotSupported,

        /// <summary>
        /// Resource usage limit (moveNextLimit) reached.
        /// </summary>
        ImplementationLimit,

        /// <summary>
        /// Event has an out-of-range value.
        /// </summary>
        InvalidData,

        /// <summary>
        /// Event has more than 8 levels of nested structs.
        /// </summary>
        StackOverflow,
    }
}
