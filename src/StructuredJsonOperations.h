#pragma once

#include "comm.h"

#include <string>

std::string Trim(const std::string & value);

inline int JsonUnicodeHexDigitValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

inline bool TryReadJsonUnicodeEscape(
    const std::string & text,
    std::size_t index,
    unsigned int * codepoint) {
    if (index + 4 >= text.size()) {
        return false;
    }
    unsigned int value = 0;
    for (std::size_t offset = 1; offset <= 4; ++offset) {
        const int digit = JsonUnicodeHexDigitValue(text[index + offset]);
        if (digit < 0) {
            return false;
        }
        value = (value << 4) | static_cast<unsigned int>(digit);
    }
    *codepoint = value;
    return true;
}

inline void AppendUtf8Codepoint(std::string * output, unsigned int codepoint) {
    if (codepoint <= 0x7F) {
        output->push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        output->push_back(static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F)));
        output->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        output->push_back(static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F)));
        output->push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        output->push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

inline std::string ExtractJsonString(
    const std::string & body,
    const std::string & key) {
    const std::string pattern = "\"" + key + "\"";
    bool in_string = false;
    bool search_escaping = false;
    std::size_t key_pos = std::string::npos;
    for (std::size_t index = 0; index + pattern.size() <= body.size(); ++index) {
        const char current = body[index];
        if (search_escaping) {
            search_escaping = false;
            continue;
        }
        if (current == '\\' && in_string) {
            search_escaping = true;
            continue;
        }
        if (current == '"') {
            if (!in_string && body.compare(index, pattern.size(), pattern) == 0) {
                std::size_t after_key = index + pattern.size();
                while (after_key < body.size() && std::isspace(static_cast<unsigned char>(body[after_key])) != 0) {
                    ++after_key;
                }
                if (after_key < body.size() && body[after_key] == ':') {
                    key_pos = index;
                    break;
                }
            }
            in_string = !in_string;
        }
    }
    if (key_pos == std::string::npos) {
        return "";
    }
    const std::size_t colon_pos = body.find(':', key_pos + pattern.size());
    if (colon_pos == std::string::npos) {
        return "";
    }
    std::size_t quote_start = colon_pos + 1;
    while (quote_start < body.size() && std::isspace(static_cast<unsigned char>(body[quote_start])) != 0) {
        ++quote_start;
    }
    if (quote_start >= body.size() || body[quote_start] != '"') {
        return "";
    }

    std::string value;
    bool escaping = false;
    for (std::size_t index = quote_start + 1; index < body.size(); ++index) {
        const char current = body[index];
        if (escaping) {
            switch (current) {
            case '"':
                value.push_back('"');
                break;
            case '\\':
                value.push_back('\\');
                break;
            case '/':
                value.push_back('/');
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case 'u': {
                unsigned int codepoint = 0;
                if (TryReadJsonUnicodeEscape(body, index, &codepoint)) {
                    AppendUtf8Codepoint(&value, codepoint);
                    index += 4;
                } else {
                    value.push_back(current);
                }
                break;
            }
            default:
                value.push_back(current);
                break;
            }
            escaping = false;
            continue;
        }
        if (current == '\\') {
            escaping = true;
            continue;
        }
        if (current == '"') {
            return value;
        }
        value.push_back(current);
    }
    return "";
}

inline std::string ExtractJsonRawValue(
    const std::string & body,
    const std::string & key) {
    const std::string pattern = "\"" + key + "\"";
    bool in_string = false;
    bool escaping = false;
    std::size_t key_pos = std::string::npos;
    for (std::size_t index = 0; index + pattern.size() <= body.size(); ++index) {
        const char current = body[index];
        if (escaping) {
            escaping = false;
            continue;
        }
        if (current == '\\' && in_string) {
            escaping = true;
            continue;
        }
        if (current == '"') {
            if (!in_string && body.compare(index, pattern.size(), pattern) == 0) {
                std::size_t after_key = index + pattern.size();
                while (after_key < body.size() && std::isspace(static_cast<unsigned char>(body[after_key])) != 0) {
                    ++after_key;
                }
                if (after_key < body.size() && body[after_key] == ':') {
                    key_pos = index;
                    break;
                }
            }
            in_string = !in_string;
        }
    }
    if (key_pos == std::string::npos) {
        return "";
    }
    std::size_t value_pos = body.find(':', key_pos + pattern.size());
    if (value_pos == std::string::npos) {
        return "";
    }
    ++value_pos;
    while (value_pos < body.size() && std::isspace(static_cast<unsigned char>(body[value_pos])) != 0) {
        ++value_pos;
    }
    if (value_pos >= body.size()) {
        return "";
    }

    if (body[value_pos] == '"') {
        std::size_t index = value_pos + 1;
        bool escaping = false;
        for (; index < body.size(); ++index) {
            const char current = body[index];
            if (escaping) {
                escaping = false;
                continue;
            }
            if (current == '\\') {
                escaping = true;
                continue;
            }
            if (current == '"') {
                return body.substr(value_pos, index - value_pos + 1);
            }
        }
        return "";
    }

    std::size_t end_pos = value_pos;
    while (end_pos < body.size()
           && body[end_pos] != ','
           && body[end_pos] != '}'
           && body[end_pos] != '\r'
           && body[end_pos] != '\n') {
        ++end_pos;
    }
    return Trim(body.substr(value_pos, end_pos - value_pos));
}

