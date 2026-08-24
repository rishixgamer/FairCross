#pragma once

// Minimal dependency-free JSON reader.
//
// FairCross keeps third-party dependencies minimal, so both the CLI and the
// conformance suite parse fixtures and golden expectations with this
// reader rather than pulling in a JSON library. It supports exactly the subset
// those files use: objects, arrays, strings, unsigned/negative integers,
// booleans, and null.
//
// It is intended for trusted local fixture files. It is not hardened against
// adversarial input: there is no depth limit, so a deeply nested document can
// exhaust the stack. Do not point it at untrusted data.

#include <cstdint>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace faircross::json {

class Value;

using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

enum class Kind { Null, Bool, Int, String, Array, Object };

class Value {
public:
    Kind kind = Kind::Null;
    bool bool_value = false;
    int64_t int_value = 0;
    std::string string_value;
    Array array_value;
    Object object_value;

    [[nodiscard]] bool is_null() const noexcept { return kind == Kind::Null; }

    [[nodiscard]] const Value& at(const std::string& key) const {
        if (kind != Kind::Object) {
            throw std::runtime_error("json: not an object (key '" + key + "')");
        }
        auto it = object_value.find(key);
        if (it == object_value.end()) {
            throw std::runtime_error("json: missing key '" + key + "'");
        }
        return it->second;
    }

    [[nodiscard]] const Array& array() const {
        if (kind != Kind::Array) throw std::runtime_error("json: not an array");
        return array_value;
    }

    [[nodiscard]] const Object& object() const {
        if (kind != Kind::Object) throw std::runtime_error("json: not an object");
        return object_value;
    }

    [[nodiscard]] uint64_t as_u64() const {
        if (kind != Kind::Int) throw std::runtime_error("json: not an integer");
        if (int_value < 0) throw std::runtime_error("json: negative value where unsigned expected");
        return static_cast<uint64_t>(int_value);
    }

    [[nodiscard]] int64_t as_i64() const {
        if (kind != Kind::Int) throw std::runtime_error("json: not an integer");
        return int_value;
    }

    [[nodiscard]] const std::string& as_string() const {
        if (kind != Kind::String) throw std::runtime_error("json: not a string");
        return string_value;
    }
};

class Parser {
public:
    explicit Parser(std::string text) : text_(std::move(text)), pos_(0) {}

    Value parse() {
        skip_whitespace();
        Value v = parse_value();
        skip_whitespace();
        if (pos_ != text_.size()) throw std::runtime_error("json: trailing content");
        return v;
    }

private:
    std::string text_;
    size_t pos_;

    [[noreturn]] void fail(const std::string& why) const {
        throw std::runtime_error("json: " + why + " at offset " + std::to_string(pos_));
    }

    void skip_whitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    char peek() const {
        if (pos_ >= text_.size()) throw std::runtime_error("json: unexpected end of input");
        return text_[pos_];
    }

    void expect(char c) {
        if (pos_ >= text_.size() || text_[pos_] != c) {
            fail(std::string("expected '") + c + "'");
        }
        ++pos_;
    }

    bool consume_literal(const char* literal) {
        const size_t n = std::string(literal).size();
        if (text_.compare(pos_, n, literal) == 0) {
            pos_ += n;
            return true;
        }
        return false;
    }

    Value parse_value() {
        skip_whitespace();
        const char c = peek();
        switch (c) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': {
                Value v;
                v.kind = Kind::String;
                v.string_value = parse_string();
                return v;
            }
            case 't': {
                if (!consume_literal("true")) fail("bad literal");
                Value v;
                v.kind = Kind::Bool;
                v.bool_value = true;
                return v;
            }
            case 'f': {
                if (!consume_literal("false")) fail("bad literal");
                Value v;
                v.kind = Kind::Bool;
                v.bool_value = false;
                return v;
            }
            case 'n': {
                if (!consume_literal("null")) fail("bad literal");
                return Value{};
            }
            default: return parse_number();
        }
    }

    Value parse_object() {
        expect('{');
        Value v;
        v.kind = Kind::Object;
        skip_whitespace();
        if (peek() == '}') {
            ++pos_;
            return v;
        }
        for (;;) {
            skip_whitespace();
            std::string key = parse_string();
            skip_whitespace();
            expect(':');
            v.object_value.emplace(std::move(key), parse_value());
            skip_whitespace();
            const char c = peek();
            if (c == ',') {
                ++pos_;
                continue;
            }
            if (c == '}') {
                ++pos_;
                return v;
            }
            fail("expected ',' or '}'");
        }
    }

    Value parse_array() {
        expect('[');
        Value v;
        v.kind = Kind::Array;
        skip_whitespace();
        if (peek() == ']') {
            ++pos_;
            return v;
        }
        for (;;) {
            v.array_value.push_back(parse_value());
            skip_whitespace();
            const char c = peek();
            if (c == ',') {
                ++pos_;
                continue;
            }
            if (c == ']') {
                ++pos_;
                return v;
            }
            fail("expected ',' or ']'");
        }
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') return out;
            if (c == '\\') {
                if (pos_ >= text_.size()) fail("truncated escape");
                const char esc = text_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 't': out.push_back('\t'); break;
                    case 'r': out.push_back('\r'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    default: fail("unsupported escape");
                }
                continue;
            }
            out.push_back(c);
        }
        fail("unterminated string");
    }

    Value parse_number() {
        const size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        if (pos_ == start) fail("expected number");
        if (pos_ < text_.size() && (text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E')) {
            // FairCross forbids floating point for all exchange quantities; a
            // non-integral number in a golden expectation is a defect, not a
            // value to be silently truncated.
            fail("floating-point number in fixture");
        }
        Value v;
        v.kind = Kind::Int;
        v.int_value = std::stoll(text_.substr(start, pos_ - start));
        return v;
    }
};

inline Value parse_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("json: cannot open '" + path + "'");
    std::ostringstream buf;
    buf << in.rdbuf();
    return Parser(buf.str()).parse();
}

} // namespace faircross::json
