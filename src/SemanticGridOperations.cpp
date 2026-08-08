#include "SemanticGridOperations.h"

#include "StructuredJsonOperations.h"
#include "comm.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>

namespace codex_lan_agent {
namespace {

struct SemanticFragment {
    std::string fragment_id;
    std::string source_file;
    std::string source_kind;
    std::string section_path;
    std::string content_hash;
    int source_line_start = 0;
    int source_line_end = 0;
    std::string fragment_type;
    std::string content_text;
    std::vector<std::string> keyword_tags;
};

struct SemanticNode {
    std::string node_id;
    std::string layer;
    std::string semantic_name;
    std::string abstract_desc;
    std::string parent_id;
    std::vector<std::string> child_ids;
    std::vector<std::string> keyword_tags;
    std::vector<std::string> source_fragment_ids;
};

struct SemanticEdge {
    std::string edge_id;
    std::string source_node_id;
    std::string target_node_id;
    std::string relation_type;
    std::string layer;
};

struct SemanticGrid {
    std::string grid_id;
    std::string domain;
    std::vector<SemanticFragment> fragments;
    std::vector<SemanticNode> nodes;
    std::vector<SemanticEdge> edges;
};

std::string JsonQuote(const std::string & value) {
    return "\"" + JsonEscape(value) + "\"";
}

std::string ReadTextFile(const std::string & path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool WriteTextFile(const std::filesystem::path & path, const std::string & content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output << content;
    output.close();
    return output.good();
}

std::string NormalizeIdText(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        if (std::isalnum(ch) != 0) {
            return static_cast<char>(std::tolower(ch));
        }
        return '_';
    });
    while (value.find("__") != std::string::npos) {
        value.replace(value.find("__"), 2, "_");
    }
    while (!value.empty() && value.front() == '_') value.erase(value.begin());
    while (!value.empty() && value.back() == '_') value.pop_back();
    return value.empty() ? "semantic" : value;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string StableSemanticHash(const std::string & value) {
    unsigned long long hash = 1469598103934665603ULL;
    for (unsigned char ch : value) {
        hash ^= static_cast<unsigned long long>(ch);
        hash *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << std::hex << hash;
    return out.str();
}

bool EndsWithAny(const std::string & value, const std::vector<std::string> & suffixes) {
    for (const auto & suffix : suffixes) {
        if (value.size() >= suffix.size() &&
            value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return true;
        }
    }
    return false;
}

bool IsSentenceEnd(const std::string & value) {
    static const std::vector<std::string> suffixes = {
        ".", "!", "?", ";",
        "\xE3\x80\x82", "\xEF\xBC\x81", "\xEF\xBC\x9F", "\xEF\xBC\x9B"
    };
    return EndsWithAny(value, suffixes);
}

bool ContainsFuzzy(const std::string & haystack, const std::string & needle) {
    if (needle.empty()) {
        return true;
    }
    if (haystack.find(needle) != std::string::npos) {
        return true;
    }

    std::istringstream terms(needle);
    std::string term;
    bool saw_term = false;
    bool all_terms_match = true;
    while (terms >> term) {
        saw_term = true;
        if (haystack.find(term) == std::string::npos) {
            all_terms_match = false;
            break;
        }
    }
    if (saw_term && all_terms_match) {
        return true;
    }

    std::size_t cursor = 0;
    for (char ch : needle) {
        cursor = haystack.find(ch, cursor);
        if (cursor == std::string::npos) {
            return false;
        }
        ++cursor;
    }
    return true;
}

bool MatchesKeyword(
    const std::string & haystack,
    const std::string & keyword,
    bool fuzzy_match,
    bool regex_match) {
    if (keyword.empty()) {
        return true;
    }
    if (regex_match) {
        try {
            return std::regex_search(haystack, std::regex(keyword, std::regex_constants::icase));
        } catch (...) {
            return false;
        }
    }
    if (fuzzy_match) {
        return ContainsFuzzy(haystack, keyword);
    }
    return haystack.find(keyword) != std::string::npos;
}

std::vector<std::string> SplitLines(const std::string & text) {
    std::vector<std::string> lines;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    if (lines.empty() && !text.empty()) {
        lines.push_back(text);
    }
    return lines;
}

std::string JoinWords(const std::vector<std::string> & words) {
    std::ostringstream output;
    for (std::size_t i = 0; i < words.size(); ++i) {
        if (i > 0) output << ",";
        output << JsonQuote(words[i]);
    }
    return output.str();
}

std::string JoinSectionPath(const std::vector<std::string> & headings) {
    std::ostringstream output;
    for (std::size_t i = 0; i < headings.size(); ++i) {
        if (headings[i].empty()) {
            continue;
        }
        if (output.tellp() > 0) {
            output << " > ";
        }
        output << headings[i];
    }
    return output.str();
}

int MarkdownHeadingLevel(const std::string & trimmed) {
    int level = 0;
    while (level < static_cast<int>(trimmed.size()) && trimmed[static_cast<std::size_t>(level)] == '#') {
        ++level;
    }
    if (level <= 0 || level > 6) {
        return 0;
    }
    if (level < static_cast<int>(trimmed.size()) && std::isspace(static_cast<unsigned char>(trimmed[static_cast<std::size_t>(level)])) != 0) {
        return level;
    }
    return 0;
}

void UpdateSectionPathFromHeading(const std::string & trimmed, std::vector<std::string> * headings) {
    const int level = MarkdownHeadingLevel(trimmed);
    if (level <= 0) {
        return;
    }
    std::string title = Trim(trimmed.substr(static_cast<std::size_t>(level)));
    if (title.empty()) {
        return;
    }
    if (headings->size() < static_cast<std::size_t>(level)) {
        headings->resize(static_cast<std::size_t>(level));
    }
    (*headings)[static_cast<std::size_t>(level - 1)] = title;
    headings->resize(static_cast<std::size_t>(level));
}

std::vector<std::string> ExtractKeywordTags(const std::string & text) {
    const std::string lower = ToLower(text);
    std::vector<std::string> tags;
    const std::vector<std::pair<std::string, std::string>> probes = {
        {"clips", "clips"},
        {"module", "module"},
        {"deftemplate", "deftemplate"},
        {"fact", "fact"},
        {"multislot", "multislot"},
        {"mcp", "mcp"},
        {"索引", "index"},
        {"检索", "retrieval"},
        {"金字塔", "pyramid"},
        {"网格", "grid"},
        {"语义", "semantic"},
        {"原文", "source_trace"},
        {"流程", "flow"},
        {"步骤", "step"},
        {"条件", "condition"},
        {"禁止", "constraint"},
        {"约束", "constraint"},
        {"依赖", "depend"},
        {"互斥", "exclude"},
        {"同义", "synonym"},
        {"时序", "sequence"}
    };
    for (const auto & probe : probes) {
        if (lower.find(probe.first) != std::string::npos &&
            std::find(tags.begin(), tags.end(), probe.second) == tags.end()) {
            tags.push_back(probe.second);
        }
    }
    if (tags.empty()) {
        tags.push_back("semantic");
    }
    return tags;
}

std::string ClassifyFragmentType(const std::string & text) {
    const std::string lower = ToLower(text);
    if (lower.find("禁止") != std::string::npos ||
        lower.find("约束") != std::string::npos ||
        lower.find("必须") != std::string::npos ||
        lower.find("仅允许") != std::string::npos ||
        lower.find("不可") != std::string::npos) {
        return "boundary_rule";
    }
    if (lower.find("条件") != std::string::npos ||
        lower.find("前置") != std::string::npos ||
        lower.find("适用") != std::string::npos ||
        lower.find("如果") != std::string::npos ||
        lower.find("when") != std::string::npos) {
        return "condition_statement";
    }
    if (lower.find("步骤") != std::string::npos ||
        lower.find("流程") != std::string::npos ||
        lower.find("阶段") != std::string::npos ||
        lower.find("执行") != std::string::npos ||
        lower.find("输出") != std::string::npos) {
        return "action_step";
    }
    return "term_definition";
}

std::vector<SemanticFragment> DecomposeTextToFragments(
    const std::string & source_file,
    const std::string & source_kind,
    const std::string & text,
    int max_fragments,
    const std::string & split_strategy,
    int max_fragment_chars,
    int sliding_overlap_chars) {
    if (max_fragments <= 0) {
        max_fragments = 512;
    }
    if (max_fragment_chars <= 0) {
        max_fragment_chars = 900;
    }
    max_fragment_chars = std::max(128, std::min(max_fragment_chars, 4000));

    std::vector<SemanticFragment> fragments;
    std::string current;
    int start_line = 1;
    std::vector<std::string> headings;
    std::string current_section_path;
    auto flush = [&](int end_line) {
        std::string trimmed = Trim(current);
        current.clear();
        if (trimmed.empty() || fragments.size() >= static_cast<std::size_t>(max_fragments)) {
            return;
        }
        const std::size_t hard_limit = static_cast<std::size_t>(std::max(max_fragment_chars, 1200));
        if (trimmed.size() > hard_limit) {
            trimmed.resize(hard_limit);
        }
        SemanticFragment fragment;
        fragment.fragment_id = "frag_" + std::to_string(fragments.size() + 1);
        fragment.source_file = source_file;
        fragment.source_kind = source_kind;
        fragment.section_path = current_section_path;
        fragment.content_hash = StableSemanticHash(ToLower(trimmed));
        fragment.source_line_start = start_line;
        fragment.source_line_end = std::max(start_line, end_line);
        fragment.content_text = trimmed;
        fragment.fragment_type = ClassifyFragmentType(trimmed);
        fragment.keyword_tags = ExtractKeywordTags(trimmed);
        fragments.push_back(fragment);
    };

    const std::string strategy = split_strategy.empty() ? "markdown" : ToLower(split_strategy);
    if (strategy == "sliding_window") {
        const int window = max_fragment_chars;
        const int overlap = std::max(0, std::min(sliding_overlap_chars, window / 2));
        int cursor = 0;
        while (cursor < static_cast<int>(text.size()) && fragments.size() < static_cast<std::size_t>(max_fragments)) {
            current = text.substr(static_cast<std::size_t>(cursor), static_cast<std::size_t>(window));
            start_line = 1;
            current_section_path = "sliding_window:" + std::to_string(fragments.size() + 1);
            flush(1);
            cursor += std::max(1, window - overlap);
        }
        return fragments;
    }

    if (strategy == "sentence") {
        const std::vector<std::string> lines = SplitLines(text);
        for (std::size_t index = 0; index < lines.size(); ++index) {
            const std::string trimmed = Trim(lines[index]);
            if (trimmed.empty()) {
                if (!current.empty()) {
                    flush(static_cast<int>(index));
                }
                continue;
            }
            if (current.empty()) {
                start_line = static_cast<int>(index + 1);
            } else {
                current += " ";
            }
            for (char ch : trimmed) {
                current.push_back(ch);
                if (IsSentenceEnd(current) || static_cast<int>(current.size()) >= max_fragment_chars) {
                    flush(static_cast<int>(index + 1));
                    start_line = static_cast<int>(index + 1);
                }
            }
        }
        if (!current.empty()) {
            flush(static_cast<int>(lines.size()));
        }
        return fragments;
    }

    const std::vector<std::string> lines = SplitLines(text);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const std::string trimmed = Trim(lines[index]);
        const int heading_level = MarkdownHeadingLevel(trimmed);
        const bool markdown_boundary =
            heading_level > 0 ||
            trimmed.rfind("-", 0) == 0 ||
            trimmed.rfind("*", 0) == 0 ||
            (trimmed.size() > 2 && std::isdigit(static_cast<unsigned char>(trimmed[0])) != 0 && trimmed[1] == '.');
        const bool boundary = strategy == "paragraph"
            ? trimmed.empty()
            : (trimmed.empty() || markdown_boundary);
        if (boundary && !current.empty()) {
            flush(static_cast<int>(index));
            start_line = static_cast<int>(index + 1);
        }
        if (!trimmed.empty()) {
            if (heading_level > 0) {
                UpdateSectionPathFromHeading(trimmed, &headings);
                current_section_path = JoinSectionPath(headings);
            } else if (current_section_path.empty()) {
                current_section_path = JoinSectionPath(headings);
            }
            if (current.empty()) {
                start_line = static_cast<int>(index + 1);
            } else {
                current += "\n";
            }
            current += trimmed;
            if (static_cast<int>(current.size()) > max_fragment_chars) {
                flush(static_cast<int>(index + 1));
            }
        }
    }
    if (!current.empty()) {
        flush(static_cast<int>(lines.size()));
    }
    return fragments;
}

std::string SerializeFragmentsJson(const std::vector<SemanticFragment> & fragments) {
    std::ostringstream json;
    json << "[\n";
    for (std::size_t i = 0; i < fragments.size(); ++i) {
        const auto & f = fragments[i];
        if (i > 0) json << ",\n";
        json << "  {\"fragment_id\":" << JsonQuote(f.fragment_id)
             << ",\"source_file\":" << JsonQuote(f.source_file)
             << ",\"source_kind\":" << JsonQuote(f.source_kind)
             << ",\"section_path\":" << JsonQuote(f.section_path)
             << ",\"content_hash\":" << JsonQuote(f.content_hash.empty() ? StableSemanticHash(ToLower(f.content_text)) : f.content_hash)
             << ",\"source_line_start\":" << f.source_line_start
             << ",\"source_line_end\":" << f.source_line_end
             << ",\"fragment_type\":" << JsonQuote(f.fragment_type)
             << ",\"content_text\":" << JsonQuote(f.content_text)
             << ",\"keyword_tags\":[" << JoinWords(f.keyword_tags) << "]}";
    }
    json << "\n]";
    return json.str();
}

std::string SerializeNodesJson(const std::vector<SemanticNode> & nodes) {
    std::ostringstream json;
    json << "[\n";
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto & n = nodes[i];
        if (i > 0) json << ",\n";
        json << "  {\"node_id\":" << JsonQuote(n.node_id)
             << ",\"layer\":" << JsonQuote(n.layer)
             << ",\"semantic_name\":" << JsonQuote(n.semantic_name)
             << ",\"abstract_desc\":" << JsonQuote(n.abstract_desc)
             << ",\"parent_id\":" << JsonQuote(n.parent_id)
             << ",\"child_ids\":[" << JoinWords(n.child_ids) << "]"
             << ",\"keyword_tags\":[" << JoinWords(n.keyword_tags) << "]"
             << ",\"source_fragment_ids\":[" << JoinWords(n.source_fragment_ids) << "]}";
    }
    json << "\n]";
    return json.str();
}

std::string SerializeEdgesJson(const std::vector<SemanticEdge> & edges) {
    std::ostringstream json;
    json << "[\n";
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const auto & e = edges[i];
        if (i > 0) json << ",\n";
        json << "  {\"edge_id\":" << JsonQuote(e.edge_id)
             << ",\"source_node_id\":" << JsonQuote(e.source_node_id)
             << ",\"target_node_id\":" << JsonQuote(e.target_node_id)
             << ",\"relation_type\":" << JsonQuote(e.relation_type)
             << ",\"layer\":" << JsonQuote(e.layer) << "}";
    }
    json << "\n]";
    return json.str();
}

