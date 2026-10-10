#pragma once

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace codex_lan_agent {

struct SemanticIntentLexiconEntry {
    const char * canonical_intent;
    std::vector<const char *> exact_terms;
    std::vector<const char *> phrase_terms;
};

inline const std::vector<SemanticIntentLexiconEntry> & SemanticIntentLexicon() {
    static const std::vector<SemanticIntentLexiconEntry> entries = {
        {
            "comment_cleanup",
            {
                "delete comments",
                "remove comments",
                "strip comments",
                "comment cleanup",
                "cleanup comments",
                "comment_cleanup",
                "delete_comments",
                "remove_comments",
                "strip_comments",
                "comment_cleaning",
                "remove annotation comments",
                "删除注释",
                "清理注释",
                "去除注释",
                "移除注释",
                "删注释",
                "删掉注释",
                "删除代码注释"
            },
            {
                "comment",
                "comments",
                "strip comment",
                "remove comment",
                "delete comment",
                "cleanup comment",
                "注释",
                "\\u6ce8",
                "\\u91ca"
            }
        },
        {
            "code_format",
            {
                "format code",
                "code format",
                "format_code",
                "code_format",
                "formatting",
                "whitespace_cleanup",
                "whitespace cleanup",
                "newline_cleanup",
                "newline cleanup",
                "remove extra newlines",
                "delete extra newlines",
                "clang format",
                "clang-format",
                "删除多余回车换行",
                "删除多余的回车换行",
                "清理多余回车换行",
                "清理空白",
                "格式化代码",
                "代码格式化"
            },
            {
                "format",
                "formatting",
                "whitespace",
                "newline",
                "blank line",
                "clang-format",
                "回车",
                "换行",
                "空行",
                "格式化"
            }
        },
        {
            "source_edit",
            {
                "file_modification",
                "file modification",
                "source_edit",
                "source edit",
                "localized_edit",
                "text_cleaning",
                "修改文件",
                "修改代码",
                "编辑代码"
            },
            {
                "modify",
                "edit",
                "change",
                "修改",
                "编辑",
                "处理"
            }
        }
    };
    return entries;
}

inline std::string SemanticIntentLowerTrim(const std::string & value) {
    std::string trimmed = value;
    const auto first = std::find_if_not(
        trimmed.begin(),
        trimmed.end(),
        [](unsigned char ch) { return std::isspace(ch) != 0; });
    const auto last = std::find_if_not(
        trimmed.rbegin(),
        trimmed.rend(),
        [](unsigned char ch) { return std::isspace(ch) != 0; }).base();
    if (first >= last) {
        return std::string();
    }
    trimmed = std::string(first, last);
    std::transform(
        trimmed.begin(),
        trimmed.end(),
        trimmed.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return trimmed;
}

inline bool SemanticIntentContains(const std::string & haystack, const std::string_view needle) {
    return !needle.empty() && haystack.find(std::string(needle)) != std::string::npos;
}

inline std::string NormalizeIntentBySemanticLexicon(const std::string & raw_intent) {
    const std::string lowered = SemanticIntentLowerTrim(raw_intent);
    if (lowered.empty()) {
        return raw_intent;
    }
    for (const auto & entry : SemanticIntentLexicon()) {
        for (const char * term : entry.exact_terms) {
            if (lowered == SemanticIntentLowerTrim(term)) {
                return entry.canonical_intent;
            }
        }
    }
    return raw_intent;
}

inline int SemanticIntentPhraseScore(
    const std::string & normalized_text,
    const SemanticIntentLexiconEntry & entry) {
    int score = 0;
    for (const char * term : entry.exact_terms) {
        if (SemanticIntentContains(normalized_text, SemanticIntentLowerTrim(term))) {
            score += 4;
        }
    }
    for (const char * term : entry.phrase_terms) {
        if (SemanticIntentContains(normalized_text, SemanticIntentLowerTrim(term))) {
            score += 2;
        }
    }
    return score;
}

inline std::string InferIntentBySemanticLexicon(
    const std::string & primary_intent,
    const std::string & request_text) {
    const std::string normalized_primary = NormalizeIntentBySemanticLexicon(primary_intent);
    if (normalized_primary == "comment_cleanup"
        || normalized_primary == "code_format"
        || normalized_primary == "source_edit") {
        return normalized_primary;
    }
    const std::string joined = SemanticIntentLowerTrim(primary_intent + " " + request_text);
    int best_score = 0;
    std::string best_intent;
    for (const auto & entry : SemanticIntentLexicon()) {
        const int score = SemanticIntentPhraseScore(joined, entry);
        if (score > best_score) {
            best_score = score;
            best_intent = entry.canonical_intent;
        }
    }
    return best_score > 0 ? best_intent : normalized_primary;
}

}  // namespace codex_lan_agent
