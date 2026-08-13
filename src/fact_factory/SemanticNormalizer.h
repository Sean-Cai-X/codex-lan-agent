#pragma once

// 第3层：语义同义归一模块 (SemanticNormalizer)
// 英文: WordNet 静态词典 C++ 查表
// 中文: 哈工大同义词词林扩展版 (cilin_ext.txt) 开源词库
// 补丁: business_supplement.txt 覆盖词林缺口
// 最终: 强制过滤，只允许输出 40 个业务词集合内的 tag

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
    // cilin_ext.txt 路径（哈工大同义词词林扩展版）
    // 格式: `Aa01A01= 探查 调研 勘测`，同一编码下全部词语互为同义/近义
    std::string cilin_ext_path;
    // WordNet 静态词典目录 (含 data.noun, index.noun 等)
    std::string wordnet_dir;
    // 业务补充映射表路径
    std::string supplement_path;
    // 最大同义候选数（防止词林返回过多泛义词）
    uint32_t max_synonym_candidates = 16;
};

class SemanticNormalizer {
public:
    explicit SemanticNormalizer(const SemanticNormalizerConfig & config)
        : config_(config) {
        loadCilinExt();
        loadSupplement();
        // WordNet 暂为 stub，等数据文件到位后实现
    }

    // 对候选 tag 列表做同义归一
    // 输入: BusinessTrieMatcher 输出的候选 tag 集合
    // 输出: 收敛后的标准业务 tag 候选集合
    struct NormalizeResult {
        std::vector<std::string> standard_tags;  // 收敛后标准 tag
        bool is_dirty = false;                   // 多义/无法消歧时标记
        std::vector<std::string> warnings;       // 告警信息
    };

    NormalizeResult Normalize(const std::vector<std::string> & candidates) const {
        NormalizeResult result;
        std::unordered_set<std::string> seen;

        for (const auto & candidate : candidates) {
            // 1. 如果已经是标准 tag，直接保留
            if (IsBusinessTag(candidate)) {
                if (seen.insert(candidate).second) {
                    result.standard_tags.push_back(candidate);
                }
                continue;
            }

            // 2. 补丁映射表查找
            auto supIt = supplement_map_.find(candidate);
            if (supIt != supplement_map_.end()) {
                if (seen.insert(supIt->second).second) {
                    result.standard_tags.push_back(supIt->second);
                }
                continue;
            }

            // 3. cilin_ext 同义查找
            auto synIt = cilin_index_.find(candidate);
            if (synIt != cilin_index_.end()) {
                const auto & synonyms = synIt->second;
                int match_count = 0;
                for (const auto & syn : synonyms) {
                    // 强制过滤：只保留落在 40 个业务词集合内的 tag
                    if (IsBusinessTag(syn)) {
                        if (seen.insert(syn).second) {
                            result.standard_tags.push_back(syn);
                        }
                        ++match_count;
                    }
                    // 也检查别名映射
                    std::string resolved = ResolveBusinessTag(syn);
                    if (!resolved.empty() && seen.insert(resolved).second) {
                        result.standard_tags.push_back(std::move(resolved));
                        ++match_count;
                    }
                }
                if (match_count > 1) {
                    // 一词多义：保留全部候选，标记 dirty
                    result.is_dirty = true;
                    result.warnings.push_back(
                        "ambiguous_synonym:" + candidate + ":" + std::to_string(match_count));
                } else if (match_count == 0) {
                    // 词林有同义词但都不在业务白名单内
                    result.warnings.push_back(
                        "synonym_outside_whitelist:" + candidate);
                }
                continue;
            }

            // 4. 无法归一
            result.warnings.push_back("unresolved_candidate:" + candidate);
        }

        // 5. 如果没有任何候选命中，标记 dirty
        if (result.standard_tags.empty() && !candidates.empty()) {
            result.is_dirty = true;
        }

        return result;
    }

    // 单独查询单个词的同义归一结果
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

        // 词林格式: `Aa01A01= 探查 调研 勘测`
        // = 表示同义，# 表示近义，@ 表示相关
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.empty() || line[0] == '#') continue;

            // 找到编码后的分隔符
            size_t code_end = line.find_first_of("=#@");
            if (code_end == std::string::npos) continue;

            std::string code = line.substr(0, code_end);
            std::string words_part = line.substr(code_end + 1);

            // 分词
            std::istringstream iss(words_part);
            std::string word;
            std::vector<std::string> synonyms;
            while (iss >> word) {
                synonyms.push_back(word);
            }

            if (synonyms.empty()) continue;

            // 为每个词建立索引，指向同义词集合
            for (const auto & w : synonyms) {
                auto & entry = cilin_index_[w];
                // 合并同义词（去重）
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

        // 格式: raw_term -> standard_tag
        std::string line;
        while (std::getline(ifs, line)) {
            // 跳过注释和空行
            size_t comment_pos = line.find('#');
            if (comment_pos != std::string::npos) {
                line = line.substr(0, comment_pos);
            }
            // trim
            auto first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) continue;
            auto last = line.find_last_not_of(" \t\r\n");
            line = line.substr(first, last - first + 1);

            size_t arrow = line.find("->");
            if (arrow == std::string::npos) continue;

            std::string raw = line.substr(0, arrow);
            std::string tag = line.substr(arrow + 2);

            // trim both sides
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

    // cilin_ext 索引: word -> 同义词列表
    std::unordered_map<std::string, std::vector<std::string>> cilin_index_;
    bool cilin_loaded_ = false;
    size_t cilin_entry_count_ = 0;

    // 补丁映射表: raw_term -> standard_tag
    std::unordered_map<std::string, std::string> supplement_map_;
};

}  // namespace fact_factory