std::string SerializeLayerDistributionJson(const std::vector<SemanticNode> & nodes) {
    std::map<std::string, int> counts;
    for (const auto & node : nodes) {
        ++counts[node.layer];
    }
    std::ostringstream json;
    json << "{";
    bool first = true;
    for (const auto & entry : counts) {
        if (!first) json << ",";
        first = false;
        json << JsonQuote(entry.first) << ":" << entry.second;
    }
    json << "}";
    return json.str();
}

std::string SerializeGridJson(const SemanticGrid & grid) {
    return "{\n"
        "  \"schema_version\":\"semantic_grid_v1\",\n"
        "  \"grid_id\":" + JsonQuote(grid.grid_id) + ",\n"
        "  \"domain\":" + JsonQuote(grid.domain) + ",\n"
        "  \"fragments\":" + SerializeFragmentsJson(grid.fragments) + ",\n"
        "  \"nodes\":" + SerializeNodesJson(grid.nodes) + ",\n"
        "  \"edges\":" + SerializeEdgesJson(grid.edges) + "\n"
        "}";
}

std::string ExtractArrayRaw(const std::string & text, const std::string & key) {
    const std::string pattern = "\"" + key + "\"";
    const std::size_t key_pos = text.find(pattern);
    if (key_pos == std::string::npos) {
        return {};
    }
    const std::size_t colon = text.find(':', key_pos + pattern.size());
    if (colon == std::string::npos) {
        return {};
    }
    std::size_t start = text.find('[', colon + 1);
    if (start == std::string::npos) {
        return {};
    }
    int depth = 0;
    bool in_string = false;
    bool escaping = false;
    for (std::size_t i = start; i < text.size(); ++i) {
        const char ch = text[i];
        if (escaping) {
            escaping = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaping = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '[') ++depth;
        if (ch == ']') {
            --depth;
            if (depth == 0) {
                return text.substr(start, i - start + 1);
            }
        }
    }
    return {};
}