inline std::string ExtractJsonObjectRaw(
    const std::string & body,
    const std::string & key) {
    const std::string pattern = "\"" + key + "\"";
    bool key_scan_in_string = false;
    bool key_scan_escaping = false;
    std::size_t key_pos = std::string::npos;
    for (std::size_t index = 0; index + pattern.size() <= body.size(); ++index) {
        const char current = body[index];
        if (key_scan_escaping) {
            key_scan_escaping = false;
            continue;
        }
        if (current == '\\' && key_scan_in_string) {
            key_scan_escaping = true;
            continue;
        }
        if (current == '"') {
            if (!key_scan_in_string && body.compare(index, pattern.size(), pattern) == 0) {
                std::size_t after_key = index + pattern.size();
                while (after_key < body.size() && std::isspace(static_cast<unsigned char>(body[after_key])) != 0) {
                    ++after_key;
                }
                if (after_key < body.size() && body[after_key] == ':') {
                    key_pos = index;
                    break;
                }
            }
            key_scan_in_string = !key_scan_in_string;
        }
    }
    if (key_pos == std::string::npos) {
        return "";
    }
    std::size_t value_pos = body.find(':', key_pos + pattern.size());
    if (value_pos == std::string::npos) {
        return "";
    }
    ++value_pos;
    while (value_pos < body.size() && std::isspace(static_cast<unsigned char>(body[value_pos])) != 0) {
        ++value_pos;
    }
    if (value_pos >= body.size()) {
        return "";
    }
    const char open_char = body[value_pos];
    const char close_char = open_char == '{' ? '}' : (open_char == '[' ? ']' : '\0');
    if (close_char == '\0') {
        return "";
    }

    bool in_string = false;
    bool escaping = false;
    int depth = 0;
    for (std::size_t index = value_pos; index < body.size(); ++index) {
        const char current = body[index];
        if (escaping) {
            escaping = false;
            continue;
        }
        if (current == '\\' && in_string) {
            escaping = true;
            continue;
        }
        if (current == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (current == open_char) {
            ++depth;
        } else if (current == close_char) {
            --depth;
            if (depth == 0) {
                return body.substr(value_pos, index - value_pos + 1);
            }
        }
    }
    return "";
}

inline std::string ExtractMcpToolNameFromJsonRpcBody(const std::string & body) {
    const std::string params_object = ExtractJsonObjectRaw(body, "params");
    const std::string nested_name = ExtractJsonString(params_object, "name");
    return nested_name.empty() ? ExtractJsonString(body, "name") : nested_name;
}

inline std::string ExtractMcpToolArgumentsBodyFromJsonRpcBody(const std::string & body) {
    const std::string params_object = ExtractJsonObjectRaw(body, "params");
    const std::string nested_arguments = ExtractJsonObjectRaw(params_object, "arguments");
    if (!nested_arguments.empty()) {
        return nested_arguments;
    }
    const std::string top_level_arguments = ExtractJsonObjectRaw(body, "arguments");
    if (!top_level_arguments.empty()) {
        return top_level_arguments;
    }
    return params_object.empty() ? body : params_object;
}

inline bool ExtractJsonBool(const std::string & body, const std::string & key, bool default_value) {
    const std::string raw = ExtractJsonRawValue(body, key);
    if (raw == "true") {
        return true;
    }
    if (raw == "false") {
        return false;
    }
    const std::string text = ExtractJsonString(body, key);
    if (text == "true" || text == "1") {
        return true;
    }
    if (text == "false" || text == "0") {
        return false;
    }
    return default_value;
}

inline std::string ExtractOutputTextFallback(const CommandResult & result) {
    std::string output_text = GetFieldOrDefault(result, "output_text", "");
    if (!output_text.empty()) {
        return output_text;
    }
    output_text = ExtractJsonString(GetFieldOrDefault(result, "body", ""), "output_text");
    if (!output_text.empty()) {
        return output_text;
    }
    output_text = ExtractJsonString(GetFieldOrDefault(result, "body", ""), "response");
    if (!output_text.empty()) {
        return output_text;
    }
    output_text = ExtractJsonString(GetFieldOrDefault(result, "body", ""), "content");
    if (!output_text.empty()) {
        return output_text;
    }
    output_text = GetFieldOrDefault(result, "body", "");
    if (output_text.size() > 4000) {
        output_text = output_text.substr(0, 4000);
    }
    return output_text;
}

std::string ExtractStructuredConclusionRaw(
    const std::string & body);

std::string ExtractStructuredConclusionString(
    const std::string & body,
    const std::string & key);

inline std::string ExtractStructuredConclusionRawValue(
    const std::string & body,
    const std::string & key) {
    const std::string structured = ExtractStructuredConclusionRaw(body);
    return structured.empty() ? std::string() : ExtractJsonRawValue(structured, key);
}

inline std::string ExtractStructuredConclusionRaw(const std::string & body) {
    return ExtractJsonObjectRaw(body, "structured_conclusion");
}

inline std::string ExtractStructuredConclusionString(
    const std::string & body,
    const std::string & key) {
    const std::string structured = ExtractStructuredConclusionRaw(body);
    return structured.empty() ? std::string() : ExtractJsonString(structured, key);
}
