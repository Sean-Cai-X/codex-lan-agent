#pragma once


#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <sstream>

#include "BusinessTagRegistry.h"

namespace fact_factory {

struct SemanticNormalizerConfig {
    std::string cilin_ext_path;
    std::string wordnet_dir;
    std::string supplement_path;
    uint32_t max_synonym_candidates = 16;
};

class SemanticNormalizer {
public:
    explicit SemanticNormalizer(const SemanticNormalizerConfig & config)
        : config_(config) {
        loadCilinExt();
        loadSupplement();
    }

    struct NormalizeResult {
        std::vector<std::string> standard_tags;
        bool is_dirty = false;
        std::vector<std::string> warnings;
    };

    NormalizeResult Normalize(const std::vector<std::string> & candidates) const {
        NormalizeResult result;
        std::unordered_set<std::string> seen;

        for (const auto & candidate : candidates) {
            if (IsBusinessTag(candidate)) {
                if (seen.insert(candidate).second) {
                    result.standard_tags.push_back(candidate);
                }
                continue;
            }

            auto supIt = supplement_map_.find(candidate);
            if (supIt != supplement_map_.end()) {
                if (seen.insert(supIt->second).second) {
                    result.standard_tags.push_back(supIt->second);
                }
                continue;
            }

            auto synIt = cilin_index_.find(candidate);
            if (synIt != cilin_index_.end()) {
                const auto & synonyms = synIt->second;
                int match_count = 0;
                for (const auto & syn : synonyms) {
                    if (IsBusinessTag(syn)) {
                        if (seen.insert(syn).second) {
                            result.standard_tags.push_back(syn);
                        }
                        ++match_count;
                    }
                    std::string resolved = ResolveBusinessTag(syn);
                    if (!resolved.empty() && seen.insert(resolved).second) {
                        result.standard_tags.push_back(std::move(resolved));
                        ++match_count;
                    }
                }
                if (match_count > 1) {
                    result.is_dirty = true;
                    result.warnings.push_back(
                        "ambiguous_synonym:" + candidate + ":" + std::to_string(match_count));
                } else if (match_count == 0) {
                    result.warnings.push_back(
                        "synonym_outside_whitelist:" + candidate);
                }
                continue;
            }

            result.warnings.push_back("unresolved_candidate:" + candidate);
        }

        if (result.standard_tags.empty() && !candidates.empty()) {
            result.is_dirty = true;
        }

        return result;
    }

    std::vector<std::string> ResolveSingle(std::string_view word) const {
        std::vector<std::string> input{std::string(word)};
        auto r = Normalize(input);
        return r.standard_tags;
    }

    bool isCilinLoaded() const { return cilin_loaded_; }
    bool isSupplementLoaded() const { return !supplement_map_.empty(); }

private:
    void loadCilinExt() {
        if (config_.cilin_ext_path.empty()) {
            return;
        }

        std::ifstream ifs(config_.cilin_ext_path);
        if (!ifs.is_open()) {
            return;
        }

        std::string line;
        while (std::getline(ifs, line)) {
            if (line.empty() || line[0] == '#') continue;

            size_t code_end = line.find_first_of("=#@");
            if (code_end == std::string::npos) continue;

            std::string code = line.substr(0, code_end);
            std::string words_part = line.substr(code_end + 1);

            std::istringstream iss(words_part);
            std::string word;
            std::vector<std::string> synonyms;
            while (iss >> word) {
                synonyms.push_back(word);
            }

            if (synonyms.empty()) continue;

            for (const auto & w : synonyms) {
                auto & entry = cilin_index_[w];
                for (const auto & s : synonyms) {
                    if (s != w) {
                        entry.push_back(s);
                    }
                }
            }

            ++cilin_entry_count_;
        }

        cilin_loaded_ = (cilin_entry_count_ > 0);
    }

    void loadSupplement() {
        if (config_.supplement_path.empty()) {
            return;
        }

        std::ifstream ifs(config_.supplement_path);
        if (!ifs.is_open()) {
            return;
        }

        std::string line;
        while (std::getline(ifs, line)) {
            size_t comment_pos = line.find('#');
            if (comment_pos != std::string::npos) {
                line = line.substr(0, comment_pos);
            }
            auto first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) continue;
            auto last = line.find_last_not_of(" \t\r\n");
            line = line.substr(first, last - first + 1);

            size_t arrow = line.find("->");
            if (arrow == std::string::npos) continue;

            std::string raw = line.substr(0, arrow);
            std::string tag = line.substr(arrow + 2);

            auto trim = [](std::string & s) {
                auto f = s.find_first_not_of(" \t");
                if (f == std::string::npos) { s.clear(); return; }
                auto l = s.find_last_not_of(" \t");
                s = s.substr(f, l - f + 1);
            };
            trim(raw);
            trim(tag);

            if (!raw.empty() && !tag.empty()) {
                supplement_map_[raw] = tag;
            }
        }
    }

    SemanticNormalizerConfig config_;

    std::unordered_map<std::string, std::vector<std::string>> cilin_index_;
    bool cilin_loaded_ = false;
    size_t cilin_entry_count_ = 0;

    std::unordered_map<std::string, std::string> supplement_map_;
};

}