std::vector<std::string> SplitTopLevelObjects(const std::string & array_text) {
    std::vector<std::string> objects;
    std::size_t start = std::string::npos;
    int depth = 0;
    bool in_string = false;
    bool escaping = false;
    for (std::size_t i = 0; i < array_text.size(); ++i) {
        const char ch = array_text[i];
        if (escaping) {
            escaping = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaping = true;
            continue;
        }
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '{') {
            if (depth == 0) start = i;
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos) {
                objects.push_back(array_text.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }
    return objects;
}

std::vector<std::string> ParseStringArray(const std::string & array_text) {
    std::vector<std::string> values;
    bool in_string = false;
    bool escaping = false;
    std::string current;
    for (std::size_t i = 0; i < array_text.size(); ++i) {
        const char ch = array_text[i];
        if (!in_string) {
            if (ch == '"') {
                in_string = true;
                current.clear();
            }
            continue;
        }
        if (escaping) {
            switch (ch) {
            case 'n': current.push_back('\n'); break;
            case 'r': current.push_back('\r'); break;
            case 't': current.push_back('\t'); break;
            case '"': current.push_back('"'); break;
            case '\\': current.push_back('\\'); break;
            default: current.push_back(ch); break;
            }
            escaping = false;
            continue;
        }
        if (ch == '\\') {
            escaping = true;
            continue;
        }
        if (ch == '"') {
            values.push_back(current);
            in_string = false;
            continue;
        }
        current.push_back(ch);
    }
    return values;
}

int ExtractIntField(const std::string & object_text, const std::string & key, int fallback = 0) {
    const std::string raw = ExtractJsonRawValue(object_text, key);
    if (raw.empty()) {
        return fallback;
    }
    try {
        return std::stoi(raw);
    } catch (...) {
        return fallback;
    }
}

std::vector<SemanticFragment> ParseFragmentsJson(const std::string & json_text) {
    std::vector<SemanticFragment> fragments;
    const std::string array = !ExtractArrayRaw(json_text, "fragments").empty()
        ? ExtractArrayRaw(json_text, "fragments")
        : json_text;
    for (const auto & object : SplitTopLevelObjects(array)) {
        SemanticFragment f;
        f.fragment_id = ExtractJsonString(object, "fragment_id");
        f.source_file = ExtractJsonString(object, "source_file");
        f.source_kind = ExtractJsonString(object, "source_kind");
        f.section_path = ExtractJsonString(object, "section_path");
        f.content_hash = ExtractJsonString(object, "content_hash");
        f.source_line_start = ExtractIntField(object, "source_line_start", 0);
        f.source_line_end = ExtractIntField(object, "source_line_end", 0);
        f.fragment_type = ExtractJsonString(object, "fragment_type");
        f.content_text = ExtractJsonString(object, "content_text");
        const std::string tags = ExtractArrayRaw(object, "keyword_tags");
        f.keyword_tags = ParseStringArray(tags);
        if (f.content_hash.empty()) {
            f.content_hash = StableSemanticHash(ToLower(f.content_text));
        }
        if (!f.fragment_id.empty()) {
            fragments.push_back(f);
        }
    }
    return fragments;
}

SemanticGrid ParseGridJson(const std::string & json_text) {
    SemanticGrid grid;
    grid.grid_id = ExtractJsonString(json_text, "grid_id");
    grid.domain = ExtractJsonString(json_text, "domain");
    grid.fragments = ParseFragmentsJson(json_text);

    for (const auto & object : SplitTopLevelObjects(ExtractArrayRaw(json_text, "nodes"))) {
        SemanticNode n;
        n.node_id = ExtractJsonString(object, "node_id");
        n.layer = ExtractJsonString(object, "layer");
        n.semantic_name = ExtractJsonString(object, "semantic_name");
        n.abstract_desc = ExtractJsonString(object, "abstract_desc");
        n.parent_id = ExtractJsonString(object, "parent_id");
        n.child_ids = ParseStringArray(ExtractArrayRaw(object, "child_ids"));
        n.keyword_tags = ParseStringArray(ExtractArrayRaw(object, "keyword_tags"));
        n.source_fragment_ids = ParseStringArray(ExtractArrayRaw(object, "source_fragment_ids"));
        if (!n.node_id.empty()) {
            grid.nodes.push_back(n);
        }
    }

    for (const auto & object : SplitTopLevelObjects(ExtractArrayRaw(json_text, "edges"))) {
        SemanticEdge e;
        e.edge_id = ExtractJsonString(object, "edge_id");
        e.source_node_id = ExtractJsonString(object, "source_node_id");
        e.target_node_id = ExtractJsonString(object, "target_node_id");
        e.relation_type = ExtractJsonString(object, "relation_type");
        e.layer = ExtractJsonString(object, "layer");
        if (!e.edge_id.empty()) {
            grid.edges.push_back(e);
        }
    }
    return grid;
}

void AddEdge(std::vector<SemanticEdge> * edges,
    const std::string & source,
    const std::string & target,
    const std::string & relation,
    const std::string & layer) {
    SemanticEdge edge;
    edge.edge_id = "edge_" + std::to_string(edges->size() + 1);
    edge.source_node_id = source;
    edge.target_node_id = target;
    edge.relation_type = relation;
    edge.layer = layer;
    edges->push_back(edge);
}

SemanticGrid BuildGridFromFragments(
    const std::vector<SemanticFragment> & fragments,
    const std::string & domain_hint,
    const std::string & grid_id_hint) {
    SemanticGrid grid;
    grid.grid_id = grid_id_hint.empty()
        ? "semantic_grid_" + NormalizeIdText(domain_hint.empty() ? "default" : domain_hint)
        : grid_id_hint;
    grid.domain = domain_hint.empty() ? "general" : domain_hint;
    grid.fragments = fragments;

    SemanticNode meta;
    meta.node_id = "L1_META_semantic_grid";
    meta.layer = "L1_META";
    meta.semantic_name = "Semantic Grid Meta Axiom";
    meta.abstract_desc = "Long text semantics are organized as a vertical pyramid plus per-layer semantic grid.";
    meta.keyword_tags = {"semantic", "pyramid", "grid"};
    grid.nodes.push_back(meta);

    SemanticNode domain;
    domain.node_id = "L2_DOMAIN_" + NormalizeIdText(grid.domain);
    domain.layer = "L2_DOMAIN";
    domain.semantic_name = grid.domain;
    domain.abstract_desc = "Domain paradigm collected from decomposed semantic fragments.";
    domain.parent_id = meta.node_id;
    domain.keyword_tags = {"domain", NormalizeIdText(grid.domain)};
    grid.nodes.push_back(domain);
    grid.nodes[0].child_ids.push_back(domain.node_id);
    AddEdge(&grid.edges, meta.node_id, domain.node_id, "contains", "vertical");

    std::map<std::string, std::string> flow_by_type;
    for (const auto & fragment : fragments) {
        const std::string type = fragment.fragment_type.empty() ? "term_definition" : fragment.fragment_type;
        if (flow_by_type.find(type) == flow_by_type.end()) {
            SemanticNode flow;
            flow.node_id = "L3_FLOW_" + NormalizeIdText(type);
            flow.layer = "L3_FLOW";
            flow.semantic_name = type;
            flow.abstract_desc = "Flow/category node for " + type + " fragments.";
            flow.parent_id = domain.node_id;
            flow.keyword_tags = {type, "flow"};
            flow_by_type[type] = flow.node_id;
            grid.nodes.push_back(flow);
            grid.nodes[1].child_ids.push_back(flow.node_id);
            AddEdge(&grid.edges, domain.node_id, flow.node_id, "contains", "vertical");
        }

        SemanticNode atom;
        atom.node_id = "L4_ATOM_" + NormalizeIdText(fragment.fragment_id);
        atom.layer = "L4_ATOM";
        atom.semantic_name = fragment.fragment_type;
        atom.abstract_desc = fragment.content_text.size() > 180
            ? fragment.content_text.substr(0, 180)
            : fragment.content_text;
        atom.parent_id = flow_by_type[type];
        atom.keyword_tags = fragment.keyword_tags;
        atom.source_fragment_ids.push_back(fragment.fragment_id);
        grid.nodes.push_back(atom);
        for (auto & node : grid.nodes) {
            if (node.node_id == atom.parent_id) {
                node.child_ids.push_back(atom.node_id);
                break;
            }
        }
        AddEdge(&grid.edges, atom.parent_id, atom.node_id, "contains", "vertical");

        SemanticNode raw;
        raw.node_id = "L5_RAW_" + NormalizeIdText(fragment.fragment_id);
        raw.layer = "L5_RAW";
        raw.semantic_name = fragment.fragment_id;
        raw.abstract_desc = fragment.content_text;
        raw.parent_id = atom.node_id;
        raw.keyword_tags = fragment.keyword_tags;
        raw.source_fragment_ids.push_back(fragment.fragment_id);
        grid.nodes.push_back(raw);
        grid.nodes[grid.nodes.size() - 2].child_ids.push_back(raw.node_id);
        AddEdge(&grid.edges, atom.node_id, raw.node_id, "source_trace", "vertical");
    }

    std::map<std::string, std::string> previous_atom_by_type;
    for (const auto & node : grid.nodes) {
        if (node.layer != "L4_ATOM") {
            continue;
        }
        const std::string type = node.semantic_name;
        const auto prev = previous_atom_by_type.find(type);
        if (prev != previous_atom_by_type.end()) {
            AddEdge(&grid.edges, prev->second, node.node_id, "sequence", "L4_ATOM");
        }
        previous_atom_by_type[type] = node.node_id;
    }
    return grid;
}

std::string ResolveGridJsonFromParams(const JsonRequestView & params) {
    const std::string direct = params.GetString("semantic_grid_json");
    if (!direct.empty()) {
        return direct;
    }
    std::string path = params.GetString("artifact_grid_json_path");
    const std::string summary_path = params.GetString("artifact_summary_path");
    if (path.empty() && !summary_path.empty()) {
        const std::string summary = ReadTextFile(summary_path);
        path = ExtractJsonString(summary, "artifact_semantic_grid_json_path");
    }
    if (!path.empty()) {
        return ReadTextFile(path);
    }
    return {};
}

std::vector<SemanticFragment> ResolveFragmentsFromParams(const JsonRequestView & params) {
    const std::string fragments_json = params.GetString("fragments_json");
    if (!fragments_json.empty()) {
        return ParseFragmentsJson(fragments_json);
    }
    std::string fragments_path = params.GetString("artifact_fragments_json_path");
    const std::string summary_path = params.GetString("artifact_summary_path");
    if (fragments_path.empty() && !summary_path.empty()) {
        const std::string summary = ReadTextFile(summary_path);
        fragments_path = ExtractJsonString(summary, "artifact_fragments_json_path");
    }
    if (!fragments_path.empty()) {
        return ParseFragmentsJson(ReadTextFile(fragments_path));
    }
    const std::string source_file = params.GetString("source_file");
    std::string source_text = params.GetString("source_text");
    if (source_text.empty() && !source_file.empty()) {
        source_text = ReadTextFile(source_file);
    }
    return DecomposeTextToFragments(
        source_file,
        params.GetString("source_kind", "text"),
        source_text,
        std::max(1, params.GetInt("max_fragments", 512)),
        params.GetString("split_strategy", "markdown"),
        params.GetInt("max_fragment_chars", 900),
        params.GetInt("sliding_overlap_chars", 0));
}

std::vector<SemanticNode> ApplyNodeQuery(
    const SemanticGrid & grid,
    const JsonRequestView & params) {
    const std::string node_id = params.GetString("node_id");
    const std::string layer = params.GetString("layer");
    std::string keyword_text = params.GetString("keyword", params.GetString("query"));
    if (keyword_text.empty()) {
        keyword_text = params.GetString("task_intent");
    }
    if (keyword_text.empty()) {
        keyword_text = params.GetString("flow_stage");
    }
    if (keyword_text.empty()) {
        keyword_text = params.GetString("domain");
    }
    const std::string keyword = ToLower(keyword_text);
    const bool fuzzy_match = params.GetBool("fuzzy_match", false);
    const bool regex_match = params.GetBool("regex_match", false);
    std::vector<SemanticNode> matched;
    for (const auto & node : grid.nodes) {
        if (!node_id.empty() && node.node_id != node_id) {
            continue;
        }
        if (!layer.empty() && node.layer != layer) {
            continue;
        }
        if (!keyword.empty()) {
            std::string haystack = ToLower(node.node_id + " " + node.layer + " " + node.semantic_name + " " + node.abstract_desc);
            for (const auto & tag : node.keyword_tags) {
                haystack += " " + ToLower(tag);
            }
            if (!MatchesKeyword(haystack, keyword, fuzzy_match, regex_match)) {
                continue;
            }
        }
        matched.push_back(node);
    }
    return matched;
}

std::vector<SemanticEdge> ApplyEdgeQuery(
    const SemanticGrid & grid,
    const std::vector<SemanticNode> & nodes,
    const JsonRequestView & params) {
    const std::string relation_type = params.GetString("relation_type");
    const std::string direction = params.GetString("direction", "both");
    std::set<std::string> ids;
    for (const auto & node : nodes) {
        ids.insert(node.node_id);
    }
    std::vector<SemanticEdge> matched;
    for (const auto & edge : grid.edges) {
        if (!relation_type.empty() && edge.relation_type != relation_type) {
            continue;
        }
        const bool outgoing = ids.find(edge.source_node_id) != ids.end();
        const bool incoming = ids.find(edge.target_node_id) != ids.end();
        if ((direction == "out" || direction == "down") && !outgoing) {
            continue;
        }
        if ((direction == "in" || direction == "up") && !incoming) {
            continue;
        }
        if (direction == "both" && !outgoing && !incoming) {
            continue;
        }
        if (direction != "both" && direction != "out" && direction != "down" &&
            direction != "in" && direction != "up" && !outgoing && !incoming) {
            continue;
        }
        matched.push_back(edge);
    }
    return matched;
}

std::vector<SemanticNode> LimitNodes(
    const std::vector<SemanticNode> & nodes,
    int offset,
    int limit) {
    if (offset < 0) offset = 0;
    if (limit < 0) limit = 0;
    if (static_cast<std::size_t>(offset) >= nodes.size()) {
        return {};
    }
    const std::size_t end = limit > 0
        ? std::min(nodes.size(), static_cast<std::size_t>(offset + limit))
        : nodes.size();
    return std::vector<SemanticNode>(nodes.begin() + offset, nodes.begin() + end);
}

std::vector<SemanticFragment> CollectSourceFragments(
    const SemanticGrid & grid,
    const std::string & node_id) {
    std::map<std::string, SemanticNode> nodes_by_id;
    for (const auto & node : grid.nodes) {
        nodes_by_id[node.node_id] = node;
    }
    std::map<std::string, SemanticFragment> fragments_by_id;
    for (const auto & fragment : grid.fragments) {
        fragments_by_id[fragment.fragment_id] = fragment;
    }

    std::vector<SemanticFragment> result;
    std::set<std::string> seen_fragments;
    std::vector<std::string> worklist = {node_id};
    for (std::size_t cursor = 0; cursor < worklist.size(); ++cursor) {
        const auto it = nodes_by_id.find(worklist[cursor]);
        if (it == nodes_by_id.end()) {
            continue;
        }
        for (const auto & fragment_id : it->second.source_fragment_ids) {
            if (seen_fragments.insert(fragment_id).second) {
                const auto fit = fragments_by_id.find(fragment_id);
                if (fit != fragments_by_id.end()) {
                    result.push_back(fit->second);
                }
            }
        }
        worklist.insert(worklist.end(), it->second.child_ids.begin(), it->second.child_ids.end());
    }
    return result;
}

int ComputeSectionPriority(const SemanticNode & node) {
    if (node.layer == "L1_META") return 100;
    if (node.layer == "L2_DOMAIN") return 90;
    if (node.layer == "L3_FLOW") return 80;
    if (node.layer == "L4_ATOM") return 70;
    if (node.layer == "L5_RAW") return 60;
    return 50;
}

std::string SerializeContextSectionsJson(const std::vector<SemanticNode> & nodes) {
    std::ostringstream json;
    json << "[\n";
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto & node = nodes[i];
        if (i > 0) json << ",\n";
        json << "  {\"node_id\":" << JsonQuote(node.node_id)
             << ",\"layer\":" << JsonQuote(node.layer)
             << ",\"section_priority\":" << ComputeSectionPriority(node)
             << ",\"section_weight\":" << JsonQuote(node.layer)
             << ",\"semantic_name\":" << JsonQuote(node.semantic_name)
             << ",\"abstract_desc\":" << JsonQuote(node.abstract_desc) << "}";
    }
    json << "\n]";
    return json.str();
}

