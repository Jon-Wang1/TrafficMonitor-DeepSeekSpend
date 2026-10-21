#include "json.h"

#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

void AppendUtf8(std::string& out, unsigned int code_point)
{
    if (code_point <= 0x7F) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

class Parser
{
public:
    explicit Parser(const std::string& text) : m_text(text) {}

    bool Parse(JsonValue& value, std::string& error)
    {
        SkipWhitespace();
        if (!ParseValue(value)) {
            error = m_error;
            return false;
        }
        SkipWhitespace();
        if (m_pos != m_text.size()) {
            error = "unexpected trailing data at position " + std::to_string(m_pos);
            return false;
        }
        return true;
    }

private:
    bool ParseValue(JsonValue& value)
    {
        if (++m_depth > 64) {
            m_error = "JSON nesting too deep";
            return false;
        }

        SkipWhitespace();
        if (m_pos >= m_text.size()) {
            m_error = "unexpected end of JSON";
            return false;
        }

        const char ch = m_text[m_pos];
        bool ok = false;
        switch (ch) {
        case '{':
            ok = ParseObject(value);
            break;
        case '[':
            ok = ParseArray(value);
            break;
        case '"':
            value.type = JsonValue::Type::String;
            ok = ParseString(value.string_value);
            break;
        case 't':
            ok = ParseLiteral("true", 4);
            value.type = JsonValue::Type::Bool;
            value.bool_value = true;
            break;
        case 'f':
            ok = ParseLiteral("false", 5);
            value.type = JsonValue::Type::Bool;
            value.bool_value = false;
            break;
        case 'n':
            ok = ParseLiteral("null", 4);
            value.type = JsonValue::Type::Null;
            break;
        default:
            if (ch == '-' || (ch >= '0' && ch <= '9')) {
                ok = ParseNumber(value);
            } else {
                m_error = "unexpected character at position " + std::to_string(m_pos);
            }
            break;
        }

        --m_depth;
        return ok;
    }

    bool ParseObject(JsonValue& value)
    {
        value.type = JsonValue::Type::Object;
        ++m_pos; // '{'
        SkipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == '}') {
            ++m_pos;
            return true;
        }

        while (true) {
            SkipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != '"') {
                m_error = "expected object key at position " + std::to_string(m_pos);
                return false;
            }
            std::string key;
            if (!ParseString(key)) {
                return false;
            }
            SkipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != ':') {
                m_error = "expected ':' at position " + std::to_string(m_pos);
                return false;
            }
            ++m_pos;
            JsonValue child;
            if (!ParseValue(child)) {
                return false;
            }
            value.object[std::move(key)] = std::move(child);
            SkipWhitespace();
            if (m_pos >= m_text.size()) {
                m_error = "unterminated object";
                return false;
            }
            if (m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_text[m_pos] == '}') {
                ++m_pos;
                return true;
            }
            m_error = "expected ',' or '}' at position " + std::to_string(m_pos);
            return false;
        }
    }

    bool ParseArray(JsonValue& value)
    {
        value.type = JsonValue::Type::Array;
        ++m_pos; // '['
        SkipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == ']') {
            ++m_pos;
            return true;
        }

        while (true) {
            JsonValue child;
            if (!ParseValue(child)) {
                return false;
            }
            value.array.push_back(std::move(child));
            SkipWhitespace();
            if (m_pos >= m_text.size()) {
                m_error = "unterminated array";
                return false;
            }
            if (m_text[m_pos] == ',') {
                ++m_pos;
                continue;
            }
            if (m_text[m_pos] == ']') {
                ++m_pos;
                return true;
            }
            m_error = "expected ',' or ']' at position " + std::to_string(m_pos);
            return false;
        }
    }

    bool ParseString(std::string& out)
    {
        ++m_pos; // '"'
        out.clear();
        while (m_pos < m_text.size()) {
            const unsigned char ch = static_cast<unsigned char>(m_text[m_pos++]);
            if (ch == '"') {
                return true;
            }
            if (ch == '\\') {
                if (m_pos >= m_text.size()) {
                    m_error = "unterminated escape sequence";
                    return false;
                }
                const char esc = m_text[m_pos++];
                switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned int code_point = 0;
                    if (!ParseHex4(code_point)) {
                        return false;
                    }
                    if (code_point >= 0xD800 && code_point <= 0xDBFF) {
                        if (m_pos + 6 <= m_text.size() && m_text[m_pos] == '\\' &&
                            m_text[m_pos + 1] == 'u') {
                            m_pos += 2;
                            unsigned int low = 0;
                            if (!ParseHex4(low)) {
                                return false;
                            }
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                code_point = 0x10000 +
                                    ((code_point - 0xD800) << 10) + (low - 0xDC00);
                            } else {
                                AppendUtf8(out, 0xFFFD);
                                code_point = low;
                            }
                        } else {
                            code_point = 0xFFFD;
                        }
                    } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
                        code_point = 0xFFFD;
                    }
                    AppendUtf8(out, code_point);
                    break;
                }
                default:
                    m_error = "invalid escape sequence";
                    return false;
                }
                continue;
            }
            if (ch < 0x20) {
                m_error = "control character in string";
                return false;
            }
            out.push_back(static_cast<char>(ch));
        }
        m_error = "unterminated string";
        return false;
    }

    bool ParseHex4(unsigned int& value)
    {
        if (m_pos + 4 > m_text.size()) {
            m_error = "incomplete unicode escape";
            return false;
        }
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char ch = m_text[m_pos++];
            value <<= 4;
            if (ch >= '0' && ch <= '9') {
                value |= static_cast<unsigned int>(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                value |= static_cast<unsigned int>(ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
                value |= static_cast<unsigned int>(ch - 'A' + 10);
            } else {
                m_error = "invalid unicode escape";
                return false;
            }
        }
        return true;
    }

    bool ParseNumber(JsonValue& value)
    {
        const size_t start = m_pos;
        if (m_pos < m_text.size() && m_text[m_pos] == '-') {
            ++m_pos;
        }
        if (m_pos >= m_text.size()) {
            m_error = "invalid number";
            return false;
        }
        if (m_text[m_pos] == '0') {
            ++m_pos;
        } else if (m_text[m_pos] >= '1' && m_text[m_pos] <= '9') {
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') {
                ++m_pos;
            }
        } else {
            m_error = "invalid number";
            return false;
        }
        if (m_pos < m_text.size() && m_text[m_pos] == '.') {
            ++m_pos;
            if (m_pos >= m_text.size() || m_text[m_pos] < '0' || m_text[m_pos] > '9') {
                m_error = "invalid number fraction";
                return false;
            }
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') {
                ++m_pos;
            }
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) {
                ++m_pos;
            }
            if (m_pos >= m_text.size() || m_text[m_pos] < '0' || m_text[m_pos] > '9') {
                m_error = "invalid number exponent";
                return false;
            }
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') {
                ++m_pos;
            }
        }

        value.type = JsonValue::Type::Number;
        value.number_text = m_text.substr(start, m_pos - start);
        value.number_value = std::strtod(value.number_text.c_str(), nullptr);
        return true;
    }

    bool ParseLiteral(const char* literal, size_t length)
    {
        if (m_pos + length > m_text.size() ||
            std::memcmp(m_text.data() + m_pos, literal, length) != 0) {
            m_error = "invalid literal at position " + std::to_string(m_pos);
            return false;
        }
        m_pos += length;
        return true;
    }

    void SkipWhitespace()
    {
        while (m_pos < m_text.size()) {
            const char ch = m_text[m_pos];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                ++m_pos;
            } else {
                break;
            }
        }
    }

    const std::string& m_text;
    size_t m_pos{0};
    int m_depth{0};
    std::string m_error;
};

} // namespace

const JsonValue* JsonValue::Find(const std::string& key) const
{
    if (type != Type::Object) {
        return nullptr;
    }
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}

const JsonValue* JsonValue::At(size_t index) const
{
    if (type != Type::Array || index >= array.size()) {
        return nullptr;
    }
    return &array[index];
}

std::string JsonValue::AsString() const
{
    if (type == Type::String) {
        return string_value;
    }
    if (type == Type::Number) {
        return number_text;
    }
    return "";
}

double JsonValue::AsDouble() const
{
    if (type == Type::Number) {
        return number_value;
    }
    if (type == Type::String) {
        return std::strtod(string_value.c_str(), nullptr);
    }
    return 0.0;
}

bool JsonValue::AsBool(bool default_value) const
{
    if (type == Type::Bool) {
        return bool_value;
    }
    if (type == Type::Number) {
        return number_value != 0.0;
    }
    return default_value;
}

bool ParseJson(const std::string& text, JsonValue& value, std::string& error)
{
    Parser parser(text);
    return parser.Parse(value, error);
}
