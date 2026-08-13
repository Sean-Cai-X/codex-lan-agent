#pragma once

// fact-factory 守卫层集成入口
// LLM 探路输出 ↔ Myrmidon/CLIPS 推理内核中间守卫层
//
// 管线: ByteSanitizer → JiebaTokenizer → BusinessTrieMatcher → SemanticNormalizer
//
// 设计约束:
//   - 守卫是校验层，不是修复层
//   - 带 is_dirty=true 的 fact 仍然送入 CLIPS，不直接丢弃
//   - 消歧、语境判断不在守卫层做，交由 CLIPS 规则层

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <optional>

#include "ByteSanitizer.h"
#include "JiebaTokenizer.h"
#include "BusinessTrieMatcher.h"
#include "SemanticNormalizer.h"
#include "BusinessTagRegistry.h"

namespace fact_factory {

// 最终输出结构体
struct FactFactoryResult {
    std::vector<std::string> standard_tags;  // 收敛后标准业务 tag 候选集合
    bool is_dirty = false;                   // 脏标记
    std::vector<std::string> warnings;       // 告警，供给观测面板日志

    // 便捷方法
    bool hasTags() const { return !standard_tags.empty(); }
    std::string firstTag() const {
        return standard_tags.empty() ? std::string() : standard_tags[0];
    }
    std::string joinedTags(const std::string & sep = ",") const {
        std::string result;
        for (size_t i = 0; i < standard_tags.size(); ++i) {
            if (i > 0) result += sep;
            result += standard_tags[i];
        }
        return result;
    }
};

struct FactFactoryConfig {
    // --- 第0层 ---
    ByteSanitizerConfig byte_sanitizer;

    // --- 第1层 ---
    JiebaTokenizerConfig tokenizer;

    // --- 第2层 ---
    TrieMatcherConfig trie_matcher;

    // --- 第3层 ---
    SemanticNormalizerConfig normalizer;

    // 全局开关
    bool enable_jieba = true;       // false 时跳过第1层，直接用 trie 做精确匹配
    bool enable_semantic = true;    // false 时跳过第3层同义归一
};

class FactFactory {
public:
    explicit FactFactory(const FactFactoryConfig & config)
        : config_(config)
        , sanitizer_(config.byte_sanitizer)
        , trie_matcher_(config.trie_matcher) {

        if (config_.enable_jieba) {
            tokenizer_ = std::make_unique<JiebaTokenizer>(config_.tokenizer);
        }
        if (config_.enable_semantic) {
            normalizer_ = std::make_unique<SemanticNormalizer>(config_.normalizer);
        }
    }

    // 完整管线处理
    FactFactoryResult Process(std::string_view raw_slot_value) const {
        FactFactoryResult result;

        // === 第0层：字节硬过滤 ===
        auto sanitize_result = sanitizer_.SanitizeSlotValue(raw_slot_value);
        if (sanitize_result.is_dirty) {
            result.is_dirty = true;
            result.warnings.push_back("layer0:" + sanitize_result.reason);
        }

        const std::string & clean = sanitize_result.sanitized;
        if (clean.empty()) {
            return result;
        }

        // 快速路径：如果输入已经是标准 tag，直接返回
        if (IsBusinessTag(clean)) {
            result.standard_tags.push_back(clean);
            return result;
        }

        // 快速路径：如果输入是已知别名，直接解析
        std::string alias_resolved = ResolveBusinessTag(clean);
        if (!alias_resolved.empty()) {
            result.standard_tags.push_back(std::move(alias_resolved));
            return result;
        }

        // === 第1层：词法分词 ===
        std::vector<std::string> tokens;
        if (config_.enable_jieba && tokenizer_) {
            auto token_results = tokenizer_->Tokenize(clean);
            tokens.reserve(token_results.size());
            for (const auto & tr : token_results) {
                tokens.push_back(tr.word);
            }
        } else {
            // 无分词器时，直接用 clean 作为单 token
            tokens.push_back(clean);
        }

        if (tokens.empty()) {
            result.is_dirty = true;
            result.warnings.push_back("layer1:empty_tokens_after_segmentation");
            return result;
        }

        // === 第2层：marisa-trie 业务词候选检索 ===
        auto trie_candidates = trie_matcher_.Match(tokens);

        if (trie_candidates.empty()) {
            // 完全无匹配：标记 dirty，不再执行第3层
            result.is_dirty = true;
            result.warnings.push_back("layer2:no_business_match");
            return result;
        }

        // === 第3层：语义同义归一 ===
        if (config_.enable_semantic && normalizer_) {
            auto norm_result = normalizer_->Normalize(trie_candidates);
            result.standard_tags = std::move(norm_result.standard_tags);
            if (norm_result.is_dirty) {
                result.is_dirty = true;
            }
            for (auto & w : norm_result.warnings) {
                result.warnings.push_back("layer3:" + std::move(w));
            }
        } else {
            // 跳过第3层，直接用 trie 结果
            result.standard_tags = std::move(trie_candidates);
        }

        // 最终保障：确保所有输出 tag 都在白名单内
        std::vector<std::string> filtered;
        for (auto & tag : result.standard_tags) {
            if (IsBusinessTag(tag)) {
                filtered.push_back(std::move(tag));
            } else {
                // 尝试最后一次别名解析
                std::string resolved = ResolveBusinessTag(tag);
                if (!resolved.empty()) {
                    filtered.push_back(std::move(resolved));
                } else {
                    result.warnings.push_back("final:tag_outside_whitelist:" + tag);
                }
            }
        }
        result.standard_tags = std::move(filtered);

        return result;
    }

    // 批量处理多个 slot 值
    std::vector<FactFactoryResult> ProcessBatch(const std::vector<std::string> & raw_values) const {
        std::vector<FactFactoryResult> results;
        results.reserve(raw_values.size());
        for (const auto & v : raw_values) {
            results.push_back(Process(v));
        }
        return results;
    }

    const FactFactoryConfig & config() const { return config_; }

private:
    FactFactoryConfig config_;
    ByteSanitizer sanitizer_;  // 第0层 (栈上，无依赖)
    std::unique_ptr<JiebaTokenizer> tokenizer_;    // 第1层
    BusinessTrieMatcher trie_matcher_;             // 第2层 (栈上)
    std::unique_ptr<SemanticNormalizer> normalizer_; // 第3层
};

// 便捷工厂：使用默认配置创建
// 需要调用方提供 jieba_dict_dir 和 business_dict_path
inline std::unique_ptr<FactFactory> CreateFactFactory(
    const std::string & jieba_dict_dir,
    const std::string & business_dict_path,
    const std::string & cilin_ext_path = "",
    const std::string & supplement_path = "",
    const std::string & wordnet_dir = "") {

    FactFactoryConfig config;
    config.tokenizer.jieba_dict_dir = jieba_dict_dir;
    config.tokenizer.business_dict_path = business_dict_path;
    config.normalizer.cilin_ext_path = cilin_ext_path;
    config.normalizer.supplement_path = supplement_path;
    config.normalizer.wordnet_dir = wordnet_dir;

    return std::make_unique<FactFactory>(config);
}

}  // namespace fact_factory