void ReassignFragmentIds(
    std::vector<SemanticFragment> * fragments,
    std::size_t starting_index) {
    for (std::size_t i = 0; i < fragments->size(); ++i) {
        (*fragments)[i].fragment_id = "frag_" + std::to_string(starting_index + i + 1);
        if ((*fragments)[i].content_hash.empty()) {
            (*fragments)[i].content_hash = StableSemanticHash(ToLower((*fragments)[i].content_text));
        }
    }
}

std::vector<SemanticNode> NodesForFragments(
    const SemanticGrid & grid,
    const std::set<std::string> & fragment_ids) {
    std::vector<SemanticNode> nodes;
    for (const auto & node : grid.nodes) {
        for (const auto & fragment_id : node.source_fragment_ids) {
            if (fragment_ids.find(fragment_id) != fragment_ids.end()) {
                nodes.push_back(node);
                break;
            }
        }
    }
    return nodes;
}

void SetSuccess(CommandResult * result, const std::string & tool, const std::string & summary) {
    result->ok = true;
    result->exit_code = 0;
    result->fields["status"] = "success";
    result->fields["tool"] = tool;
    result->fields["preflight_status"] = "ready";
    result->fields["summary"] = summary;
}

void SetBlocked(CommandResult * result, const std::string & message, const std::string & reason) {
    result->ok = false;
    result->exit_code = 400;
    result->fields["status"] = "blocked";
    result->fields["error"] = message;
    result->fields["preflight_status"] = "blocked";
    result->fields["preflight_reason_code"] = reason;
    result->fields["summary"] = message;
}

}  // namespace

