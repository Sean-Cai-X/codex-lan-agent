#pragma once

#include "StructuredJsonOperations.h"

#include <cstdlib>
#include <string>
#include <unordered_map>

class JsonRequestView {
public:
    explicit JsonRequestView(const std::string & body) : body_(body) {}

    const std::string & body() const {
        return body_;
    }

    std::string GetString(const std::string & key, const std::string & default_value = std::string()) const {
        const std::string value = GetCachedString(key);
        return value.empty() ? default_value : value;
    }

    std::string GetRawJson(const std::string & key, const std::string & default_value = std::string()) const {
        const std::string value = GetCachedRaw(key);
        return value.empty() ? default_value : value;
    }

    bool GetBool(const std::string & key, bool default_value = false) const {
        const std::string raw = GetRawJson(key);
        if (raw == "true") {
            return true;
        }
        if (raw == "false") {
            return false;
        }
        const std::string text = ToLowerAscii(GetString(key));
        if (text == "true" || text == "1" || text == "yes") {
            return true;
        }
        if (text == "false" || text == "0" || text == "no") {
            return false;
        }
        return default_value;
    }

    int GetInt(const std::string & key, int default_value = 0) const {
        const std::string raw = GetRawJson(key);
        const std::string text = raw.empty() ? GetString(key) : raw;
        if (text.empty()) {
            return default_value;
        }
        char * end = nullptr;
        const long value = std::strtol(text.c_str(), &end, 10);
        return end == text.c_str() ? default_value : static_cast<int>(value);
    }

private:
    std::string GetCachedString(const std::string & key) const {
        const auto existing = string_cache_.find(key);
        if (existing != string_cache_.end()) {
            return existing->second;
        }
        const std::string value = ExtractJsonString(body_, key);
        string_cache_[key] = value;
        return value;
    }

    std::string GetCachedRaw(const std::string & key) const {
        const auto existing = raw_cache_.find(key);
        if (existing != raw_cache_.end()) {
            return existing->second;
        }
        const std::string value = ExtractJsonRawValue(body_, key);
        raw_cache_[key] = value;
        return value;
    }

    const std::string & body_;
    mutable std::unordered_map<std::string, std::string> string_cache_;
    mutable std::unordered_map<std::string, std::string> raw_cache_;
};
