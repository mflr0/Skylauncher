#include "shared/mini_json.h"
#include <cstdlib>

namespace
{
    const JsonValue g_null;

    struct Reader
    {
        std::string_view text;
        size_t at = 0;
        std::string error;

        bool Fail(const char* message)
        {
            if (error.empty())
                error = std::string(message) + " at byte " + std::to_string(at);
            return false;
        }
        void Space() { while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) at++; }
        bool Literal(const char* word) {
            size_t n = std::char_traits<char>::length(word);
            if (text.substr(at, n) != word)
                return false;
            at += n;
            return true;
        }
        static void Utf8(std::string& out, unsigned code)
        {
            if (code < 0x80) out += char(code);
            else if (code < 0x800) { out += char(0xC0 | code >> 6); out += char(0x80 | (code & 0x3F)); }
            else if (code < 0x10000) { out += char(0xE0 | code >> 12); out += char(0x80 | (code >> 6 & 0x3F)); out += char(0x80 | (code & 0x3F)); }
            else { out += char(0xF0 | code >> 18); out += char(0x80 | (code >> 12 & 0x3F)); out += char(0x80 | (code >> 6 & 0x3F)); out += char(0x80 | (code & 0x3F)); }
        }
        bool Hex4(unsigned& code)
        {
            if (at + 4 > text.size())
                return Fail("short \\u escape");
            code = 0;
            for (int i = 0; i < 4; i++, at++)
            {
                char c = text[at];
                unsigned d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
                if (d == 16)
                    return Fail("bad \\u escape");
                code = code * 16 + d;
            }
            return true;
        }
        bool String(std::string& out)
        {
            at++; // opening quote
            for (; at < text.size(); at++)
            {
                char c = text[at];
                if (c == '"')
                {
                    at++;
                    return true;
                }
                if (c != '\\')
                {
                    out += c;
                    continue;
                }
                if (++at >= text.size())
                    break;
                switch (text[at])
                {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u':
                {
                    at++;
                    unsigned code;
                    if (!Hex4(code))
                        return false;
                    if (code >= 0xD800 && code < 0xDC00 && text.substr(at, 2) == "\\u")
                    {
                        at += 2;
                        unsigned low;
                        if (!Hex4(low))
                            return false;
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    Utf8(out, code);
                    at--; // the loop's increment
                    break;
                }
                default: out += text[at]; break;
                }
            }
            return Fail("unterminated string");
        }
        bool Value(JsonValue& out, int depth)
        {
            if (depth > 64)
                return Fail("nested too deep");
            Space();
            if (at >= text.size())
                return Fail("unexpected end");
            char c = text[at];
            if (c == '{')
            {
                out.type = JsonValue::Type::Object;
                at++;
                Space();
                if (at < text.size() && text[at] == '}')
                    return ++at, true;
                for (;;)
                {
                    Space();
                    std::string key;
                    if (at >= text.size() || text[at] != '"' || !String(key))
                        return Fail("expected a key");
                    Space();
                    if (at >= text.size() || text[at++] != ':')
                        return Fail("expected ':'");
                    if (!Value(out.object[key], depth + 1))
                        return false;
                    Space();
                    if (at < text.size() && text[at] == ',')
                    {
                        at++;
                        continue;
                    }
                    if (at < text.size() && text[at] == '}')
                        return ++at, true;
                    return Fail("expected ',' or '}'");
                }
            }
            if (c == '[')
            {
                out.type = JsonValue::Type::Array;
                at++;
                Space();
                if (at < text.size() && text[at] == ']')
                    return ++at, true;
                for (;;)
                {
                    out.array.emplace_back();
                    if (!Value(out.array.back(), depth + 1))
                        return false;
                    Space();
                    if (at < text.size() && text[at] == ',')
                    {
                        at++;
                        continue;
                    }
                    if (at < text.size() && text[at] == ']')
                        return ++at, true;
                    return Fail("expected ',' or ']'");
                }
            }
            if (c == '"')
            {
                out.type = JsonValue::Type::String;
                return String(out.string);
            }
            if (Literal("true")) { out.type = JsonValue::Type::Bool; out.boolean = true; return true; }
            if (Literal("false")) { out.type = JsonValue::Type::Bool; return true; }
            if (Literal("null")) return true;
            if (c == '-' || (c >= '0' && c <= '9'))
            {
                std::string number;
                while (at < text.size() && (text[at] == '-' || text[at] == '+' || text[at] == '.' || text[at] == 'e' || text[at] == 'E' || (text[at] >= '0' && text[at] <= '9')))
                    number += text[at++];
                out.type = JsonValue::Type::Number;
                out.number = std::strtod(number.c_str(), nullptr);
                return true;
            }
            return Fail("unexpected character");
        }
    };
}

const JsonValue& JsonValue::operator[](std::string_view key) const
{
    if (type == Type::Object)
        if (auto it = object.find(std::string(key)); it != object.end())
            return it->second;
    return g_null;
}

const JsonValue& JsonValue::operator[](size_t index) const
{
    return type == Type::Array && index < array.size() ? array[index] : g_null;
}

bool ParseJson(std::string_view text, JsonValue& out, std::string* error)
{
    Reader reader{ text };
    out = {};
    bool ok = reader.Value(out, 0);
    if (ok)
    {
        reader.Space();
        if (reader.at != text.size())
            ok = reader.Fail("trailing text");
    }
    if (!ok && error != nullptr)
        *error = reader.error;
    return ok;
}