CommandResult BuildSemanticGridIngestTextResult(
    const AgentConfig &,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string source_file = params.GetString("source_file");
    std::string source_text = params.GetString("source_text");
    if (source_text.empty() && !source_file.empty()) {
        source_text = ReadTextFile(source_file);
    }
    if (source_text.empty()) {
        SetBlocked(&result, "semantic grid ingest blocked: source_text or readable source_file is required", "missing_source_text");
        return result;
    }

    const std::string split_strategy = params.GetString("split_strategy", "markdown");
    const std::string source_kind = params.GetString("source_kind", "text");
    const int max_fragment_chars = params.GetInt("max_fragment_chars", 900);
    const int sliding_overlap_chars = params.GetInt("sliding_overlap_chars", 0);
    const auto fragments = DecomposeTextToFragments(
        source_file,
        source_kind,
        source_text,
        params.GetInt("max_fragments", 512),
        split_strategy,
        max_fragment_chars,
        sliding_overlap_chars);
    const std::string fragments_json = SerializeFragmentsJson(fragments);
    SetSuccess(&result, "semantic_grid_ingest_text", "Semantic fragments ingested: " + std::to_string(fragments.size()));
    result.fields["result"] = "semantic_grid_ingest_success";
    result.fields["ingest_id"] = "ingest_" + NormalizeIdText(source_file.empty() ? "inline" : source_file);
    result.fields["source_file"] = source_file;
    result.fields["source_kind"] = source_kind;
    result.fields["split_strategy"] = split_strategy;
    result.fields["max_fragment_chars"] = std::to_string(std::max(128, max_fragment_chars));
    result.fields["fragment_count"] = std::to_string(fragments.size());
    result.fields["fragments_json"] = fragments_json;

    const std::string output_dir = params.GetString("output_dir");
    if (!output_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(output_dir, ec);
        if (ec || !WriteTextFile(std::filesystem::path(output_dir) / "fragments.json", fragments_json)) {
            SetBlocked(&result, "semantic grid ingest failed while writing output_dir", "output_dir_write_failed");
            return result;
        }
        const std::filesystem::path summary_path = std::filesystem::path(output_dir) / "summary.json";
        const std::string summary_json =
            "{\n"
            "  \"tool\":\"semantic_grid_ingest_text\",\n"
            "  \"artifact_fragments_json_path\":" + JsonQuote((std::filesystem::path(output_dir) / "fragments.json").string()) + ",\n"
            "  \"artifact_summary_json_path\":" + JsonQuote(summary_path.string()) + ",\n"
            "  \"source_kind\":" + JsonQuote(source_kind) + ",\n"
            "  \"split_strategy\":" + JsonQuote(split_strategy) + ",\n"
            "  \"fragment_count\":" + std::to_string(fragments.size()) + "\n"
            "}";
        WriteTextFile(summary_path, summary_json);
        result.fields["artifact_bundle_written"] = "true";
        result.fields["artifact_fragments_json_path"] = (std::filesystem::path(output_dir) / "fragments.json").string();
        result.fields["artifact_summary_json_path"] = summary_path.string();
    }
    return result;
}

