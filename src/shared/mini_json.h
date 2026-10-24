#pragma once

// A small JSON reader for the launcher's own needs (GitHub's release answer): objects, arrays, strings, numbers,
// booleans and null, UTF-8 strings with \u escapes. No writer: nothing here produces JSON.
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct JsonValue
{
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;

    // Member `key` of an object, or a shared null value: lookups chain without checks (v["assets"][0]["name"]).
    const JsonValue& operator[](std::string_view key) const;
    const JsonValue& operator[](size_t index) const;
    const std::string& Str() const { return string; } // "" for anything but a string
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
};

// Parses `text`; false (and `error` set) when it isn't valid JSON or nests deeper than 64 levels.
bool ParseJson(std::string_view text, JsonValue& out, std::string* error = nullptr);
