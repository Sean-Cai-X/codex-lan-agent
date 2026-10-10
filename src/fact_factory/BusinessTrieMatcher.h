#pragma once

// 第2层：marisa-trie 业务词组候选检索
// 仅引入 marisa-trie 库核心，不引入 Rime 输入法上层逻辑
// 预构建内存 Trie 词典，装入 40 个标准业务词
// 输入 token 序列做多候选匹配，产出命中业务候选 tag 集合

#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>

#include "marisa/trie.h"
#include "marisa/keyset.h"
#include "marisa/agent.h"

#include "BusinessTagRegistry.h"

namespace fact_factory {

struct TrieMatcherConfig {
    // 可选：外部补充词表路径（每行一个词）
    std::string extra_words_path;
};

class BusinessTrieMatcher {
public:
    explicit BusinessTrieMatcher(const TrieMatcherConfig & config = {})
        : config_(config) {
        buildTrie();
    }

    // 输入 token 序列，返回命中的业务候选 tag 集合
    // 匹配策略：
    //   1. 精确匹配：token 直接命中 trie 中的标准 tag
    //   2. 别名匹配：token 命中别名，解析为标准 tag
    //   3. 子串匹配：token 是某标准 tag 的一部分（如 "delete" 匹配 "delete" ）
    std::vector<std::string> Match(const std::vector<std::string> & tokens) const {
        std::vector<std::string> candidates;
        std::unordered_set<std::string> seen;  // 去重

        for (const auto & token : tokens) {
            // 1. 别名映射（包括标准词自身）
            std::string resolved = ResolveBusinessTag(token);
            if (!resolved.empty() && seen.insert(resolved).second) {
                candidates.push_back(std::move(resolved));
                continue;
            }

            // 2. trie 精确查找
            if (trie_.has_trie()) {
                marisa::Agent agent;
                agent.set_query(token.c_str(), token.size());
                if (trie_.lookup(agent)) {
                    std::string matched(agent.key().ptr(), agent.key().len());
                    // 再次通过别名映射确保是标准 tag
                    std::string standard = ResolveBusinessTag(matched);
                    if (standard.empty()) {
                        standard = matched;  // trie 里只装了标准 tag，直接用
                    }
                    if (seen.insert(standard).second) {
                        candidates.push_back(std::move(standard));
                    }
                }
            }

            // 3. 前缀匹配：token 可能是标准 tag 的一部分
            // 例如 token="probe" 能匹配 trie 中的 "probe"
            // 已被步骤2覆盖，不再重复
        }

        return candidates;
    }

    // 便捷重载：直接接受 TokenResult 序列
    template <typename TokenIter>
    std::vector<std::string> MatchTokens(TokenIter begin, TokenIter end) const {
        std::vector<std::string> words;
        for (auto it = begin; it != end; ++it) {
            words.push_back(it->word);
        }
        return Match(words);
    }

    // 判断是否有任何 token 命中
    bool HasAnyMatch(const std::vector<std::string> & tokens) const {
        return !Match(tokens).empty();
    }

private:
    void buildTrie() {
        marisa::Keyset keyset;

        // 装入全部 40 个标准业务词
        for (const auto & entry : BusinessTagCatalog()) {
            keyset.push_back(entry.tag.c_str(), entry.tag.size());
        }

        // 装入别名（中英文同义词）
        for (const auto & entry : BusinessTagCatalog()) {
            for (const auto & alias : entry.aliases) {
                keyset.push_back(alias.c_str(), alias.size());
            }
        }

        trie_.build(keyset);
    }

    TrieMatcherConfig config_;
    marisa::Trie trie_;
};

}  // namespace fact_factory