CommandResult BuildSemanticGridBuildResult(
    const AgentConfig &,
    const JsonRequestView & params) {
    CommandResult result;
    std::vector<SemanticFragment> fragments = ResolveFragmentsFromParams(params);
    if (fragments.empty()) {
        SetBlocked(&result, "semantic grid build blocked: fragments_json/source_text/source_file is required", "missing_semantic_fragments");
        return result;
    }
    const std::string domain = params.GetString("domain", "general");
    const SemanticGrid grid = BuildGridFromFragments(fragments, domain, params.GetString("grid_id"));
    const std::string grid_json = SerializeGridJson(grid);
    const std::string nodes_json = SerializeNodesJson(grid.nodes);
    const std::string edges_json = SerializeEdgesJson(grid.edges);
    const std::string layer_distribution_json = SerializeLayerDistributionJson(grid.nodes);

    SetSuccess(&result, "semantic_grid_build", "Semantic grid built: " + std::to_string(grid.nodes.size()) + " nodes, " + std::to_string(grid.edges.size()) + " edges");
    result.fields["result"] = "semantic_grid_build_success";
    result.fields["grid_id"] = grid.grid_id;
    result.fields["domain"] = grid.domain;
    result.fields["fragment_count"] = std::to_string(grid.fragments.size());
    result.fields["node_count"] = std::to_string(grid.nodes.size());
    result.fields["edge_count"] = std::to_string(grid.edges.size());
    result.fields["layer_distribution_json"] = layer_distribution_json;
    result.fields["semantic_grid_json"] = grid_json;
    result.fields["nodes_json"] = nodes_json;
    result.fields["edges_json"] = edges_json;
    result.fields["clips_bridge_status"] = "facts_export_deferred";

    const std::string output_dir = params.GetString("output_dir");
    if (!output_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(output_dir, ec);
        const std::filesystem::path dir(output_dir);
        if (ec ||
            !WriteTextFile(dir / "semantic_grid.json", grid_json) ||
            !WriteTextFile(dir / "nodes.json", nodes_json) ||
            !WriteTextFile(dir / "edges.json", edges_json)) {
            SetBlocked(&result, "semantic grid build failed while writing output_dir", "output_dir_write_failed");
            return result;
        }
        const std::filesystem::path summary_path = dir / "summary.json";
        const std::string summary_json =
            "{\n"
            "  \"tool\":\"semantic_grid_build\",\n"
            "  \"grid_id\":" + JsonQuote(grid.grid_id) + ",\n"
            "  \"artifact_semantic_grid_json_path\":" + JsonQuote((dir / "semantic_grid.json").string()) + ",\n"
            "  \"artifact_nodes_json_path\":" + JsonQuote((dir / "nodes.json").string()) + ",\n"
            "  \"artifact_edges_json_path\":" + JsonQuote((dir / "edges.json").string()) + ",\n"
            "  \"artifact_summary_json_path\":" + JsonQuote(summary_path.string()) + ",\n"
            "  \"layer_distribution\":" + layer_distribution_json + ",\n"
            "  \"node_count\":" + std::to_string(grid.nodes.size()) + ",\n"
            "  \"edge_count\":" + std::to_string(grid.edges.size()) + "\n"
            "}";
        WriteTextFile(summary_path, summary_json);
        result.fields["artifact_bundle_written"] = "true";
        result.fields["artifact_semantic_grid_json_path"] = (dir / "semantic_grid.json").string();
        result.fields["artifact_summary_json_path"] = summary_path.string();
    }
    return result;
}

