#pragma once

#include <map>
#include <string>
#include <vector>

class JsonValue
{
public:
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Type type{Type::Null};
    bool bool_value{false};
    double number_value{0.0};
    std::string number_text;
    std::string string_value;
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;

    bool IsNull() const { return type == Type::Null; }
    bool IsBool() const { return type == Type::Bool; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    const JsonValue* Find(const std::string& key) const;
    const JsonValue* At(size_t index) const;
    std::string AsString() const;
    double AsDouble() const;
    bool AsBool(bool default_value) const;
};

bool ParseJson(const std::string& text, JsonValue& value, std::string& error);
