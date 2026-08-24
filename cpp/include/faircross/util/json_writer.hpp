#pragma once

// Minimal JSON writer for the CLI's `--json` output.
//
// The field names and nesting are a published surface: downstream tooling and
// the golden expectations under `fixtures/golden/expected/` both read them, so
// a renamed or reordered field is a breaking change, not a formatting detail.

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace faircross::json {

/// Builds a JSON document incrementally. Values are emitted in insertion order,
/// which keeps the output stable and diffable across runs.
class Writer {
public:
    Writer() { buffer_.reserve(4096); }

    void begin_object() { punctuate(); buffer_ += '{'; fresh_ = true; }
    void end_object() { buffer_ += '}'; fresh_ = false; }
    void begin_array() { punctuate(); buffer_ += '['; fresh_ = true; }
    void end_array() { buffer_ += ']'; fresh_ = false; }

    void key(const std::string& k) {
        punctuate();
        buffer_ += '"';
        buffer_ += escape(k);
        buffer_ += "\":";
        fresh_ = true;
    }

    void value_u64(uint64_t v) { punctuate(); buffer_ += std::to_string(v); fresh_ = false; }
    void value_i64(int64_t v) { punctuate(); buffer_ += std::to_string(v); fresh_ = false; }
    void value_bool(bool v) { punctuate(); buffer_ += v ? "true" : "false"; fresh_ = false; }
    void value_null() { punctuate(); buffer_ += "null"; fresh_ = false; }

    void value_u128(unsigned __int128 v) {
        punctuate();
        if (v == 0) {
            buffer_ += '0';
        } else {
            std::string digits;
            while (v > 0) {
                digits.push_back(static_cast<char>('0' + static_cast<int>(v % 10)));
                v /= 10;
            }
            buffer_.append(digits.rbegin(), digits.rend());
        }
        fresh_ = false;
    }

    void value_string(const std::string& v) {
        punctuate();
        buffer_ += '"';
        buffer_ += escape(v);
        buffer_ += '"';
        fresh_ = false;
    }

    void field_u64(const std::string& k, uint64_t v) { key(k); value_u64(v); }
    void field_u128(const std::string& k, unsigned __int128 v) { key(k); value_u128(v); }
    void field_bool(const std::string& k, bool v) { key(k); value_bool(v); }
    void field_string(const std::string& k, const std::string& v) { key(k); value_string(v); }

    void field_optional_u64(const std::string& k, const std::optional<uint64_t>& v) {
        key(k);
        if (v.has_value()) {
            value_u64(v.value());
        } else {
            value_null();
        }
    }

    [[nodiscard]] const std::string& str() const noexcept { return buffer_; }

private:
    std::string buffer_;
    bool fresh_ = true;

    /// Emits a comma between siblings, but not after an opening brace/bracket
    /// or a key.
    void punctuate() {
        if (!fresh_ && !buffer_.empty()) {
            const char last = buffer_.back();
            if (last != '{' && last != '[' && last != ':') {
                buffer_ += ',';
            }
        }
        fresh_ = false;
    }

    static std::string escape(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (const char c : in) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\t': out += "\\t"; break;
                case '\r': out += "\\r"; break;
                default: out += c; break;
            }
        }
        return out;
    }
};

} // namespace faircross::json
