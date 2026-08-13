#pragma once

// 第0层：字节硬过滤 (ByteSanitizer)
// 纯 C++ 字符串处理，最高优先级，无任何 NLP 库
// C-CLIPS 防 segfault 的唯一屏障

#include <string>
#include <string_view>
#include <cstdint>

namespace fact_factory {

struct ByteSanitizerConfig {
    uint32_t max_field_bytes = 32;      // slot 单字段最大字节
    bool escape_clips_syntax = true;    // 转义 CLIPS 语法特殊字符
    bool strip_control_chars = true;    // 过滤 ASCII 0-31 不可见控制字符
    bool validate_utf8 = true;          // UTF-8 合法性校验
};

struct ByteSanitizerResult {
    std::string sanitized;   // 消毒后字符串
    bool is_dirty = false;   // 脏标记
    std::string reason;      // 脏的原因
};

namespace detail {

// 校验 UTF-8 编码合法性
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
            return false;  // 非法起始字节
        }

        if (p + seq_len > end) {
            return false;  // 序列不完整
        }

        // 校验后续字节 10xxxxxx
        for (int i = 1; i < seq_len; ++i) {
            if ((p[i] & 0xC0) != 0x80) {
                return false;
            }
        }

        // 检查 overlong encoding
        if (seq_len == 2 && c < 0xC2) {
            return false;
        }

        p += seq_len;
    }
    return true;
}

// 转义 CLIPS 语法特殊字符: " ( ) ;
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

}  // namespace detail

inline ByteSanitizerResult SanitizeSlotValue(std::string_view raw, const ByteSanitizerConfig & config = {}) {
    ByteSanitizerResult result;

    // 1. 检测零字节
    if (raw.find('\0') != std::string_view::npos) {
        result.is_dirty = true;
        result.reason = "null_byte_detected";
        // 移除零字节，但仍标记脏
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

    // 2. 长度检查
    if (raw.size() > config.max_field_bytes) {
        result.is_dirty = true;
        result.reason = "field_too_long:" + std::to_string(raw.size()) + ">" + std::to_string(config.max_field_bytes);
        // 超长直接标记 dirty，不做截断复用
        result.sanitized = std::string(raw);
        return result;
    }

    // 3. 过滤控制字符
    std::string filtered;
    if (config.strip_control_chars) {
        filtered.reserve(raw.size());
        for (unsigned char c : raw) {
            // 允许 \t (0x09) 和 \n (0x0A)，过滤其他 0-31
            if (c >= 0x20 || c == 0x09 || c == 0x0A) {
                filtered += static_cast<char>(c);
            }
        }
    } else {
        filtered = std::string(raw);
    }

    // 4. UTF-8 合法性校验
    if (config.validate_utf8 && !detail::IsValidUtf8(filtered)) {
        result.is_dirty = true;
        result.reason = "invalid_utf8";
        result.sanitized = std::move(filtered);
        return result;
    }

    // 5. CLIPS 语法转义
    if (config.escape_clips_syntax) {
        result.sanitized = detail::EscapeClipsSyntax(filtered);
    } else {
        result.sanitized = std::move(filtered);
    }

    return result;
}

}  // namespace fact_factory
