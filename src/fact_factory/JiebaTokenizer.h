#pragma once

// 第1层：词法分词模块 (JiebaTokenizer)
// CppJieba 精确模式，关闭 HMM 未登录词猜测
// 加载基础通用词典 + 自定义业务词典 (business_dict.utf8)
// 后置过滤：过滤助词、副词、标点符号；只保留动词、名词类 token

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <unordered_set>

#include "cppjieba/Jieba.hpp"

#include "BusinessTagRegistry.h"

namespace fact_factory {

struct JiebaTokenizerConfig {
    // cppjieba 基础词典目录 (包含 jieba.dict.utf8, hmm_model.utf8 等)
    std::string jieba_dict_dir;
    // 业务自定义词典文件路径
    std::string business_dict_path;
    // idf 词典目录 (可选)
    std::string idf_path;
    // 停用词词典目录 (可选)
    std::string stop_word_path;
};

struct TokenResult {
    std::string word;
    std::string pos_tag;  // 词性标注 (v=动词, n=名词, r=代词, d=副词, u=助词, w=标点, x=非词符)
};

class JiebaTokenizer {
public:
    explicit JiebaTokenizer(const JiebaTokenizerConfig & config)
        : config_(config) {
        jieba_ = std::make_unique<cppjieba::Jieba>(
            config.jieba_dict_dir,   // dict_path (dir with jieba.dict.utf8)
            config.jieba_dict_dir,   // model_path (dir with hmm_model.utf8)
            config.business_dict_path, // user_dict_path (our business_dict.utf8)
            config.idf_path.empty() ? config.jieba_dict_dir : config.idf_path,
            config.stop_word_path.empty() ? config.jieba_dict_dir : config.stop_word_path
        );

        // 额外加载业务词典确保业务词不切碎
        if (!config.business_dict_path.empty()) {
            jieba_->LoadUserDict(config.business_dict_path);
        }

        initPosFilter();
    }

    // 精确模式分词 + 词性标注
    // hmm=false: 关闭 HMM 未登录词猜测，禁止自动生成新词
    std::vector<TokenResult> Tokenize(std::string_view input) const {
        std::vector<TokenResult> results;

        if (input.empty()) {
            return results;
        }

        const std::string sentence(input);

        // 1. 精确模式分词 (hmm=false)
        std::vector<std::string> words;
        jieba_->Cut(sentence, words, false);

        // 2. 词性标注
        std::vector<std::pair<std::string, std::string>> tagged;
        jieba_->Tag(sentence, tagged);

        // 3. 后置过滤：过滤助词、副词、标点符号，只保留动词、名词类
        // tagged 和 words 的分词结果可能不完全一致（Tag 使用 mix_seg_，Cut hmm=false 也使用 mix_seg_）
        // 所以直接使用 tagged 结果
        for (const auto & [word, tag] : tagged) {
            if (shouldKeep(tag)) {
                TokenResult tr;
                tr.word = word;
                tr.pos_tag = tag;
                results.push_back(std::move(tr));
            }
        }

        return results;
    }

    // 仅分词不标注（快速路径）
    std::vector<std::string> CutOnly(std::string_view input) const {
        std::vector<std::string> words;
        if (!input.empty()) {
            jieba_->Cut(std::string(input), words, false);
        }
        return words;
    }

    const JiebaTokenizerConfig & config() const { return config_; }

private:
    void initPosFilter() {
        // 保留的词性: 动词类、名词类
        // v=动词, vn=名动词, n=名词, nr=人名, ns=地名, nt=机构名, nz=其他专名
        // eng=英文, a=形容词 (保留用于 risk/safety 描述)
        kept_tags_ = {
            "v", "vn", "n", "nr", "ns", "nt", "nz",
            "ng", "nl", "nr1", "nr2", "nsf", "nto", "nts", "ntu",
            "eng", "a",
        };
    }

    bool shouldKeep(const std::string & tag) const {
        // 空标签保留（未标注的词，可能是业务词）
        if (tag.empty()) return true;
        return kept_tags_.count(tag) > 0;
    }

    JiebaTokenizerConfig config_;
    std::unique_ptr<cppjieba::Jieba> jieba_;
    std::unordered_set<std::string> kept_tags_;
};

}  // namespace fact_factory