CommandResult BuildSemanticGridQueryResult(
    const AgentConfig &,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string grid_json = ResolveGridJsonFromParams(params);
    if (grid_json.empty()) {
        SetBlocked(&result, "semantic grid query blocked: semantic_grid_json or artifact_summary_path is required", "missing_semantic_grid_json");
        return result;
    }
    const SemanticGrid grid = ParseGridJson(grid_json);
    std::vector<SemanticNode> matched_nodes = ApplyNodeQuery(grid, params);
    const std::size_t matched_count = matched_nodes.size();
    const int offset = std::max(0, params.GetInt("offset", 0));
    const int limit = std::max(0, params.GetInt("limit", 32));
    std::vector<SemanticNode> returned_nodes = LimitNodes(matched_nodes, offset, limit);
    std::vector<SemanticEdge> returned_edges = ApplyEdgeQuery(grid, returned_nodes, params);

    SetSuccess(&result, "semantic_grid_query", "Semantic grid query returned " + std::to_string(returned_nodes.size()) + " nodes");
    result.fields["result"] = "semantic_grid_query_success";
    result.fields["grid_id"] = grid.grid_id;
    result.fields["matched_node_count"] = std::to_string(matched_count);
    result.fields["node_count"] = std::to_string(returned_nodes.size());
    result.fields["edge_count"] = std::to_string(returned_edges.size());
    result.fields["offset"] = std::to_string(offset);
    result.fields["limit"] = std::to_string(limit);
    result.fields["has_more"] = (limit > 0 && static_cast<std::size_t>(offset + limit) < matched_count) ? "true" : "false";
    result.fields["next_offset"] = result.fields["has_more"] == "true" ? std::to_string(offset + limit) : std::to_string(matched_count);
    result.fields["next_offset_or_null"] = result.fields["has_more"] == "true" ? std::to_string(offset + limit) : "null";
    result.fields["pagination_status"] = result.fields["has_more"] == "true" ? "has_more" : "end";
    result.fields["match_mode"] = params.GetBool("regex_match", false)
        ? "regex"
        : (params.GetBool("fuzzy_match", false) ? "fuzzy" : "substring");
    result.fields["matched_nodes_json"] = SerializeNodesJson(returned_nodes);
    result.fields["matched_edges_json"] = SerializeEdgesJson(returned_edges);
    return result;
}

CommandResult BuildSemanticGridTraceSourceResult(
    const AgentConfig &,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string grid_json = ResolveGridJsonFromParams(params);
    const std::string node_id = params.GetString("node_id");
    if (grid_json.empty() || node_id.empty()) {
        SetBlocked(&result, "semantic grid source trace blocked: semantic_grid_json/artifact_summary_path and node_id are required", "missing_trace_args");
        return result;
    }
    const SemanticGrid grid = ParseGridJson(grid_json);
    const auto fragments = CollectSourceFragments(grid, node_id);
    SetSuccess(&result, "semantic_grid_trace_source", "Semantic source trace returned " + std::to_string(fragments.size()) + " fragments");
    result.fields["result"] = "semantic_grid_trace_source_success";
    result.fields["grid_id"] = grid.grid_id;
    result.fields["node_id"] = node_id;
    result.fields["source_fragment_count"] = std::to_string(fragments.size());
    result.fields["source_fragments_json"] = SerializeFragmentsJson(fragments);
    return result;
}

CommandResult BuildSemanticGridContextBundleResult(
    const AgentConfig &,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string grid_json = ResolveGridJsonFromParams(params);
    if (grid_json.empty()) {
        SetBlocked(&result, "semantic grid context bundle blocked: semantic_grid_json or artifact_summary_path is required", "missing_semantic_grid_json");
        return result;
    }
    SemanticGrid grid = ParseGridJson(grid_json);
    std::vector<SemanticNode> nodes = ApplyNodeQuery(grid, params);
    if (nodes.empty()) {
        nodes = grid.nodes;
    }
    const int max_nodes = std::max(1, params.GetInt("max_nodes", 16));
    const int max_chars = std::max(256, params.GetInt("max_chars", 6000));
    if (nodes.size() > static_cast<std::size_t>(max_nodes)) {
        nodes.resize(static_cast<std::size_t>(max_nodes));
    }

    std::ostringstream prompt;
    prompt << "Semantic Grid Context\n";
    prompt << "grid_id: " << grid.grid_id << "\n";
    prompt << "domain: " << grid.domain << "\n";
    for (const auto & node : nodes) {
        if (static_cast<int>(prompt.str().size()) >= max_chars) {
            break;
        }
        prompt << "\n[" << node.layer << "] " << node.node_id << "\n";
        prompt << node.semantic_name << ": " << node.abstract_desc << "\n";
    }
    std::string prompt_text = prompt.str();
    if (static_cast<int>(prompt_text.size()) > max_chars) {
        prompt_text.resize(static_cast<std::size_t>(max_chars));
    }

    SetSuccess(&result, "semantic_grid_context_bundle", "Semantic context bundle built: " + std::to_string(nodes.size()) + " nodes");
    result.fields["result"] = "semantic_grid_context_bundle_success";
    result.fields["grid_id"] = grid.grid_id;
    result.fields["node_count"] = std::to_string(nodes.size());
    result.fields["max_chars"] = std::to_string(max_chars);
    const std::string context_sections_json = SerializeContextSectionsJson(nodes);
    result.fields["context_bundle_json"] =
        "{\"grid_id\":" + JsonQuote(grid.grid_id) +
        ",\"nodes\":" + SerializeNodesJson(nodes) +
        ",\"sections\":" + context_sections_json +
        ",\"prompt_text\":" + JsonQuote(prompt_text) + "}";
    result.fields["context_sections_json"] = context_sections_json;
    result.fields["prompt_sections"] = prompt_text;
    return result;
}

