// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

using EventHeaderDecode;
using System;
using Debugger = System.Diagnostics.Debugger;
using Encoding = System.Text.Encoding;
using File = System.IO.File;

namespace EventHeaderDecodeDotNetSample
{
    public static class Program
    {
        public static int Main(string[] args)
        {
            if (Debugger.IsAttached)
            {
                return Run(args);
            }
            else
            {
                try
                {
                    return Run(args);
                }
                catch (Exception ex)
                {
                    Console.WriteLine();
                    Console.WriteLine("Ex: {0}", ex.Message);
                    return 1;
                }
            }
        }

        private static int Run(string[] args)
        {
            Console.OutputEncoding = Encoding.UTF8;
            var enumerator = new EventEnumerator();
            var comma = false;
            foreach (var filename in args)
            {
                Console.WriteLine(comma ? "," : "");
                Console.Write("\"{0}\": {{", filename);
                ProcessFile(enumerator, filename);
                Console.Write(" }");
                comma = true;
            }

            return 0;
        }

        private static void ProcessFile(EventEnumerator e, string filename)
        {
            bool comma = false;

            var bytes = File.ReadAllBytes(filename);
            var pos = 0;
            while (pos < bytes.Length)
            {
                Console.WriteLine(comma ? "," : "");
                if (pos >= bytes.Length - 4)
                {
                    Console.Write("Pos {0}: Unexpected eof.", pos);
                    break;
                }

                var size = BitConverter.ToInt32(bytes, pos);
                if (size < 4 || size > bytes.Length - pos)
                {
                    Console.Write("Pos {0}: Bad size {1}.", pos, size);
                    break;
                }

                var nameStart = pos + 4;
                pos += size;

                var nameEnd = Array.IndexOf<byte>(bytes, 0, nameStart, pos - nameStart);
                if (nameEnd < 0)
                {
                    Console.Write("Pos {0}: Unterminated event name.", nameStart);
                    break;
                }

                var tracepointName = Encoding.UTF8.GetString(bytes, nameStart, nameEnd - nameStart);
                var eventStart = nameEnd + 1;
                if (!e.StartEvent(tracepointName, new ArraySegment<byte>(bytes, eventStart, pos - eventStart)))
                {
                    Console.Write("Pos {0}: TryStartEvent error {1}.", eventStart, e.LastError);
                }
                else
                {
                    Console.Write("  {");
                    comma = false;
                    if (e.MoveNext())
                    {
                        while (true)
                        {
                            var item = e.GetItemInfo();
                            switch (e.State)
                            {
                                case EventEnumeratorState.Value:
                                    WriteJsonItemBegin(comma, item.Name, item.FieldTag, item.ArrayFlags != 0);
                                    Console.Write("\"{0}\"",
                                        item.FormatValue());
                                    comma = true;
                                    break;
                                case EventEnumeratorState.StructBegin:
                                    WriteJsonItemBegin(comma, item.Name, item.FieldTag, item.ArrayFlags != 0);
                                    Console.Write('{');
                                    comma = false;
                                    break;
                                case EventEnumeratorState.StructEnd:
                                    Console.Write(" }");
                                    comma = true;
                                    break;
                                case EventEnumeratorState.ArrayBegin:
                                    WriteJsonItemBegin(comma, item.Name, item.FieldTag);
                                    Console.Write('[');
                                    comma = false;
                                    if (item.ElementSize != 0)
                                    {
                                        // Process the entire array directly without using the enumerator.
                                        var a = item.ValueBytes.Array;
                                        var o = item.ValueBytes.Offset;
                                        var c = item.ElementSize;
                                        for (int i = 0; i != item.ArrayCount; i++)
                                        {
                                            item.ValueBytes = new ArraySegment<byte>(a, o, c);
                                            o += c;
                                            Console.Write("{0} \"{1}\"",
                                                comma ? "," : "",
                                                item.FormatValue());
                                            comma = true;
                                        }

                                        Console.Write(" ]");
                                        comma = true;

                                        // Skip the entire array at once.
                                        if (!e.MoveNextSibling()) // Instead of MoveNext().
                                        {
                                            goto EventDone; // End of event, or error.
                                        }

                                        continue; // Skip the MoveNext().
                                    }
                                    break;
                                case EventEnumeratorState.ArrayEnd:
                                    Console.Write(" ]");
                                    comma = true;
                                    break;
                            }

                            if (!e.MoveNext())
                            {
                                goto EventDone; // End of event, or error.
                            }
                        }
                    }

                EventDone:

                    var ei = e.GetEventInfo();
                    WriteJsonItemBegin(comma, "meta");
                    Console.Write('{');
                    comma = false;

                    WriteJsonItemBegin(comma, "provider");
                    Console.Write("\"{0}\"", ei.ProviderName);
                    comma = true;

                    WriteJsonItemBegin(comma, "event");
                    Console.Write("\"{0}\"", ei.Name);

                    if (ei.Header.Id != 0)
                    {
                        WriteJsonItemBegin(comma, "id");
                        Console.Write("{0}", ei.Header.Id);
                    }

                    if (ei.Header.Version != 0)
                    {
                        WriteJsonItemBegin(comma, "version");
                        Console.Write("{0}", ei.Header.Version);
                    }

                    if (ei.Header.Level != 0)
                    {
                        WriteJsonItemBegin(comma, "level");
                        Console.Write("{0}", (byte)ei.Header.Level);
                    }

                    if (ei.Keyword != 0)
                    {
                        WriteJsonItemBegin(comma, "keyword");
                        Console.Write("\"0x{0:X}\"", ei.Keyword);
                    }

                    if (ei.Header.Opcode != 0)
                    {
                        WriteJsonItemBegin(comma, "opcode");
                        Console.Write("{0}", (byte)ei.Header.Opcode);
                    }

                    if (ei.Header.Tag != 0)
                    {
                        WriteJsonItemBegin(comma, "tag");
                        Console.Write("\"0x{0:X}\"", ei.Header.Tag);
                    }

                    Guid? g;

                    g = ei.ActivityId;
                    if (g.HasValue)
                    {
                        WriteJsonItemBegin(comma, "activity");
                        Console.Write("\"{0}\"", g.Value.ToString());
                    }

                    g = ei.RelatedActivityId;
                    if (g.HasValue)
                    {
                        WriteJsonItemBegin(comma, "relatedActivity");
                        Console.Write("\"{0}\"", g.Value.ToString());
                    }

                    /*
                    var options = ei.Options;
                    if (options.Length != 0)
                    {
                        WriteJsonItemBegin(comma, "options");
                        Console.Write(options);
                    }
                    */

                    // Show the metadata as well.

                    Console.WriteLine(" } },");

                    e.Reset();

                    Console.Write("  {");
                    comma = false;
                    while (e.MoveNextMetadata())
                    {
                        var item = e.GetItemInfo();
                        WriteJsonItemBegin(comma, item.Name, item.FieldTag);
                        Console.Write('{');
                        comma = false;

                        WriteJsonItemBegin(comma, "Encoding");
                        Console.Write("\"{0}\"", item.Encoding.ToString());
                        comma = true;

                        if (item.Format != 0)
                        {
                            if (item.Encoding == EventFieldEncoding.Struct)
                            {
                                WriteJsonItemBegin(comma, "FieldCount");
                                Console.Write((byte)item.Format);
                                comma = true;
                            }
                            else
                            {
                                WriteJsonItemBegin(comma, "Format");
                                Console.Write("\"{0}\"", item.Format.ToString());
                                comma = true;
                            }
                        }

                        if (item.ValueBytes.Count != 0)
                        {
                            WriteJsonItemBegin(comma, "BadValueBytes");
                            Console.Write(item.ValueBytes.Count);
                            comma = true;
                        }

                        if (item.ElementSize != 0)
                        {
                            WriteJsonItemBegin(comma, "BadElementSize");
                            Console.Write(item.ElementSize);
                            comma = true;
                        }

                        if (item.ArrayIndex != 0)
                        {
                            WriteJsonItemBegin(comma, "BadArrayIndex");
                            Console.Write(item.ArrayIndex);
                            comma = true;
                        }

                        if (item.ArrayFlags != 0)
                        {
                            WriteJsonItemBegin(comma, "ArrayCount");
                            Console.Write(item.ArrayCount);
                            comma = true;
                        }
                        else if (item.ArrayCount != 1)
                        {
                            Console.Write("BadArrayCount {0} ", item.ArrayCount);
                        }

                        Console.Write(" }");
                        comma = true;
                    }
                    if (e.LastError != EventEnumeratorError.Success)
                    {
                        WriteJsonItemBegin(comma, "err");
                        Console.Write("\"{0}\"", e.LastError.ToString());
                    }

                    Console.Write(" }");
                    comma = true;
                }
            }
        }

        private static void WriteJsonItemBegin(bool comma, string name, int tag = 0, bool noname = false)
        {
            if (noname)
            {
                Console.Write(comma ? ", " : " ");
            }
            else
            {
                Console.Write(comma ? ", \"" : " \"");
                Console.Write(name);

                if (tag != 0)
                {
                    Console.Write(";tag=0x");
                    Console.Write(tag.ToString("X"));
                }

                Console.Write("\": ");
            }
        }
    }
}
