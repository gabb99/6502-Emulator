// A cursor-based JSON reader, just enough for the ProcessorTests format.
//
// Deliberately not a general-purpose library: it walks the buffer once and
// hands out values as they are encountered, so a 3 MB test file never becomes
// a 60 MB value tree.
#pragma once

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <string_view>

namespace json {

class Cursor {
public:
    Cursor(const char* begin, const char* end) : p_(begin), end_(end) {}

    void skipWhitespace() {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
    }

    [[nodiscard]] bool atEnd() {
        skipWhitespace();
        return p_ >= end_;
    }

    char peek() {
        skipWhitespace();
        if (p_ >= end_) fail("unexpected end of input");
        return *p_;
    }

    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++p_;
    }

    bool consume(char c) {
        if (atEnd() || *p_ != c) return false;
        ++p_;
        return true;
    }

    // Returns a view into the buffer; no escape processing, which the test
    // files never need.
    std::string_view string() {
        expect('"');
        const char* start = p_;
        while (p_ < end_ && *p_ != '"') ++p_;
        if (p_ >= end_) fail("unterminated string");
        const std::string_view text(start, static_cast<std::size_t>(p_ - start));
        ++p_;
        return text;
    }

    long long number() {
        skipWhitespace();
        char*           stop  = nullptr;
        const long long value = std::strtoll(p_, &stop, 10);
        if (stop == p_) fail("expected a number");
        p_ = stop;
        return value;
    }

    // Iterates the members of an object: for (auto key : ...) style, driven by
    // the caller so each value can be parsed with the right type.
    template <class Fn>
    void object(Fn&& onMember) {
        expect('{');
        if (consume('}')) return;
        do {
            const std::string_view key = string();
            expect(':');
            onMember(key);
        } while (consume(','));
        expect('}');
    }

    template <class Fn>
    void array(Fn&& onElement) {
        expect('[');
        if (consume(']')) return;
        do {
            onElement();
        } while (consume(','));
        expect(']');
    }

    [[noreturn]] void fail(const std::string& message) {
        const std::size_t offset = static_cast<std::size_t>(p_ - origin_);
        throw std::runtime_error("json: " + message + " at offset " + std::to_string(offset));
    }

private:
    const char* p_;
    const char* end_;
    const char* origin_ = p_;
};

}  // namespace json
