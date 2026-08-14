#pragma once


#include <string>
#include <string_view>
#include <cstdint>

namespace fact_factory {

struct ByteSanitizerConfig {
    uint32_t max_field_bytes = 32;
    bool escape_clips_syntax = true;
    bool strip_control_chars = true;
    bool validate_utf8 = true;
};

struct ByteSanitizerResult {
    std::string sanitized;
    bool is_dirty = false;
    std::string reason;
};

namespace detail {

inline bool IsValidUtf8(std::string_view str) {
    const auto * p = reinterpret_cast<const uint8_t *>(str.data());
    const auto * end = p + str.size();

    while (p < end) {
        uint8_t c = *p;
        int seq_len = 0;

        if (c <= 0x7F) {
            seq_len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            seq_len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            seq_len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            seq_len = 4;
        } else {
            return false;
        }

        if (p + seq_len > end) {
            return false;
        }

        for (int i = 1; i < seq_len; ++i) {
            if ((p[i] & 0xC0) != 0x80) {
                return false;
            }
        }

        if (seq_len == 2 && c < 0xC2) {
            return false;
        }

        p += seq_len;
    }
    return true;
}

inline std::string EscapeClipsSyntax(std::string_view input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '(':  out += "\\(";  break;
            case ')':  out += "\\)";  break;
            case ';':  out += "\\;";  break;
            case '\\': out += "\\\\"; break;
            default:   out += c;      break;
        }
    }
    return out;
}

}

inline ByteSanitizerResult SanitizeSlotValue(std::string_view raw, const ByteSanitizerConfig & config = {}) {
    ByteSanitizerResult result;

    if (raw.find('\0') != std::string_view::npos) {
        result.is_dirty = true;
        result.reason = "null_byte_detected";
        std::string cleaned;
        cleaned.reserve(raw.size());
        for (char c : raw) {
            if (c != '\0') {
                cleaned += c;
            }
        }
        result.sanitized = std::move(cleaned);
        return result;
    }

    if (raw.size() > config.max_field_bytes) {
        result.is_dirty = true;
        result.reason = "field_too_long:" + std::to_string(raw.size()) + ">" + std::to_string(config.max_field_bytes);
        result.sanitized = std::string(raw);
        return result;
    }

    std::string filtered;
    if (config.strip_control_chars) {
        filtered.reserve(raw.size());
        for (unsigned char c : raw) {
            if (c >= 0x20 || c == 0x09 || c == 0x0A) {
                filtered += static_cast<char>(c);
            }
        }
    } else {
        filtered = std::string(raw);
    }

    if (config.validate_utf8 && !detail::IsValidUtf8(filtered)) {
        result.is_dirty = true;
        result.reason = "invalid_utf8";
        result.sanitized = std::move(filtered);
        return result;
    }

    if (config.escape_clips_syntax) {
        result.sanitized = detail::EscapeClipsSyntax(filtered);
    } else {
        result.sanitized = std::move(filtered);
    }

    return result;
}

}
