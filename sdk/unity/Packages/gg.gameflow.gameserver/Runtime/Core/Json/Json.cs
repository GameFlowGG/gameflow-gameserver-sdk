using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace GameFlow
{
    /// <summary>
    /// Minimal hand-rolled JSON for the SDK's tiny surface. No external dependency and no reflection,
    /// so it behaves identically under dotnet and Unity IL2CPP. Parsed values are
    /// <see cref="Dictionary{TKey,TValue}"/> (string→object), <see cref="List{T}"/>, string, double, bool, or null.
    /// </summary>
    internal static class Json
    {
        internal static object Parse(string s)
        {
            int i = 0;
            var v = ParseValue(s, ref i);
            SkipWs(s, ref i);
            if (i != s.Length) throw new FormatException("trailing JSON content");
            return v;
        }

        private static object ParseValue(string s, ref int i)
        {
            SkipWs(s, ref i);
            char c = s[i];
            switch (c)
            {
                case '{': return ParseObject(s, ref i);
                case '[': return ParseArray(s, ref i);
                case '"': return ParseString(s, ref i);
                case 't': i += 4; return true;
                case 'f': i += 5; return false;
                case 'n': i += 4; return null;
                default: return ParseNumber(s, ref i);
            }
        }

        private static Dictionary<string, object> ParseObject(string s, ref int i)
        {
            var d = new Dictionary<string, object>();
            i++; // '{'
            SkipWs(s, ref i);
            if (s[i] == '}') { i++; return d; }
            while (true)
            {
                SkipWs(s, ref i);
                string k = ParseString(s, ref i);
                SkipWs(s, ref i);
                i++; // ':'
                d[k] = ParseValue(s, ref i);
                SkipWs(s, ref i);
                if (s[i] == ',') { i++; continue; }
                i++; // '}'
                break;
            }
            return d;
        }

        private static List<object> ParseArray(string s, ref int i)
        {
            var a = new List<object>();
            i++; // '['
            SkipWs(s, ref i);
            if (s[i] == ']') { i++; return a; }
            while (true)
            {
                a.Add(ParseValue(s, ref i));
                SkipWs(s, ref i);
                if (s[i] == ',') { i++; continue; }
                i++; // ']'
                break;
            }
            return a;
        }

        private static string ParseString(string s, ref int i)
        {
            var sb = new StringBuilder();
            i++; // opening quote
            while (true)
            {
                char c = s[i++];
                if (c == '"') break;
                if (c == '\\')
                {
                    char e = s[i++];
                    switch (e)
                    {
                        case '"': sb.Append('"'); break;
                        case '\\': sb.Append('\\'); break;
                        case '/': sb.Append('/'); break;
                        case 'b': sb.Append('\b'); break;
                        case 'f': sb.Append('\f'); break;
                        case 'n': sb.Append('\n'); break;
                        case 'r': sb.Append('\r'); break;
                        case 't': sb.Append('\t'); break;
                        case 'u':
                            sb.Append((char)int.Parse(s.Substring(i, 4), NumberStyles.HexNumber, CultureInfo.InvariantCulture));
                            i += 4;
                            break;
                        default: sb.Append(e); break;
                    }
                }
                else sb.Append(c);
            }
            return sb.ToString();
        }

        private static object ParseNumber(string s, ref int i)
        {
            int start = i;
            while (i < s.Length && "+-0123456789.eE".IndexOf(s[i]) >= 0) i++;
            return double.Parse(s.Substring(start, i - start), CultureInfo.InvariantCulture);
        }

        private static void SkipWs(string s, ref int i)
        {
            while (i < s.Length && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
        }

        /// <summary>Coerce a parsed value to long, accepting both JSON numbers and numeric strings (int64 fields arrive as strings).</summary>
        internal static long AsLong(object v)
        {
            if (v is string str) return long.Parse(str, CultureInfo.InvariantCulture);
            if (v is double d) return (long)d;
            if (v is null) return 0;
            throw new FormatException("not a number: " + v);
        }

        /// <summary>Serialize a flat object of string fields: <c>Obj() =&gt; "{}"</c>, <c>Obj(("value","x")) =&gt; {"value":"x"}</c>.</summary>
        internal static string Obj(params (string key, string val)[] fields)
        {
            if (fields.Length == 0) return "{}";
            var sb = new StringBuilder("{");
            for (int k = 0; k < fields.Length; k++)
            {
                if (k > 0) sb.Append(',');
                sb.Append('"').Append(Escape(fields[k].key)).Append("\":\"").Append(Escape(fields[k].val)).Append('"');
            }
            return sb.Append('}').ToString();
        }

        private static string Escape(string s)
        {
            var sb = new StringBuilder();
            foreach (char c in s)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default: sb.Append(c); break;
                }
            }
            return sb.ToString();
        }
    }
}
