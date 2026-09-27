// MqttJson.h
// @see https://github.com/DelegateMQ/DelegateMQ
// Minimal flat-JSON writer and reader for MqttBridge payload converters.
//
// Covers what dashboards and home-automation tools exchange over MQTT: one
// object of string, number and boolean fields, e.g.
//   {"state":"RUNNING","rpm":1500,"active":true}
// Nested objects and arrays are not supported by Reader; use a full JSON
// library (e.g. RapidJSON) in your converter for those.

#ifndef MQTT_JSON_H
#define MQTT_JSON_H

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace mqttjson {

/// @brief Builds one flat JSON object: Writer().Add("a", 1).Add("b", "x").Str()
class Writer {
public:
    Writer& Add(const char* key, const std::string& value) {
        Key(key);
        Quote(value);
        return *this;
    }
    Writer& Add(const char* key, const char* value) { return Add(key, std::string(value)); }
    Writer& Add(const char* key, bool value) {
        Key(key);
        m_json += value ? "true" : "false";
        return *this;
    }
    Writer& Add(const char* key, int value) { return Add(key, static_cast<long long>(value)); }
    Writer& Add(const char* key, long long value) {
        Key(key);
        m_json += std::to_string(value);
        return *this;
    }
    /// @param decimals Digits after the decimal point; < 0 uses up to 10 significant digits.
    Writer& Add(const char* key, double value, int decimals = -1) {
        Key(key);
        if (!std::isfinite(value)) {
            m_json += "null";       // JSON has no NaN/Inf
            return *this;
        }
        char buf[40];
        if (decimals >= 0)
            snprintf(buf, sizeof(buf), "%.*f", decimals, value);
        else
            snprintf(buf, sizeof(buf), "%.10g", value);
        m_json += buf;
        return *this;
    }
    std::string Str() const { return m_json + "}"; }

private:
    void Key(const char* key) {
        m_json += m_json.size() > 1 ? "," : "";
        Quote(key);
        m_json += ':';
    }
    void Quote(const std::string& s) {
        m_json += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"':  m_json += "\\\""; break;
                case '\\': m_json += "\\\\"; break;
                case '\n': m_json += "\\n"; break;
                case '\r': m_json += "\\r"; break;
                case '\t': m_json += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "\\u%04x", c);
                        m_json += buf;
                    } else {
                        m_json += static_cast<char>(c);
                    }
            }
        }
        m_json += '"';
    }
    std::string m_json = "{";
};

/// @brief Parses one flat JSON object and gives typed access to its fields.
class Reader {
public:
    /// @return false if text isn't a flat JSON object.
    bool Parse(const std::string& text) {
        m_fields.clear();
        m_pos = 0;
        m_text = &text;
        SkipWs();
        if (!Consume('{')) return false;
        SkipWs();
        if (Consume('}')) return AtEnd();
        for (;;) {
            std::string key;
            SkipWs();
            if (!ParseString(key)) return false;
            SkipWs();
            if (!Consume(':')) return false;
            SkipWs();
            Value v;
            if (!ParseValue(v)) return false;
            m_fields[key] = v;
            SkipWs();
            if (Consume(',')) continue;
            if (Consume('}')) return AtEnd();
            return false;
        }
    }

    bool Has(const char* key) const { return m_fields.count(key) != 0; }

    bool GetString(const char* key, std::string& out) const {
        auto it = m_fields.find(key);
        if (it == m_fields.end() || it->second.type != Type::STRING) return false;
        out = it->second.str;
        return true;
    }
    bool GetNumber(const char* key, double& out) const {
        auto it = m_fields.find(key);
        if (it == m_fields.end() || it->second.type != Type::NUMBER) return false;
        out = it->second.num;
        return true;
    }
    bool GetBool(const char* key, bool& out) const {
        auto it = m_fields.find(key);
        if (it == m_fields.end() || it->second.type != Type::BOOL) return false;
        out = it->second.b;
        return true;
    }

private:
    enum class Type { STRING, NUMBER, BOOL, NUL };
    struct Value {
        Type type = Type::NUL;
        std::string str;
        double num = 0;
        bool b = false;
    };

    char Peek() const { return m_pos < m_text->size() ? (*m_text)[m_pos] : '\0'; }
    bool Consume(char c) {
        if (Peek() != c) return false;
        ++m_pos;
        return true;
    }
    void SkipWs() {
        while (m_pos < m_text->size() && (Peek() == ' ' || Peek() == '\t' || Peek() == '\n' || Peek() == '\r'))
            ++m_pos;
    }
    bool AtEnd() {
        SkipWs();
        return m_pos == m_text->size();
    }
    bool Literal(const char* word) {
        size_t n = std::char_traits<char>::length(word);
        if (m_text->compare(m_pos, n, word) != 0) return false;
        m_pos += n;
        return true;
    }
    bool ParseString(std::string& out) {
        if (!Consume('"')) return false;
        out.clear();
        while (m_pos < m_text->size()) {
            char c = (*m_text)[m_pos++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (m_pos >= m_text->size()) return false;
            char e = (*m_text)[m_pos++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    // Basic Multilingual Plane only; enough for field values.
                    if (m_pos + 4 > m_text->size()) return false;
                    unsigned cp = static_cast<unsigned>(std::strtoul(m_text->substr(m_pos, 4).c_str(), nullptr, 16));
                    m_pos += 4;
                    if (cp < 0x80) {
                        out += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool ParseValue(Value& v) {
        char c = Peek();
        if (c == '"') {
            v.type = Type::STRING;
            return ParseString(v.str);
        }
        if (Literal("true")) { v.type = Type::BOOL; v.b = true; return true; }
        if (Literal("false")) { v.type = Type::BOOL; v.b = false; return true; }
        if (Literal("null")) { v.type = Type::NUL; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* start = m_text->c_str() + m_pos;
            char* end = nullptr;
            v.num = std::strtod(start, &end);
            if (end == start) return false;
            m_pos += static_cast<size_t>(end - start);
            v.type = Type::NUMBER;
            return true;
        }
        return false;   // nested object or array
    }

    std::map<std::string, Value> m_fields;
    const std::string* m_text = nullptr;
    size_t m_pos = 0;
};

} // namespace mqttjson

#endif // MQTT_JSON_H