CommandResult BuildSemanticGridIncrementalUpdateResult(
    const AgentConfig &,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string existing_grid_json = ResolveGridJsonFromParams(params);
    if (existing_grid_json.empty()) {
        SetBlocked(&result, "semantic grid incremental update blocked: semantic_grid_json or artifact_summary_path is required", "missing_semantic_grid_json");
        return result;
    }

    SemanticGrid previous_grid = ParseGridJson(existing_grid_json);
    const std::string source_file = params.GetString("source_file");
    std::string source_text = params.GetString("source_text");
    if (source_text.empty() && !source_file.empty()) {
        source_text = ReadTextFile(source_file);
    }
    if (source_text.empty()) {
        SetBlocked(&result, "semantic grid incremental update blocked: source_text or readable source_file is required", "missing_source_text");
        return result;
    }

    std::vector<SemanticFragment> incoming = DecomposeTextToFragments(
        source_file,
        params.GetString("source_kind", "incremental_text"),
        source_text,
        std::max(1, params.GetInt("max_fragments", 512)),
        params.GetString("split_strategy", "markdown"),
        params.GetInt("max_fragment_chars", 900),
        params.GetInt("sliding_overlap_chars", 0));

    std::unordered_set<std::string> existing_hashes;
    for (const auto & fragment : previous_grid.fragments) {
        const std::string hash = fragment.content_hash.empty()
            ? StableSemanticHash(ToLower(fragment.content_text))
            : fragment.content_hash;
        existing_hashes.insert(hash);
    }

    const bool dedupe_existing = params.GetBool("dedupe_existing", true);
    std::vector<SemanticFragment> added;
    int skipped_duplicate_count = 0;
    for (auto fragment : incoming) {
        if (fragment.content_hash.empty()) {
            fragment.content_hash = StableSemanticHash(ToLower(fragment.content_text));
        }
        if (dedupe_existing && existing_hashes.find(fragment.content_hash) != existing_hashes.end()) {
            ++skipped_duplicate_count;
            continue;
        }
        existing_hashes.insert(fragment.content_hash);
        added.push_back(fragment);
    }
    ReassignFragmentIds(&added, previous_grid.fragments.size());

    std::vector<SemanticFragment> combined = previous_grid.fragments;
    combined.insert(combined.end(), added.begin(), added.end());
    const std::string domain = params.GetString("domain", previous_grid.domain.empty() ? "general" : previous_grid.domain);
    const std::string grid_id = params.GetString("grid_id", previous_grid.grid_id.empty() ? "" : previous_grid.grid_id);
    const SemanticGrid updated_grid = BuildGridFromFragments(combined, domain, grid_id);
    const std::string grid_json = SerializeGridJson(updated_grid);
    const std::string nodes_json = SerializeNodesJson(updated_grid.nodes);
    const std::string edges_json = SerializeEdgesJson(updated_grid.edges);
    const std::string layer_distribution_json = SerializeLayerDistributionJson(updated_grid.nodes);

    std::set<std::string> added_fragment_ids;
    for (const auto & fragment : added) {
        added_fragment_ids.insert(fragment.fragment_id);
    }
    const std::vector<SemanticNode> delta_nodes = NodesForFragments(updated_grid, added_fragment_ids);

    SetSuccess(&result, "semantic_grid_incremental_update", "Semantic grid incrementally updated: +" + std::to_string(added.size()) + " fragments, " + std::to_string(updated_grid.nodes.size()) + " nodes total");
    result.fields["result"] = "semantic_grid_incremental_update_success";
    result.fields["grid_id"] = updated_grid.grid_id;
    result.fields["domain"] = updated_grid.domain;
    result.fields["previous_fragment_count"] = std::to_string(previous_grid.fragments.size());
    result.fields["incoming_fragment_count"] = std::to_string(incoming.size());
    result.fields["added_fragment_count"] = std::to_string(added.size());
    result.fields["skipped_duplicate_fragment_count"] = std::to_string(skipped_duplicate_count);
    result.fields["new_fragment_count"] = std::to_string(updated_grid.fragments.size());
    result.fields["previous_node_count"] = std::to_string(previous_grid.nodes.size());
    result.fields["new_node_count"] = std::to_string(updated_grid.nodes.size());
    result.fields["new_edge_count"] = std::to_string(updated_grid.edges.size());
    result.fields["delta_node_count"] = std::to_string(delta_nodes.size());
    result.fields["dedupe_existing"] = dedupe_existing ? "true" : "false";
    result.fields["layer_distribution_json"] = layer_distribution_json;
    result.fields["delta_fragments_json"] = SerializeFragmentsJson(added);
    result.fields["delta_nodes_json"] = SerializeNodesJson(delta_nodes);
    result.fields["semantic_grid_json"] = grid_json;
    result.fields["nodes_json"] = nodes_json;
    result.fields["edges_json"] = edges_json;

    const std::string output_dir = params.GetString("output_dir");
    if (!output_dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(output_dir, ec);
        const std::filesystem::path dir(output_dir);
        if (ec ||
            !WriteTextFile(dir / "semantic_grid.json", grid_json) ||
            !WriteTextFile(dir / "nodes.json", nodes_json) ||
            !WriteTextFile(dir / "edges.json", edges_json) ||
            !WriteTextFile(dir / "delta_fragments.json", SerializeFragmentsJson(added)) ||
            !WriteTextFile(dir / "delta_nodes.json", SerializeNodesJson(delta_nodes))) {
            SetBlocked(&result, "semantic grid incremental update failed while writing output_dir", "output_dir_write_failed");
            return result;
        }
        const std::filesystem::path summary_path = dir / "summary.json";
        const std::string summary_json =
            "{\n"
            "  \"tool\":\"semantic_grid_incremental_update\",\n"
            "  \"grid_id\":" + JsonQuote(updated_grid.grid_id) + ",\n"
            "  \"artifact_semantic_grid_json_path\":" + JsonQuote((dir / "semantic_grid.json").string()) + ",\n"
            "  \"artifact_nodes_json_path\":" + JsonQuote((dir / "nodes.json").string()) + ",\n"
            "  \"artifact_edges_json_path\":" + JsonQuote((dir / "edges.json").string()) + ",\n"
            "  \"artifact_delta_fragments_json_path\":" + JsonQuote((dir / "delta_fragments.json").string()) + ",\n"
            "  \"artifact_delta_nodes_json_path\":" + JsonQuote((dir / "delta_nodes.json").string()) + ",\n"
            "  \"artifact_summary_json_path\":" + JsonQuote(summary_path.string()) + ",\n"
            "  \"layer_distribution\":" + layer_distribution_json + ",\n"
            "  \"previous_fragment_count\":" + std::to_string(previous_grid.fragments.size()) + ",\n"
            "  \"incoming_fragment_count\":" + std::to_string(incoming.size()) + ",\n"
            "  \"added_fragment_count\":" + std::to_string(added.size()) + ",\n"
            "  \"skipped_duplicate_fragment_count\":" + std::to_string(skipped_duplicate_count) + ",\n"
            "  \"new_fragment_count\":" + std::to_string(updated_grid.fragments.size()) + ",\n"
            "  \"new_node_count\":" + std::to_string(updated_grid.nodes.size()) + ",\n"
            "  \"new_edge_count\":" + std::to_string(updated_grid.edges.size()) + "\n"
            "}";
        WriteTextFile(summary_path, summary_json);
        result.fields["artifact_bundle_written"] = "true";
        result.fields["artifact_semantic_grid_json_path"] = (dir / "semantic_grid.json").string();
        result.fields["artifact_delta_fragments_json_path"] = (dir / "delta_fragments.json").string();
        result.fields["artifact_delta_nodes_json_path"] = (dir / "delta_nodes.json").string();
        result.fields["artifact_summary_json_path"] = summary_path.string();
    }
    return result;
}

}  // namespace codex_lan_agent
