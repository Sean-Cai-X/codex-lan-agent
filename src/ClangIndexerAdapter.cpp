#include "ClangIndexerAdapter.h"
#include "ClangAstVisitor.h"
#include "ClangAstParser.h"
#include "CfGBuilder.h"
#include "GraphSerialization.h"
#include "comm.h"
#include "StructuredJsonOperations.h"
#include "JsonRequestView.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace codex_lan_agent
{
namespace
{
std::string QuoteArg(const std::string & value)
{
    if (value.empty()) {
        return "\"\"";
    }

    bool needs_quotes = false;
    for (char ch : value) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
            ch == '"' || ch == '\0') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) {
        return value;
    }

    std::string result = "\"";
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            result += '\\';
        }
        result += ch;
    }
    result += '"';
    return result;
}

bool FileExists(const std::string & path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::string ReadFilePrefix(const std::string & path, std::size_t max_bytes)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }

    std::string content;
    content.resize(max_bytes);
    input.read(&content[0], static_cast<std::streamsize>(content.size()));
    content.resize(static_cast<std::size_t>(input.gcount()));
    return content;
}

bool IsLikelyComplexCppTranslationUnit(
    const ClangIndexerOptions & options,
    std::string * reason_code,
    std::string * detail)
{
    if (!options.compile_db_dir.empty() ||
        !options.compilation_database_path.empty() ||
        !options.extra_include_dirs.empty()) {
        return false;
    }

    std::error_code ec;
    const std::uintmax_t file_size = std::filesystem::file_size(options.source_file, ec);
    const std::string prefix = ReadFilePrefix(options.source_file, 256 * 1024);
    const bool has_include = prefix.find("#include") != std::string::npos;

    auto block = [&](const std::string & code, const std::string & message) {
        if (reason_code != nullptr) {
            *reason_code = code;
        }
        if (detail != nullptr) {
            *detail = message;
        }
        return true;
    };

    if (prefix.find("#include \"pch.h\"") != std::string::npos ||
        prefix.find("#include <pch.h>") != std::string::npos) {
        return block(
            "compile_db_required_for_pch",
            "source includes pch.h; Clang parsing needs the project compilation database or equivalent include dirs");
    }

    if (prefix.find("<opencv2/") != std::string::npos ||
        prefix.find("AIS_InteractiveContext.hxx") != std::string::npos ||
        prefix.find("#include <format>") != std::string::npos) {
        return block(
            "compile_db_required_for_external_headers",
            "source includes third-party or C++20 project headers; provide compile_commands.json or extra_include_dirs");
    }

    if (!ec && file_size > 128 * 1024 && has_include) {
        return block(
            "compile_db_required_for_large_translation_unit",
            "large C/C++ translation units with includes require compile_commands.json to avoid unstable fallback parsing");
    }

    return false;
}

std::string NormalizePathString(const std::filesystem::path & path)
{
    std::error_code ec;
    const std::filesystem::path weak = std::filesystem::weakly_canonical(path, ec);
    return ec ? path.lexically_normal().string() : weak.string();
}

std::string NormalizeJsonPathProbe(const std::filesystem::path & path)
{
    std::string normalized = NormalizePathString(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
#ifdef _WIN32
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
#endif
    return normalized;
}

std::string NormalizePathTextForCompare(std::string value)
{
    std::replace(value.begin(), value.end(), '\\', '/');
#ifdef _WIN32
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
#endif
    return value;
}

bool CompilationDatabaseMentionsSource(
    const std::filesystem::path & compile_db_path,
    const std::string & source_file)
{
    std::ifstream input(compile_db_path, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string content = buffer.str();
    std::replace(content.begin(), content.end(), '\\', '/');
#ifdef _WIN32
    std::transform(content.begin(), content.end(), content.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
#endif

    const std::string normalized_source = NormalizeJsonPathProbe(source_file);
    return !normalized_source.empty() && content.find(normalized_source) != std::string::npos;
}

std::string FindNearbyCompilationDatabase(const std::string & source_file)
{
    if (source_file.empty()) {
        return {};
    }

    std::error_code ec;
    std::filesystem::path current = std::filesystem::absolute(source_file, ec);
    if (ec) {
        current = std::filesystem::path(source_file);
    }
    if (current.has_filename()) {
        current = current.parent_path();
    }

    std::set<std::string> visited;
    while (!current.empty()) {
        const std::vector<std::filesystem::path> candidates = {
            current / "compile_commands.json",
            current / "build" / "compile_commands.json",
            current / "AIbuild" / "compile_commands.json",
            current / "build_new" / "compile_commands.json",
            current / "build01" / "compile_commands.json"
        };

        for (const auto & candidate : candidates) {
            const std::string key = NormalizePathString(candidate);
            if (visited.find(key) != visited.end()) {
                continue;
            }
            visited.insert(key);
            if (std::filesystem::exists(candidate, ec) && !ec &&
                CompilationDatabaseMentionsSource(candidate, source_file)) {
                return NormalizePathString(candidate);
            }
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current) {
            break;
        }
        current = parent;
    }

    return {};
}

void AppendStringListFromJsonish(
    const std::string & jsonish,
    std::vector<std::string> * output)
{
    if (output == nullptr || jsonish.empty()) {
        return;
    }

    std::string cleaned = jsonish;
    if (!cleaned.empty() && cleaned.front() == '[') {
        cleaned = cleaned.substr(1);
    }
    if (!cleaned.empty() && cleaned.back() == ']') {
        cleaned.pop_back();
    }

    std::istringstream iss(cleaned);
    std::string item;
    while (std::getline(iss, item, ',')) {
        while (!item.empty() && (item.front() == '"' || item.front() == ' ')) {
            item.erase(item.begin());
        }
        while (!item.empty() && (item.back() == '"' || item.back() == ' ')) {
            item.pop_back();
        }
        if (!item.empty()) {
            output->push_back(item);
        }
    }
}

std::string EscapeJsonValue(const std::string & value)
{
    std::ostringstream escaped;
    for (unsigned char ch : value) {
        switch (ch) {
        case '\\':
            escaped << "\\\\";
            break;
        case '"':
            escaped << "\\\"";
            break;
        case '\b':
            escaped << "\\b";
            break;
        case '\f':
            escaped << "\\f";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            if (ch < 0x20) {
                escaped << "\\u";
                const char * digits = "0123456789abcdef";
                escaped << '0' << '0' << digits[(ch >> 4) & 0x0F] << digits[ch & 0x0F];
            } else {
                escaped << static_cast<char>(ch);
            }
            break;
        }
    }
    return escaped.str();
}

std::string ReadWholeFileText(const std::string & path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::vector<std::string> SplitSourceLines(const std::string & text)
{
    std::vector<std::string> output;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        output.push_back(line);
    }
    return output;
}

std::string StripLineComment(const std::string & line)
{
    const std::size_t pos = line.find("//");
    return pos == std::string::npos ? line : line.substr(0, pos);
}

bool IsDfgKeyword(const std::string & token)
{
    static const std::set<std::string> keywords = {
        "alignas", "alignof", "and", "auto", "bool", "break", "case", "catch",
        "char", "class", "const", "constexpr", "continue", "decltype", "default",
        "delete", "do", "double", "else", "enum", "explicit", "extern", "false",
        "float", "for", "if", "inline", "int", "long", "namespace", "new",
        "nullptr", "operator", "private", "protected", "public", "return", "short",
        "signed", "sizeof", "static", "struct", "switch", "template", "this", "throw",
        "true", "try", "typedef", "typename", "using", "virtual", "void", "volatile",
        "while", "std", "string", "vector", "map", "set", "size_t", "uint32_t",
        "uint64_t", "int32_t", "int64_t"
    };
    return keywords.find(token) != keywords.end();
}

std::vector<std::string> ExtractDfgIdentifiers(const std::string & text)
{
    std::vector<std::string> identifiers;
    static const std::regex ident_re(R"(\b[A-Za-z_][A-Za-z0-9_]*\b)");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), ident_re);
         it != std::sregex_iterator();
         ++it) {
        const std::string token = it->str();
        if (!IsDfgKeyword(token)) {
            identifiers.push_back(token);
        }
    }
    return identifiers;
}

std::string LastDfgIdentifier(const std::string & text)
{
    const std::vector<std::string> identifiers = ExtractDfgIdentifiers(text);
    return identifiers.empty() ? std::string() : identifiers.back();
}

std::size_t FindAssignmentOperator(const std::string & line)
{
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '=') {
            continue;
        }
        const char prev = i == 0 ? '\0' : line[i - 1];
        const char next = i + 1 >= line.size() ? '\0' : line[i + 1];
        if (prev == '=' || prev == '!' || prev == '<' || prev == '>' || next == '=') {
            continue;
        }
        return i;
    }
    return std::string::npos;
}

void AddDfgEdge(
    const std::string & source,
    const std::string & target,
    const std::string & kind,
    int line,
    std::map<std::string, DfgEdgeInfo> * edges)
{
    if (source.empty() || target.empty() || edges == nullptr) {
        return;
    }

    const std::string key = source + "\n" + target + "\n" + kind;
    auto & edge = (*edges)[key];
    if (edge.count == 0) {
        edge.source = source;
        edge.target = target;
        edge.kind = kind;
        edge.first_source_line = line;
    }
    ++edge.count;
}

std::vector<ClangMethodInfo> FlattenAstFunctions(const ClangAstParseResult & ast_result)
{
    std::vector<ClangMethodInfo> functions = ast_result.schema.free_functions;
    for (const auto & cls : ast_result.schema.classes) {
        functions.insert(functions.end(), cls.methods.begin(), cls.methods.end());
    }
    std::sort(functions.begin(), functions.end(), [](const ClangMethodInfo & lhs, const ClangMethodInfo & rhs) {
        const std::string lhs_file = NormalizeJsonPathProbe(lhs.source_file);
        const std::string rhs_file = NormalizeJsonPathProbe(rhs.source_file);
        if (lhs_file != rhs_file) {
            return lhs_file < rhs_file;
        }
        if (lhs.source_line != rhs.source_line) {
            return lhs.source_line < rhs.source_line;
        }
        return lhs.qualified_name < rhs.qualified_name;
    });
    return functions;
}

std::string FindAstFunctionForLine(
    const std::vector<ClangMethodInfo> & functions,
    const std::string & source_file,
    int line_number)
{
    const std::string normalized_source = NormalizeJsonPathProbe(source_file);
    std::string best_name;
    int best_line = -1;
    for (const auto & function : functions) {
        if (function.source_line <= 0 ||
            function.source_line > line_number ||
            NormalizeJsonPathProbe(function.source_file) != normalized_source) {
            continue;
        }
        if (function.source_line >= best_line) {
            best_line = function.source_line;
            best_name = function.qualified_name.empty() ? function.name : function.qualified_name;
        }
    }
    return best_name;
}

std::set<std::string> CollectDfgEdgeAstFunctions(
    const std::vector<DfgEdgeInfo> & edges,
    const std::vector<ClangMethodInfo> & functions,
    const std::string & source_file)
{
    std::vector<std::pair<int, std::string>> source_functions;
    const std::string normalized_source = NormalizePathTextForCompare(source_file);
    for (const auto & function : functions) {
        if (function.source_line <= 0 ||
            NormalizePathTextForCompare(function.source_file) != normalized_source) {
            continue;
        }
        source_functions.push_back({
            function.source_line,
            function.qualified_name.empty() ? function.name : function.qualified_name
        });
    }
    std::sort(source_functions.begin(), source_functions.end());

    std::set<std::string> names;
    for (const auto & edge : edges) {
        if (edge.first_source_line <= 0 || source_functions.empty()) {
            continue;
        }
        const auto it = std::upper_bound(
            source_functions.begin(),
            source_functions.end(),
            std::make_pair(edge.first_source_line, std::string(1, char(127))));
        if (it != source_functions.begin()) {
            names.insert(std::prev(it)->second);
        }
    }
    return names;
}

int CountSourceScopedCallRefs(
    const ClangAstParseResult & ast_result,
    const std::string & source_file,
    const std::set<int> * source_lines)
{
    const std::string normalized_source = NormalizePathTextForCompare(source_file);
    std::map<std::string, std::string> normalized_path_cache;
    int count = 0;
    for (const auto & ref : ast_result.call_refs) {
        auto cache_it = normalized_path_cache.find(ref.source_file);
        if (cache_it == normalized_path_cache.end()) {
            cache_it = normalized_path_cache.insert({
                ref.source_file,
                NormalizePathTextForCompare(ref.source_file)
            }).first;
        }
        if (cache_it->second != normalized_source) {
            continue;
        }
        if (source_lines != nullptr &&
            source_lines->find(ref.source_line) == source_lines->end()) {
            continue;
        }
        ++count;
    }
    return count;
}

bool BuildAstStatementDfg(
    const ClangAstParseResult & ast_result,
    const std::string & source_file,
    std::set<std::string> * nodes,
    std::map<std::string, DfgEdgeInfo> * edge_map,
    int * definition_count,
    int * use_count)
{
    if (nodes == nullptr ||
        edge_map == nullptr ||
        definition_count == nullptr ||
        use_count == nullptr) {
        return false;
    }

    nodes->clear();
    edge_map->clear();
    *definition_count = 0;
    *use_count = 0;

    const std::string normalized_source = NormalizePathTextForCompare(source_file);
    std::map<int, std::vector<std::string>> defs_by_line;
    std::map<int, std::vector<std::string>> uses_by_line;

    for (const auto & ref : ast_result.data_flow_refs) {
        if (ref.symbol.empty() ||
            ref.source_line <= 0 ||
            NormalizePathTextForCompare(ref.source_file) != normalized_source) {
            continue;
        }

        nodes->insert(ref.symbol);
        if (ref.access_kind == "def") {
            defs_by_line[ref.source_line].push_back(ref.symbol);
            ++(*definition_count);
        } else if (ref.access_kind == "use") {
            uses_by_line[ref.source_line].push_back(ref.symbol);
            ++(*use_count);
        }
    }

    for (const auto & item : defs_by_line) {
        const int line = item.first;
        const auto uses_it = uses_by_line.find(line);
        if (uses_it == uses_by_line.end()) {
            continue;
        }
        for (const auto & def : item.second) {
            for (const auto & use : uses_it->second) {
                if (use == def) {
                    continue;
                }
                AddDfgEdge(use, def, "ast_stmt_def_use", line, edge_map);
            }
        }
    }

    return *definition_count > 0;
}

struct CfgPathSensitivityInfo
{
    int function_count = 0;
    int branch_count = 0;
    int cyclic_function_count = 0;
};

CfgPathSensitivityInfo ComputeCfgPathSensitivityInfo(
    const CfgBuildResult & cfg_result,
    const std::set<std::string> & function_names)
{
    CfgPathSensitivityInfo info;
    for (const auto & function : cfg_result.functions) {
        const std::string name = function.qualified_name.empty()
            ? function.function_name
            : function.qualified_name;
        if (!function_names.empty() &&
            function_names.find(name) == function_names.end() &&
            function_names.find(function.function_name) == function_names.end()) {
            continue;
        }
        ++info.function_count;
        info.branch_count += function.branch_count;
        if (function.has_cycle) {
            ++info.cyclic_function_count;
        }
    }
    return info;
}

bool WriteTextArtifactFile(
    const std::filesystem::path & path,
    const std::string & content)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output << content;
    output.close();
    return output.good();
}

bool WriteArtifactBundle(
    const std::string & output_dir,
    const std::vector<std::pair<std::string, std::string>> & artifacts,
    const std::string & create_failure_summary,
    const std::string & write_failure_summary,
    CommandResult * result,
    std::filesystem::path * bundle_dir)
{
    if (output_dir.empty()) {
        return true;
    }
    if (result == nullptr) {
        return false;
    }

    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::path(output_dir);
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        result->ok = false;
        result->exit_code = 500;
        result->fields["error"] = "failed to create output_dir";
        result->fields["status"] = "failed";
        result->fields["preflight_reason_code"] = "output_dir_create_failed";
        result->fields["summary"] = create_failure_summary;
        return false;
    }

    for (const auto & artifact : artifacts) {
        if (!WriteTextArtifactFile(dir / artifact.first, artifact.second)) {
            result->ok = false;
            result->exit_code = 500;
            result->fields["error"] = "failed to write one or more output_dir artifact files";
            result->fields["status"] = "failed";
            result->fields["preflight_reason_code"] = "output_dir_write_failed";
            result->fields["summary"] = write_failure_summary;
            return false;
        }
    }

    if (bundle_dir != nullptr) {
        *bundle_dir = dir;
    }
    return true;
}

bool WriteArtifactBundleAndRecordPaths(
    const std::string & output_dir,
    const std::vector<std::pair<std::string, std::string>> & artifacts,
    const std::vector<std::pair<std::string, std::string>> & artifact_path_fields,
    const std::string & create_failure_summary,
    const std::string & write_failure_summary,
    CommandResult * result)
{
    std::filesystem::path bundle_dir;
    if (!WriteArtifactBundle(
            output_dir,
            artifacts,
            create_failure_summary,
            write_failure_summary,
            result,
            &bundle_dir)) {
        return false;
    }
    if (output_dir.empty() || result == nullptr) {
        return true;
    }
    result->fields["output_dir"] = output_dir;
    result->fields["artifact_bundle_written"] = "true";
    for (const auto & field : artifact_path_fields) {
        result->fields[field.first] = (bundle_dir / field.second).string();
    }
    return true;
}

struct PageWindowInfo
{
    bool has_more = false;
    int next_offset = 0;
};

PageWindowInfo ComputePageWindowInfo(
    int matched_count,
    int offset,
    int max_items)
{
    if (matched_count < 0) matched_count = 0;
    if (offset < 0) offset = 0;
    if (max_items < 0) max_items = 0;
    PageWindowInfo info;
    info.has_more = max_items > 0 && offset + max_items < matched_count;
    info.next_offset = info.has_more ? offset + max_items : matched_count;
    return info;
}

void SetArtifactResolutionFields(
    CommandResult * result,
    const std::string & artifact_json_path,
    const std::string & artifact_summary_path,
    const std::string & artifact_json_path_source,
    const std::string & artifact_json_path_resolution_detail)
{
    if (result == nullptr) {
        return;
    }
    result->fields["artifact_json_path"] = artifact_json_path;
    result->fields["artifact_summary_path"] = artifact_summary_path;
    result->fields["artifact_json_path_resolved_from"] = artifact_json_path_source;
    result->fields["artifact_json_path_resolution_detail"] = artifact_json_path_resolution_detail;
}

void SetCfgPaginationFields(
    CommandResult * result,
    bool cfg_truncated,
    int offset_functions,
    int max_functions,
    const PageWindowInfo & page)
{
    if (result == nullptr) {
        return;
    }
    result->fields["cfg_truncated"] = cfg_truncated ? "true" : "false";
    result->fields["offset_functions"] = std::to_string(offset_functions);
    result->fields["max_functions"] = std::to_string(max_functions);
    result->fields["cfg_has_more"] = page.has_more ? "true" : "false";
    result->fields["next_offset_functions"] = std::to_string(page.next_offset);
}

void SetGraphPaginationFields(
    CommandResult * result,
    bool graph_truncated,
    int max_nodes,
    int offset_edges,
    int max_edges,
    const PageWindowInfo & page)
{
    if (result == nullptr) {
        return;
    }
    result->fields["graph_truncated"] = graph_truncated ? "true" : "false";
    result->fields["max_nodes"] = std::to_string(max_nodes);
    result->fields["offset_edges"] = std::to_string(offset_edges);
    result->fields["max_edges"] = std::to_string(max_edges);
    result->fields["graph_has_more"] = page.has_more ? "true" : "false";
    result->fields["next_offset_edges"] = std::to_string(page.next_offset);
}

std::string UnescapeJsonValue(const std::string & value)
{
    std::string output;
    output.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 >= value.size()) {
            output.push_back(value[i]);
            continue;
        }
        const char escaped = value[++i];
        switch (escaped) {
        case '"': output.push_back('"'); break;
        case '\\': output.push_back('\\'); break;
        case '/': output.push_back('/'); break;
        case 'b': output.push_back('\b'); break;
        case 'f': output.push_back('\f'); break;
        case 'n': output.push_back('\n'); break;
        case 'r': output.push_back('\r'); break;
        case 't': output.push_back('\t'); break;
        default: output.push_back(escaped); break;
        }
    }
    return output;
}

bool ExtractJsonStringField(
    const std::string & object_text,
    const std::string & key,
    std::string * value)
{
    if (value == nullptr) {
        return false;
    }
    const std::string extracted = ExtractJsonString(object_text, key);
    if (extracted.empty() && ExtractJsonRawValue(object_text, key).empty()) {
        return false;
    }
    *value = extracted;
    return true;
}

bool ExtractJsonIntField(
    const std::string & object_text,
    const std::string & key,
    int * value)
{
    if (value == nullptr) {
        return false;
    }
    const std::string raw = ExtractJsonRawValue(object_text, key);
    if (raw.empty()) {
        return false;
    }
    try {
        *value = std::stoi(raw);
    } catch (...) {
        return false;
    }
    return true;
}

void RecomputeCfgTotals(CfgBuildResult * result);

bool ExtractJsonDoubleField(
    const std::string & object_text,
    const std::string & key,
    double * value)
{
    if (value == nullptr) {
        return false;
    }
    const std::string raw = ExtractJsonRawValue(object_text, key);
    if (raw.empty()) {
        return false;
    }
    try {
        *value = std::stod(raw);
    } catch (...) {
        return false;
    }
    return true;
}

bool ExtractJsonBoolField(
    const std::string & object_text,
    const std::string & key,
    bool * value)
{
    if (value == nullptr) {
        return false;
    }
    const std::string raw = ExtractJsonRawValue(object_text, key);
    if (raw != "true" && raw != "false") {
        return false;
    }
    *value = raw == "true";
    return true;
}

bool ExtractJsonArrayRaw(
    const std::string & object_text,
    const std::string & key,
    std::string * array_text)
{
    if (array_text == nullptr) {
        return false;
    }
    const std::string raw = ExtractJsonObjectRaw(object_text, key);
    if (raw.size() < 2 || raw.front() != '[' || raw.back() != ']') {
        return false;
    }
    *array_text = raw.substr(1, raw.size() - 2);
    return true;
}

std::vector<std::string> SplitTopLevelJsonObjects(const std::string & array_text)
{
    std::vector<std::string> objects;
    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    std::size_t object_start = std::string::npos;
    for (std::size_t i = 0; i < array_text.size(); ++i) {
        const char ch = array_text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '{') {
            if (depth == 0) {
                object_start = i;
            }
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0 && object_start != std::string::npos) {
                objects.push_back(array_text.substr(object_start, i - object_start + 1));
                object_start = std::string::npos;
            }
        }
    }
    return objects;
}

std::vector<int> ParseJsonIntArray(const std::string & array_text)
{
    std::vector<int> values;
    static const std::regex int_re(R"(-?[0-9]+)");
    for (auto it = std::sregex_iterator(array_text.begin(), array_text.end(), int_re);
         it != std::sregex_iterator();
         ++it) {
        try {
            values.push_back(std::stoi(it->str()));
        } catch (...) {
        }
    }
    return values;
}

std::vector<std::string> ParseJsonStringArray(const std::string & array_text)
{
    std::vector<std::string> values;
    static const std::regex string_re("\"((?:\\\\.|[^\"])*)\"");
    for (auto it = std::sregex_iterator(array_text.begin(), array_text.end(), string_re);
         it != std::sregex_iterator();
         ++it) {
        if (it->size() > 1) {
            values.push_back(UnescapeJsonValue((*it)[1].str()));
        }
    }
    return values;
}

bool ParseCfgArtifactJson(
    const std::string & json_text,
    CfgBuildResult * result)
{
    if (result == nullptr) {
        return false;
    }
    *result = CfgBuildResult();
    result->success = true;
    ExtractJsonStringField(json_text, "error", &result->error);
    ExtractJsonStringField(json_text, "source_file", &result->source_file);
    double build_time_ms = 0.0;
    if (ExtractJsonDoubleField(json_text, "build_time_ms", &build_time_ms)) {
        result->build_time_ms = build_time_ms;
    }

    std::string functions_array;
    if (!ExtractJsonArrayRaw(json_text, "functions", &functions_array)) {
        return false;
    }
    for (const auto & function_text : SplitTopLevelJsonObjects(functions_array)) {
        CfgFunctionInfo function;
        ExtractJsonStringField(function_text, "function_name", &function.function_name);
        ExtractJsonStringField(function_text, "qualified_name", &function.qualified_name);
        ExtractJsonStringField(function_text, "namespace_name", &function.namespace_name);
        ExtractJsonStringField(function_text, "source_file", &function.source_file);
        ExtractJsonIntField(function_text, "source_line", &function.source_line);
        ExtractJsonIntField(function_text, "entry_block_id", &function.entry_block_id);
        ExtractJsonIntField(function_text, "exit_block_id", &function.exit_block_id);
        ExtractJsonIntField(function_text, "block_count", &function.block_count);
        ExtractJsonIntField(function_text, "edge_count", &function.edge_count);
        ExtractJsonIntField(function_text, "branch_count", &function.branch_count);
        double complexity = 1.0;
        if (ExtractJsonDoubleField(function_text, "cyclomatic_complexity", &complexity)) {
            function.cyclomatic_complexity = static_cast<float>(complexity);
        }
        ExtractJsonBoolField(function_text, "has_cycle", &function.has_cycle);

        std::string blocks_array;
        if (ExtractJsonArrayRaw(function_text, "blocks", &blocks_array)) {
            for (const auto & block_text : SplitTopLevelJsonObjects(blocks_array)) {
                CfGBlockInfo block;
                ExtractJsonIntField(block_text, "block_id", &block.block_id);
                ExtractJsonStringField(block_text, "block_type", &block.block_type);
                ExtractJsonStringField(block_text, "label", &block.label);
                ExtractJsonBoolField(block_text, "is_entry", &block.is_entry);
                ExtractJsonBoolField(block_text, "is_exit", &block.is_exit);
                ExtractJsonIntField(block_text, "source_line", &block.source_line);
                ExtractJsonStringField(block_text, "source_file", &block.source_file);
                std::string statements_array;
                if (ExtractJsonArrayRaw(block_text, "statements", &statements_array)) {
                    block.statements = ParseJsonStringArray(statements_array);
                }
                std::string successors_array;
                if (ExtractJsonArrayRaw(block_text, "successor_ids", &successors_array)) {
                    block.successor_ids = ParseJsonIntArray(successors_array);
                }
                std::string predecessors_array;
                if (ExtractJsonArrayRaw(block_text, "predecessor_ids", &predecessors_array)) {
                    block.predecessor_ids = ParseJsonIntArray(predecessors_array);
                }
                function.blocks.push_back(block);
            }
        }
        if (function.block_count == 0) {
            function.block_count = static_cast<int>(function.blocks.size());
        }
        if (function.edge_count == 0) {
            for (const auto & block : function.blocks) {
                function.edge_count += static_cast<int>(block.successor_ids.size());
            }
        }
        result->functions.push_back(function);
    }
    RecomputeCfgTotals(result);
    return !result->functions.empty();
}

bool ParseCallGraphArtifactJson(
    const std::string & json_text,
    std::vector<std::string> * nodes,
    std::vector<CallGraphEdgeInfo> * edges)
{
    if (nodes == nullptr || edges == nullptr) {
        return false;
    }
    nodes->clear();
    edges->clear();

    std::set<std::string> node_set;
    std::string nodes_array;
    if (ExtractJsonArrayRaw(json_text, "nodes", &nodes_array)) {
        for (const auto & node_text : SplitTopLevelJsonObjects(nodes_array)) {
            std::string id;
            std::string name;
            if (ExtractJsonStringField(node_text, "id", &id) ||
                ExtractJsonStringField(node_text, "name", &name)) {
                node_set.insert(id.empty() ? name : id);
            }
        }
    }

    std::string edges_array;
    if (ExtractJsonArrayRaw(json_text, "edges", &edges_array)) {
        for (const auto & edge_text : SplitTopLevelJsonObjects(edges_array)) {
            CallGraphEdgeInfo edge;
            if (ExtractJsonStringField(edge_text, "caller", &edge.caller) &&
                ExtractJsonStringField(edge_text, "callee", &edge.callee)) {
                ExtractJsonIntField(edge_text, "count", &edge.count);
                ExtractJsonStringField(edge_text, "first_source_file", &edge.first_source_file);
                ExtractJsonIntField(edge_text, "first_source_line", &edge.first_source_line);
                ExtractJsonIntField(edge_text, "first_source_col", &edge.first_source_col);
                node_set.insert(edge.caller);
                node_set.insert(edge.callee);
                edges->push_back(edge);
            }
        }
    }

    if (node_set.empty() && edges->empty()) {
        std::istringstream lines(json_text);
        std::string line;
        while (std::getline(lines, line)) {
        std::string id;
        std::string name;
        if (ExtractJsonStringField(line, "id", &id) &&
            ExtractJsonStringField(line, "name", &name)) {
            node_set.insert(id.empty() ? name : id);
            continue;
        }

        CallGraphEdgeInfo edge;
        if (ExtractJsonStringField(line, "caller", &edge.caller) &&
            ExtractJsonStringField(line, "callee", &edge.callee)) {
            ExtractJsonIntField(line, "count", &edge.count);
            ExtractJsonStringField(line, "first_source_file", &edge.first_source_file);
            ExtractJsonIntField(line, "first_source_line", &edge.first_source_line);
            ExtractJsonIntField(line, "first_source_col", &edge.first_source_col);
            node_set.insert(edge.caller);
            node_set.insert(edge.callee);
            edges->push_back(edge);
        }
        }
    }

    nodes->assign(node_set.begin(), node_set.end());
    return !nodes->empty() || !edges->empty();
}

bool ParseDfgArtifactJson(
    const std::string & json_text,
    std::vector<std::string> * nodes,
    std::vector<DfgEdgeInfo> * edges)
{
    if (nodes == nullptr || edges == nullptr) {
        return false;
    }
    nodes->clear();
    edges->clear();

    std::set<std::string> node_set;
    std::string nodes_array;
    if (ExtractJsonArrayRaw(json_text, "nodes", &nodes_array)) {
        for (const auto & node_text : SplitTopLevelJsonObjects(nodes_array)) {
            std::string id;
            std::string name;
            if (ExtractJsonStringField(node_text, "id", &id) ||
                ExtractJsonStringField(node_text, "name", &name)) {
                node_set.insert(id.empty() ? name : id);
            }
        }
    }

    std::string edges_array;
    if (ExtractJsonArrayRaw(json_text, "edges", &edges_array)) {
        for (const auto & edge_text : SplitTopLevelJsonObjects(edges_array)) {
            DfgEdgeInfo edge;
            if (ExtractJsonStringField(edge_text, "source", &edge.source) &&
                ExtractJsonStringField(edge_text, "target", &edge.target)) {
                ExtractJsonStringField(edge_text, "kind", &edge.kind);
                ExtractJsonIntField(edge_text, "count", &edge.count);
                ExtractJsonIntField(edge_text, "first_source_line", &edge.first_source_line);
                node_set.insert(edge.source);
                node_set.insert(edge.target);
                edges->push_back(edge);
            }
        }
    }

    if (node_set.empty() && edges->empty()) {
        std::istringstream lines(json_text);
        std::string line;
        while (std::getline(lines, line)) {
        std::string id;
        std::string name;
        if (ExtractJsonStringField(line, "id", &id) &&
            ExtractJsonStringField(line, "name", &name)) {
            node_set.insert(id.empty() ? name : id);
            continue;
        }

        DfgEdgeInfo edge;
        if (ExtractJsonStringField(line, "source", &edge.source) &&
            ExtractJsonStringField(line, "target", &edge.target)) {
            ExtractJsonStringField(line, "kind", &edge.kind);
            ExtractJsonIntField(line, "count", &edge.count);
            ExtractJsonIntField(line, "first_source_line", &edge.first_source_line);
            node_set.insert(edge.source);
            node_set.insert(edge.target);
            edges->push_back(edge);
        }
        }
    }

    nodes->assign(node_set.begin(), node_set.end());
    return !nodes->empty() || !edges->empty();
}

std::string ResolveArtifactJsonPathFromSummary(
    const std::string & artifact_json_path,
    const std::string & artifact_summary_path,
    const std::string & json_file_name,
    const std::string & summary_json_path_field,
    std::string * resolution_source,
    std::string * resolution_detail)
{
    if (resolution_source != nullptr) {
        resolution_source->clear();
    }
    if (resolution_detail != nullptr) {
        resolution_detail->clear();
    }
    if (!artifact_json_path.empty()) {
        if (resolution_source != nullptr) {
            *resolution_source = "artifact_json_path";
        }
        if (resolution_detail != nullptr) {
            *resolution_detail = "direct";
        }
        return artifact_json_path;
    }
    if (artifact_summary_path.empty()) {
        return {};
    }

    const std::filesystem::path summary_path(artifact_summary_path);
    const std::string summary_json = ReadWholeFileText(artifact_summary_path);
    std::string summary_artifact_json_path;
    if (!summary_json.empty() &&
        !summary_json_path_field.empty() &&
        ExtractJsonStringField(
            summary_json,
            summary_json_path_field,
            &summary_artifact_json_path) &&
        !summary_artifact_json_path.empty()) {
        if (resolution_source != nullptr) {
            *resolution_source = "artifact_summary_path";
        }
        if (resolution_detail != nullptr) {
            *resolution_detail = summary_json_path_field;
        }
        std::filesystem::path resolved(summary_artifact_json_path);
        if (resolved.is_relative()) {
            resolved = summary_path.parent_path() / resolved;
        }
        return resolved.string();
    }

    if (resolution_source != nullptr) {
        *resolution_source = "artifact_summary_path";
    }
    if (resolution_detail != nullptr) {
        *resolution_detail = "sibling_filename_fallback";
    }
    return (summary_path.parent_path() / json_file_name).string();
}

void AddLimitedNode(
    const std::string & node,
    std::size_t max_nodes,
    std::set<std::string> * seen,
    std::vector<std::string> * limited)
{
    if (node.empty() || seen == nullptr || limited == nullptr) {
        return;
    }
    if (max_nodes > 0 && limited->size() >= max_nodes) {
        return;
    }
    if (seen->insert(node).second) {
        limited->push_back(node);
    }
}

bool ApplyCallGraphLimits(
    int max_nodes,
    int offset_edges,
    int max_edges,
    std::vector<std::string> * nodes,
    std::vector<CallGraphEdgeInfo> * edges)
{
    if (nodes == nullptr || edges == nullptr) {
        return false;
    }

    if (offset_edges < 0) {
        offset_edges = 0;
    }
    bool truncated = false;
    const bool edges_offset =
        offset_edges > 0 && !edges->empty();
    if (edges_offset) {
        const std::size_t offset = static_cast<std::size_t>(offset_edges);
        if (offset >= edges->size()) {
            edges->clear();
        } else {
            edges->erase(edges->begin(), edges->begin() + offset);
        }
        truncated = true;
    }

    const bool edges_limited =
        max_edges > 0 && edges->size() > static_cast<std::size_t>(max_edges);
    if (edges_limited) {
        edges->resize(static_cast<std::size_t>(max_edges));
        truncated = true;
    }

    if (max_nodes > 0 ||
        edges_offset ||
        edges_limited) {
        const std::size_t node_limit =
            max_nodes > 0 ? static_cast<std::size_t>(max_nodes) : 0;
        std::set<std::string> seen;
        std::vector<std::string> limited_nodes;
        for (const auto & edge : *edges) {
            AddLimitedNode(edge.caller, node_limit, &seen, &limited_nodes);
            AddLimitedNode(edge.callee, node_limit, &seen, &limited_nodes);
        }
        if (max_nodes > 0) {
            for (const auto & node : *nodes) {
                AddLimitedNode(node, node_limit, &seen, &limited_nodes);
            }
        }

        edges->erase(
            std::remove_if(
                edges->begin(),
                edges->end(),
                [&](const CallGraphEdgeInfo & edge) {
                    return seen.find(edge.caller) == seen.end() ||
                        seen.find(edge.callee) == seen.end();
                }),
            edges->end());

        if (limited_nodes.size() != nodes->size()) {
            truncated = true;
        }
        *nodes = limited_nodes;
    }

    return truncated;
}

bool GraphNodeMatchesFocus(
    const std::string & node,
    const std::string & focus_symbol)
{
    return !focus_symbol.empty() &&
        (node == focus_symbol || node.find(focus_symbol) != std::string::npos);
}

bool ApplyCallGraphNeighborhood(
    const std::string & focus_symbol,
    int neighborhood_depth,
    const std::string & neighborhood_direction,
    std::vector<std::string> * nodes,
    std::vector<CallGraphEdgeInfo> * edges)
{
    if (focus_symbol.empty() || nodes == nullptr || edges == nullptr) {
        return false;
    }
    if (neighborhood_depth < 0) {
        neighborhood_depth = 0;
    }
    if (neighborhood_depth > 16) {
        neighborhood_depth = 16;
    }
    const bool use_incoming = neighborhood_direction != "outgoing";
    const bool use_outgoing = neighborhood_direction != "incoming";

    std::set<std::string> kept;
    std::vector<std::pair<std::string, int>> worklist;
    for (const auto & node : *nodes) {
        if (GraphNodeMatchesFocus(node, focus_symbol) &&
            kept.insert(node).second) {
            worklist.push_back({node, 0});
        }
    }

    for (std::size_t cursor = 0; cursor < worklist.size(); ++cursor) {
        const std::string current = worklist[cursor].first;
        const int depth = worklist[cursor].second;
        if (depth >= neighborhood_depth) {
            continue;
        }
        for (const auto & edge : *edges) {
            if (use_outgoing && edge.caller == current &&
                kept.insert(edge.callee).second) {
                worklist.push_back({edge.callee, depth + 1});
            }
            if (use_incoming && edge.callee == current &&
                kept.insert(edge.caller).second) {
                worklist.push_back({edge.caller, depth + 1});
            }
        }
    }

    std::vector<std::string> filtered_nodes;
    for (const auto & node : *nodes) {
        if (kept.find(node) != kept.end()) {
            filtered_nodes.push_back(node);
        }
    }
    std::vector<CallGraphEdgeInfo> filtered_edges;
    for (const auto & edge : *edges) {
        if (kept.find(edge.caller) != kept.end() &&
            kept.find(edge.callee) != kept.end()) {
            filtered_edges.push_back(edge);
        }
    }
    *nodes = filtered_nodes;
    *edges = filtered_edges;
    return true;
}

bool ApplyDfgLimits(
    int max_nodes,
    int offset_edges,
    int max_edges,
    std::vector<std::string> * nodes,
    std::vector<DfgEdgeInfo> * edges)
{
    if (nodes == nullptr || edges == nullptr) {
        return false;
    }

    if (offset_edges < 0) {
        offset_edges = 0;
    }
    bool truncated = false;
    const bool edges_offset =
        offset_edges > 0 && !edges->empty();
    if (edges_offset) {
        const std::size_t offset = static_cast<std::size_t>(offset_edges);
        if (offset >= edges->size()) {
            edges->clear();
        } else {
            edges->erase(edges->begin(), edges->begin() + offset);
        }
        truncated = true;
    }

    const bool edges_limited =
        max_edges > 0 && edges->size() > static_cast<std::size_t>(max_edges);
    if (edges_limited) {
        edges->resize(static_cast<std::size_t>(max_edges));
        truncated = true;
    }

    if (max_nodes > 0 ||
        edges_offset ||
        edges_limited) {
        const std::size_t node_limit =
            max_nodes > 0 ? static_cast<std::size_t>(max_nodes) : 0;
        std::set<std::string> seen;
        std::vector<std::string> limited_nodes;
        for (const auto & edge : *edges) {
            AddLimitedNode(edge.source, node_limit, &seen, &limited_nodes);
            AddLimitedNode(edge.target, node_limit, &seen, &limited_nodes);
        }
        if (max_nodes > 0) {
            for (const auto & node : *nodes) {
                AddLimitedNode(node, node_limit, &seen, &limited_nodes);
            }
        }

        edges->erase(
            std::remove_if(
                edges->begin(),
                edges->end(),
                [&](const DfgEdgeInfo & edge) {
                    return seen.find(edge.source) == seen.end() ||
                        seen.find(edge.target) == seen.end();
                }),
            edges->end());

        if (limited_nodes.size() != nodes->size()) {
            truncated = true;
        }
        *nodes = limited_nodes;
    }

    return truncated;
}

bool ApplyDfgNeighborhood(
    const std::string & focus_symbol,
    int neighborhood_depth,
    const std::string & neighborhood_direction,
    std::vector<std::string> * nodes,
    std::vector<DfgEdgeInfo> * edges)
{
    if (focus_symbol.empty() || nodes == nullptr || edges == nullptr) {
        return false;
    }
    if (neighborhood_depth < 0) {
        neighborhood_depth = 0;
    }
    if (neighborhood_depth > 16) {
        neighborhood_depth = 16;
    }
    const bool use_incoming = neighborhood_direction != "outgoing";
    const bool use_outgoing = neighborhood_direction != "incoming";

    std::set<std::string> kept;
    std::vector<std::pair<std::string, int>> worklist;
    for (const auto & node : *nodes) {
        if (GraphNodeMatchesFocus(node, focus_symbol) &&
            kept.insert(node).second) {
            worklist.push_back({node, 0});
        }
    }

    for (std::size_t cursor = 0; cursor < worklist.size(); ++cursor) {
        const std::string current = worklist[cursor].first;
        const int depth = worklist[cursor].second;
        if (depth >= neighborhood_depth) {
            continue;
        }
        for (const auto & edge : *edges) {
            if (use_outgoing && edge.source == current &&
                kept.insert(edge.target).second) {
                worklist.push_back({edge.target, depth + 1});
            }
            if (use_incoming && edge.target == current &&
                kept.insert(edge.source).second) {
                worklist.push_back({edge.source, depth + 1});
            }
        }
    }

    std::vector<std::string> filtered_nodes;
    for (const auto & node : *nodes) {
        if (kept.find(node) != kept.end()) {
            filtered_nodes.push_back(node);
        }
    }
    std::vector<DfgEdgeInfo> filtered_edges;
    for (const auto & edge : *edges) {
        if (kept.find(edge.source) != kept.end() &&
            kept.find(edge.target) != kept.end()) {
            filtered_edges.push_back(edge);
        }
    }
    *nodes = filtered_nodes;
    *edges = filtered_edges;
    return true;
}

bool CfgFunctionMatchesFilter(
    const CfgFunctionInfo & function,
    const std::string & function_name,
    const std::vector<std::string> & function_names)
{
    const bool name_match = function_name.empty() ||
        function.function_name == function_name ||
        function.qualified_name.find(function_name) != std::string::npos;
    if (!name_match) {
        return false;
    }
    if (function_names.empty()) {
        return true;
    }
    for (const auto & name : function_names) {
        if (name.empty()) {
            continue;
        }
        if (function.function_name == name ||
            function.qualified_name.find(name) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void RecomputeCfgTotals(CfgBuildResult * result)
{
    if (result == nullptr) {
        return;
    }

    result->total_functions = static_cast<int>(result->functions.size());
    result->total_blocks = 0;
    result->total_edges = 0;
    for (const auto & function : result->functions) {
        result->total_blocks += function.block_count;
        result->total_edges += function.edge_count;
    }
}

bool ApplyCfgFunctionSelection(
    const std::string & function_name,
    const std::vector<std::string> & function_names,
    int offset_functions,
    int max_functions,
    CfgBuildResult * result,
    int * matched_total_functions,
    int * matched_total_blocks,
    int * matched_total_edges)
{
    if (result == nullptr) {
        return false;
    }
    if (offset_functions < 0) {
        offset_functions = 0;
    }
    if (max_functions < 0) {
        max_functions = 0;
    }

    const bool has_filter = !function_name.empty() || !function_names.empty();
    const std::size_t offset = static_cast<std::size_t>(offset_functions);
    const std::size_t limit =
        max_functions > 0 ? static_cast<std::size_t>(max_functions) : 0;

    std::vector<CfgFunctionInfo> matched;
    matched.reserve(result->functions.size());
    int matched_blocks = 0;
    int matched_edges = 0;
    for (const auto & function : result->functions) {
        if (!CfgFunctionMatchesFilter(function, function_name, function_names)) {
            continue;
        }
        matched.push_back(function);
        matched_blocks += function.block_count;
        matched_edges += function.edge_count;
    }

    if (matched_total_functions != nullptr) {
        *matched_total_functions = static_cast<int>(matched.size());
    }
    if (matched_total_blocks != nullptr) {
        *matched_total_blocks = matched_blocks;
    }
    if (matched_total_edges != nullptr) {
        *matched_total_edges = matched_edges;
    }

    std::vector<CfgFunctionInfo> selected;
    if (offset < matched.size()) {
        const std::size_t end = limit == 0
            ? matched.size()
            : std::min(matched.size(), offset + limit);
        selected.insert(selected.end(), matched.begin() + offset, matched.begin() + end);
    }

    const bool truncated = offset > 0 || selected.size() < matched.size();
    if (has_filter || offset > 0 || limit > 0) {
        result->functions = selected;
        RecomputeCfgTotals(result);
    }

    return truncated;
}
}

bool ResolveCompilationDatabaseLocation(
    const ClangIndexerOptions & options,
    std::string * resolved_directory,
    std::string * resolved_file_path,
    std::string * error_message)
{
    if (resolved_directory != nullptr) {
        resolved_directory->clear();
    }
    if (resolved_file_path != nullptr) {
        resolved_file_path->clear();
    }
    if (error_message != nullptr) {
        error_message->clear();
    }

    std::string candidate = options.compilation_database_path;
    if (candidate.empty()) {
        candidate = options.compile_db_dir;
    }
    if (candidate.empty()) {
        candidate = FindNearbyCompilationDatabase(options.source_file);
    }
    if (candidate.empty()) {
        return true;
    }

    std::error_code ec;
    const std::filesystem::path raw_path(candidate);
    if (!std::filesystem::exists(raw_path, ec) || ec) {
        if (error_message != nullptr) {
            *error_message = "compilation database path does not exist: " + candidate;
        }
        return false;
    }

    std::filesystem::path directory_path;
    std::filesystem::path file_path;
    if (std::filesystem::is_regular_file(raw_path, ec) && !ec) {
        if (raw_path.filename() != "compile_commands.json") {
            if (error_message != nullptr) {
                *error_message =
                    "compilation_database_path must point to compile_commands.json: " + candidate;
            }
            return false;
        }
        file_path = raw_path;
        directory_path = raw_path.parent_path();
    } else if (std::filesystem::is_directory(raw_path, ec) && !ec) {
        directory_path = raw_path;
        file_path = raw_path / "compile_commands.json";
        if (!std::filesystem::exists(file_path, ec) || ec) {
            if (error_message != nullptr) {
                *error_message =
                    "compile_commands.json was not found in directory: " + candidate;
            }
            return false;
        }
    } else {
        if (error_message != nullptr) {
            *error_message = "invalid compilation database path: " + candidate;
        }
        return false;
    }

    if (resolved_directory != nullptr) {
        *resolved_directory = NormalizePathString(directory_path);
    }
    if (resolved_file_path != nullptr) {
        *resolved_file_path = NormalizePathString(file_path);
    }
    return true;
}

bool BuildClangIndexerCommand(
    const std::string & indexer_path,
    const ClangIndexerOptions & options,
    std::string * command_line,
    std::vector<std::string> * args)
{
    if (command_line == nullptr || args == nullptr)
        return false;

    if (indexer_path.empty() || options.source_file.empty())
        return false;

    args->clear();
    args->push_back("--source");
    args->push_back(options.source_file);

    std::string resolved_compile_db_dir;
    std::string resolved_compile_db_file;
    if (!ResolveCompilationDatabaseLocation(
            options,
            &resolved_compile_db_dir,
            &resolved_compile_db_file,
            nullptr)) {
        return false;
    }

    if (!resolved_compile_db_dir.empty()) {
        args->push_back("--compile-db");
        args->push_back(resolved_compile_db_dir);
    }

    if (!options.output_json_path.empty()) {
        args->push_back("--output");
        args->push_back(options.output_json_path);
    }

    if (!options.project_root.empty()) {
        args->push_back("--project-root");
        args->push_back(options.project_root);
    }

    for (const auto & inc : options.extra_include_dirs) {
        args->push_back("--include-dir");
        args->push_back(inc);
    }

    for (const auto & def : options.extra_defines) {
        args->push_back("--define");
        args->push_back(def);
    }

    if (options.verbose)
        args->push_back("--verbose");

    std::ostringstream cmd;
    cmd << QuoteArg(indexer_path);
    for (const auto & arg : *args) {
        cmd << " " << QuoteArg(arg);
    }
    *command_line = cmd.str();
    return true;
}

ClangIndexerResult ParseClangIndexerOutput(
    const std::string & output_json,
    const std::string & error_message)
{
    ClangIndexerResult result;

    if (!error_message.empty()) {
        result.success = false;
        result.error = error_message;
        return result;
    }

    if (output_json.empty()) {
        result.success = false;
        result.error = "empty output from clang indexer";
        return result;
    }

    result.success = true;

    result.schema.module_name = ExtractJsonString(output_json, "module_name");

    std::string classes_json = ExtractJsonString(output_json, "classes");
    if (!classes_json.empty()) {
        result.schema.classes.push_back({});
        result.schema.classes.back().name = ExtractJsonString(classes_json, "name");
        result.schema.classes.back().qualified_name = ExtractJsonString(classes_json, "qualified_name");
        result.schema.classes.back().namespace_name = ExtractJsonString(classes_json, "namespace_name");
    }

    result.symbol_count = 0;
    result.ref_count = 0;
    std::string elapsed = ExtractJsonString(output_json, "elapsed_ms");
    if (!elapsed.empty()) {
        try {
            result.elapsed_ms = std::stoi(elapsed);
        } catch (...) {
            result.elapsed_ms = 0;
        }
    }

    return result;
}

std::string FindClangIndexerExecutablePath(
    const std::string & config_dir)
{
    std::vector<std::string> candidates;

    if (!config_dir.empty()) {
        candidates.push_back(config_dir + "\\cxparser_clang_indexer.exe");
        candidates.push_back(config_dir + "\\clang_indexer.exe");
        candidates.push_back(config_dir + "\\bin\\cxparser_clang_indexer.exe");
    }

    for (const auto & path : candidates) {
        if (FileExists(path)) {
            return path;
        }
    }

    return std::string();
}

CommandResult BuildRunClangAstParserResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    using namespace ::codex_lan_agent;
    
    CommandResult result;

    ClangIndexerOptions options;
    options.source_file = params.GetString("source_file");
    options.compile_db_dir = params.GetString("compile_db_dir");
    options.compilation_database_path = params.GetString("compilation_database_path");
    options.output_json_path = params.GetString("output_json_path");
    options.project_root = params.GetString("project_root");
    options.verbose = params.GetBool("verbose", false);

    std::string target_ns_json = params.GetString("target_namespaces");
    if (!target_ns_json.empty()) {
        std::string cleaned = target_ns_json;
        if (!cleaned.empty() && cleaned.front() == '[') {
            cleaned = cleaned.substr(1);
        }
        if (!cleaned.empty() && cleaned.back() == ']') {
            cleaned.pop_back();
        }
        std::istringstream iss(cleaned);
        std::string item;
        while (std::getline(iss, item, ',')) {
            while (!item.empty() && (item.front() == '"' || item.front() == ' ')) {
                item.erase(item.begin());
            }
            while (!item.empty() && (item.back() == '"' || item.back() == ' ')) {
                item.pop_back();
            }
            if (!item.empty()) {
                options.target_namespaces.push_back(item);
            }
        }
    }

    std::string include_dirs_json = params.GetString("extra_include_dirs");
    if (!include_dirs_json.empty()) {
        std::string cleaned = include_dirs_json;
        if (!cleaned.empty() && cleaned.front() == '[') {
            cleaned = cleaned.substr(1);
        }
        if (!cleaned.empty() && cleaned.back() == ']') {
            cleaned.pop_back();
        }
        std::istringstream iss(cleaned);
        std::string item;
        while (std::getline(iss, item, ',')) {
            while (!item.empty() && (item.front() == '"' || item.front() == ' ')) {
                item.erase(item.begin());
            }
            while (!item.empty() && (item.back() == '"' || item.back() == ' ')) {
                item.pop_back();
            }
            if (!item.empty()) {
                options.extra_include_dirs.push_back(item);
            }
        }
    }

    std::string defines_json = params.GetString("extra_defines");
    if (!defines_json.empty()) {
        std::string cleaned = defines_json;
        if (!cleaned.empty() && cleaned.front() == '[') {
            cleaned = cleaned.substr(1);
        }
        if (!cleaned.empty() && cleaned.back() == ']') {
            cleaned.pop_back();
        }
        std::istringstream iss(cleaned);
        std::string item;
        while (std::getline(iss, item, ',')) {
            while (!item.empty() && (item.front() == '"' || item.front() == ' ')) {
                item.erase(item.begin());
            }
            while (!item.empty() && (item.back() == '"' || item.back() == ' ')) {
                item.pop_back();
            }
            if (!item.empty()) {
                options.extra_defines.push_back(item);
            }
        }
    }

    if (options.source_file.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "source_file is required for Clang AST parsing";
        result.fields["result"] = "parse_blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "missing_source_file";
        result.fields["summary"] = "Clang AST parse blocked: missing source_file";
        result.fields["next_action"] = "provide source_file path to parse";
        return result;
    }

    std::string resolved_compile_db_dir;
    std::string resolved_compile_db_file;
    std::string compile_db_error;
    if (!ResolveCompilationDatabaseLocation(
            options,
            &resolved_compile_db_dir,
            &resolved_compile_db_file,
            &compile_db_error)) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = compile_db_error;
        result.fields["result"] = "parse_blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "invalid_compilation_database";
        result.fields["summary"] = "Clang AST parse blocked: invalid compilation database input";
        result.fields["next_action"] =
            "provide compile_db_dir pointing to a directory containing compile_commands.json, or compilation_database_path pointing to the compile_commands.json file";
        return result;
    }
    if (!resolved_compile_db_dir.empty()) {
        options.compile_db_dir = resolved_compile_db_dir;
        options.compilation_database_path = resolved_compile_db_file;
        result.fields["resolved_compile_db_dir"] = resolved_compile_db_dir;
        result.fields["resolved_compilation_database_path"] = resolved_compile_db_file;
        result.fields["compile_db_mode"] = "compile_commands_json";
    } else {
        result.fields["compile_db_mode"] = "fallback_arguments";
    }

    std::string fallback_block_reason;
    std::string fallback_block_detail;
    if (IsLikelyComplexCppTranslationUnit(
            options,
            &fallback_block_reason,
            &fallback_block_detail)) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = fallback_block_detail;
        result.fields["result"] = "parse_blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = fallback_block_reason;
        result.fields["summary"] = "Clang AST parse blocked: compile_commands.json is required for this translation unit";
        result.fields["next_action"] =
            "generate and pass compile_db_dir/compilation_database_path, or provide extra_include_dirs for all project and third-party headers";
        return result;
    }

    ClangAstParseResult ast_result = RunClangAstParser(options);

    if (!ast_result.success) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["error"] = ast_result.error.empty()
            ? "Clang AST parsing failed"
            : ast_result.error;
        result.fields["result"] = "parse_failed";
        result.fields["preflight_status"] = "failed";
        result.fields["preflight_reason_code"] = "clang_ast_parse_failed";
        result.fields["summary"] = "Clang AST parse failed";
        result.fields["error_detail"] = ast_result.error;
        return result;
    }

    result.ok = true;
    result.exit_code = 0;
    result.fields["result"] = "parse_success";
    result.fields["status"] = "completed";
    result.fields["tool"] = "clang_ast_indexer";
    result.fields["symbol_count"] = std::to_string(
        ast_result.schema.classes.size() +
        ast_result.schema.free_functions.size());
    result.fields["class_count"] = std::to_string(
        ast_result.schema.classes.size());
    result.fields["classes_count"] = std::to_string(
        ast_result.schema.classes.size());
    result.fields["function_count"] = std::to_string(
        ast_result.schema.free_functions.size());
    result.fields["functions_count"] = std::to_string(
        ast_result.schema.free_functions.size());
    result.fields["call_ref_count"] = std::to_string(
        ast_result.call_refs.size());
    result.fields["namespace_count"] = std::to_string(
        ast_result.schema.namespaces.size());
    result.fields["namespaces_count"] = std::to_string(
        ast_result.schema.namespaces.size());
    result.fields["elapsed_ms"] = std::to_string(ast_result.elapsed_ms);
    result.fields["preflight_status"] = "ready";

    std::string json_output = SerializeAstParseResultToJson(ast_result);
    result.fields["ast_json"] = json_output;

    std::string cxscript_output = BuildCxScriptFromSchema(ast_result.schema);
    result.fields["generated_cxscript"] = cxscript_output;

    CxScriptValidationResult validation = ValidateCxScriptSyntax(cxscript_output);
    result.fields["generated_cxscript_valid"] = validation.valid ? "true" : "false";
    result.fields["generated_cxscript_validation"] = SerializeCxScriptValidationToJson(validation);

    if (!options.output_json_path.empty()) {
        result.fields["output_json_path"] = options.output_json_path;
        std::ofstream output(options.output_json_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            result.ok = false;
            result.exit_code = 500;
            result.fields["error"] = "failed to open output_json_path for writing";
            result.fields["result"] = "parse_failed";
            result.fields["preflight_status"] = "failed";
            result.fields["preflight_reason_code"] = "output_json_write_failed";
            result.fields["summary"] = "Clang AST parse failed while writing output_json_path";
            return result;
        }
        output << json_output;
        output.close();
        result.fields["output_json_written"] = output.good() ? "true" : "false";
        if (!output.good()) {
            result.ok = false;
            result.exit_code = 500;
            result.fields["error"] = "failed to write parser JSON to output_json_path";
            result.fields["result"] = "parse_failed";
            result.fields["preflight_status"] = "failed";
            result.fields["preflight_reason_code"] = "output_json_write_failed";
            result.fields["summary"] = "Clang AST parse failed while writing output_json_path";
            return result;
        }
    }

    result.fields["summary"] = "Clang AST parsed successfully: " +
        std::to_string(ast_result.schema.classes.size()) + " classes, " +
        std::to_string(ast_result.schema.free_functions.size()) + " functions, " +
        std::to_string(ast_result.call_refs.size()) + " call refs";

    result.fields["next_action"] = validation.valid
        ? "generated_cxscript is syntactically valid CxScript. Use it as a template for writing cxsc test scripts."
        : "generated_cxscript has syntax issues. Check generated_cxscript_validation for details.";

    return result;
}

CommandResult BuildRunCfgResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    using namespace ::codex_lan_agent;

    CommandResult result;

    ClangIndexerOptions options;
    options.source_file = params.GetString("source_file");
    options.compile_db_dir = params.GetString("compile_db_dir");
    options.compilation_database_path = params.GetString("compilation_database_path");
    options.verbose = params.GetBool("verbose", false);
    options.output_json_path = params.GetString("output_json_path");
    const std::string output_dir = params.GetString("output_dir");
    const bool include_dot = params.GetBool("include_dot", true);
    const std::string function_name = params.GetString("function_name");
    std::vector<std::string> function_names;
    AppendStringListFromJsonish(params.GetString("function_names"), &function_names);
    int offset_functions = params.GetInt("offset_functions", 0);
    int max_functions = params.GetInt("max_functions", 0);
    if (offset_functions < 0) {
        offset_functions = 0;
    }
    if (max_functions < 0) {
        max_functions = 0;
    }

    std::string extra_includes_json = params.GetString("extra_include_dirs");
    if (!extra_includes_json.empty()) {
        std::string cleaned = extra_includes_json;
        if (!cleaned.empty() && cleaned.front() == '[') cleaned = cleaned.substr(1);
        if (!cleaned.empty() && cleaned.back() == ']') cleaned.pop_back();
        std::istringstream iss(cleaned);
        std::string token;
        while (std::getline(iss, token, ',')) {
            token.erase(0, token.find_first_not_of(" \t\""));
            token.erase(token.find_last_not_of(" \t\"") + 1);
            if (!token.empty()) {
                options.extra_include_dirs.push_back(token);
            }
        }
    }

    result.fields["source_file"] = options.source_file;
    result.fields["compile_db_dir"] = options.compile_db_dir;
    result.fields["cfg_scope"] = "source_file";
    result.fields["filtered_to_source_file"] = "true";

    std::string resolved_compile_db_dir;
    std::string resolved_compile_db_file;
    std::string compile_db_error;
    if (!ResolveCompilationDatabaseLocation(
            options,
            &resolved_compile_db_dir,
            &resolved_compile_db_file,
            &compile_db_error)) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["status"] = "blocked";
        result.fields["error"] = compile_db_error;
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "invalid_compilation_database";
        result.fields["summary"] = "CFG construction blocked: invalid compilation database input";
        result.fields["next_action"] =
            "provide compile_db_dir pointing to a directory containing compile_commands.json, or compilation_database_path pointing to the compile_commands.json file";
        return result;
    }
    if (!resolved_compile_db_dir.empty()) {
        options.compile_db_dir = resolved_compile_db_dir;
        options.compilation_database_path = resolved_compile_db_file;
        result.fields["resolved_compile_db_dir"] = resolved_compile_db_dir;
        result.fields["resolved_compilation_database_path"] = resolved_compile_db_file;
        result.fields["compile_db_mode"] = "compile_commands_json";
    } else {
        result.fields["compile_db_mode"] = "fallback_arguments";
    }

    std::string fallback_block_reason;
    std::string fallback_block_detail;
    if (IsLikelyComplexCppTranslationUnit(
            options,
            &fallback_block_reason,
            &fallback_block_detail)) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["status"] = "blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = fallback_block_reason;
        result.fields["error"] = fallback_block_detail;
        result.fields["summary"] = "CFG construction blocked: compile_commands.json is required for this translation unit";
        result.fields["next_action"] =
            "generate and pass compile_db_dir/compilation_database_path, or provide extra_include_dirs for all project and third-party headers";
        return result;
    }

    CfgBuildResult cfg_result = RunCfgBuilder(options);

    if (!cfg_result.success) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["status"] = "failed";
        result.fields["error"] = cfg_result.error;
        result.fields["summary"] = "CFG construction failed: " + cfg_result.error;
        result.fields["build_time_ms"] = std::to_string(cfg_result.build_time_ms);
        return result;
    }

    CfgBuildResult response_cfg_result = cfg_result;
    const int original_total_functions = cfg_result.total_functions;
    const int original_total_blocks = cfg_result.total_blocks;
    const int original_total_edges = cfg_result.total_edges;
    int matched_total_functions = original_total_functions;
    int matched_total_blocks = original_total_blocks;
    int matched_total_edges = original_total_edges;
    const bool cfg_truncated =
        ApplyCfgFunctionSelection(
            function_name,
            function_names,
            offset_functions,
            max_functions,
            &response_cfg_result,
            &matched_total_functions,
            &matched_total_blocks,
            &matched_total_edges);
    const bool cfg_filtered = !function_name.empty() || !function_names.empty();
    const PageWindowInfo cfg_page =
        ComputePageWindowInfo(matched_total_functions, offset_functions, max_functions);

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "success";
    result.fields["cfg_json"] = SerializeCfgBuildResultToJson(response_cfg_result);
    result.fields["include_dot"] = include_dot ? "true" : "false";
    if (include_dot) {
        result.fields["cfg_dot"] = SerializeCfgToDot(response_cfg_result);
    }
    result.fields["total_functions"] = std::to_string(response_cfg_result.total_functions);
    result.fields["total_blocks"] = std::to_string(response_cfg_result.total_blocks);
    result.fields["total_edges"] = std::to_string(response_cfg_result.total_edges);
    result.fields["original_total_functions"] = std::to_string(original_total_functions);
    result.fields["original_total_blocks"] = std::to_string(original_total_blocks);
    result.fields["original_total_edges"] = std::to_string(original_total_edges);
    result.fields["matched_total_functions"] = std::to_string(matched_total_functions);
    result.fields["matched_total_blocks"] = std::to_string(matched_total_blocks);
    result.fields["matched_total_edges"] = std::to_string(matched_total_edges);
    result.fields["cfg_filtered"] = cfg_filtered ? "true" : "false";
    result.fields["function_name_filter"] = function_name;
    result.fields["function_names_filter_count"] = std::to_string(function_names.size());
    SetCfgPaginationFields(&result, cfg_truncated, offset_functions, max_functions, cfg_page);
    result.fields["build_time_ms"] = std::to_string(cfg_result.build_time_ms);
    result.fields["preflight_status"] = "ready";
    result.fields["cfg_scope"] = "source_file";
    result.fields["filtered_to_source_file"] = "true";
    if (!options.output_json_path.empty()) {
        result.fields["output_json_path"] = options.output_json_path;
        std::ofstream output(options.output_json_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            result.ok = false;
            result.exit_code = 500;
            result.fields["status"] = "failed";
            result.fields["error"] = "failed to open output_json_path for writing";
            result.fields["summary"] = "CFG construction failed while writing output_json_path";
            result.fields["preflight_reason_code"] = "output_json_write_failed";
            return result;
        }
        output << result.fields["cfg_json"];
        output.close();
        result.fields["output_json_written"] = output.good() ? "true" : "false";
        if (!output.good()) {
            result.ok = false;
            result.exit_code = 500;
            result.fields["status"] = "failed";
            result.fields["error"] = "failed to write cfg_json to output_json_path";
            result.fields["summary"] = "CFG construction failed while writing output_json_path";
            result.fields["preflight_reason_code"] = "output_json_write_failed";
            return result;
        }
    }

    std::ostringstream summary;
    summary << "CFG built: "
            << response_cfg_result.total_functions << " functions, "
            << response_cfg_result.total_blocks << " blocks, "
            << response_cfg_result.total_edges << " edges";
    result.fields["summary"] = summary.str();

    if (!output_dir.empty()) {
        const std::string cfg_dot = SerializeCfgToDot(response_cfg_result);
        const std::filesystem::path bundle_dir_path(output_dir);
        const std::string summary_json =
            "{\n"
            "  \"tool\": \"clang_cfg_builder\",\n"
            "  \"source_file\": \"" + EscapeJsonValue(options.source_file) + "\",\n"
            "  \"artifact_cfg_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "cfg.json").string()) + "\",\n"
            "  \"artifact_cfg_dot_path\": \"" + EscapeJsonValue((bundle_dir_path / "cfg.dot").string()) + "\",\n"
            "  \"artifact_summary_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "summary.json").string()) + "\",\n"
            "  \"summary\": \"" + EscapeJsonValue(result.fields["summary"]) + "\",\n"
            "  \"total_functions\": " + result.fields["total_functions"] + ",\n"
            "  \"total_blocks\": " + result.fields["total_blocks"] + ",\n"
            "  \"total_edges\": " + result.fields["total_edges"] + ",\n"
            "  \"original_total_functions\": " + result.fields["original_total_functions"] + ",\n"
            "  \"matched_total_functions\": " + result.fields["matched_total_functions"] + ",\n"
            "  \"cfg_has_more\": " + result.fields["cfg_has_more"] + ",\n"
            "  \"next_offset_functions\": " + result.fields["next_offset_functions"] + "\n"
            "}";
        if (!WriteArtifactBundleAndRecordPaths(
                output_dir,
                {
                    {"cfg.json", result.fields["cfg_json"]},
                    {"cfg.dot", cfg_dot},
                    {"summary.json", summary_json}
                },
                {
                    {"artifact_cfg_json_path", "cfg.json"},
                    {"artifact_cfg_dot_path", "cfg.dot"},
                    {"artifact_summary_json_path", "summary.json"}
                },
                "CFG construction failed while creating output_dir",
                "CFG construction failed while writing output_dir artifacts",
                &result)) {
            return result;
        }
    }
    result.fields["next_action"] = "Use cfg_json for detailed block/edge structure. Build call graph next.";

    return result;
}

CommandResult BuildQueryCfgArtifactResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    (void)config;
    CommandResult result;

    const std::string artifact_summary_path = params.GetString("artifact_summary_path");
    std::string artifact_json_path_source;
    std::string artifact_json_path_resolution_detail;
    const std::string artifact_json_path =
        ResolveArtifactJsonPathFromSummary(
            params.GetString("artifact_json_path"),
            artifact_summary_path,
            "cfg.json",
            "artifact_cfg_json_path",
            &artifact_json_path_source,
            &artifact_json_path_resolution_detail);
    const std::string output_dir = params.GetString("output_dir");
    const bool include_dot = params.GetBool("include_dot", true);
    const std::string function_name = params.GetString("function_name");
    std::vector<std::string> function_names;
    AppendStringListFromJsonish(params.GetString("function_names"), &function_names);
    int offset_functions = params.GetInt("offset_functions", 0);
    int max_functions = params.GetInt("max_functions", 0);
    if (offset_functions < 0) offset_functions = 0;
    if (max_functions < 0) max_functions = 0;

    if (artifact_json_path.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["status"] = "blocked";
        result.fields["error"] = "artifact_json_path or artifact_summary_path is required for CFG artifact query";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "missing_artifact_json_path";
        result.fields["summary"] = "CFG artifact query blocked: missing artifact_json_path/artifact_summary_path";
        return result;
    }

    const std::string artifact_json = ReadWholeFileText(artifact_json_path);
    if (artifact_json.empty()) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["status"] = "failed";
        result.fields["error"] = "failed to read artifact_json_path";
        result.fields["preflight_status"] = "failed";
        result.fields["preflight_reason_code"] = "artifact_read_failed";
        result.fields["summary"] = "CFG artifact query failed while reading artifact_json_path";
        return result;
    }

    CfgBuildResult cfg_result;
    if (!ParseCfgArtifactJson(artifact_json, &cfg_result)) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["status"] = "failed";
        result.fields["error"] = "failed to parse CFG artifact JSON";
        result.fields["preflight_status"] = "failed";
        result.fields["preflight_reason_code"] = "artifact_parse_failed";
        result.fields["summary"] = "CFG artifact query failed while parsing artifact_json_path";
        return result;
    }

    const int original_total_functions = cfg_result.total_functions;
    const int original_total_blocks = cfg_result.total_blocks;
    const int original_total_edges = cfg_result.total_edges;
    int matched_total_functions = original_total_functions;
    int matched_total_blocks = original_total_blocks;
    int matched_total_edges = original_total_edges;
    const bool cfg_truncated =
        ApplyCfgFunctionSelection(
            function_name,
            function_names,
            offset_functions,
            max_functions,
            &cfg_result,
            &matched_total_functions,
            &matched_total_blocks,
            &matched_total_edges);
    const bool cfg_filtered = !function_name.empty() || !function_names.empty();
    const PageWindowInfo cfg_page =
        ComputePageWindowInfo(matched_total_functions, offset_functions, max_functions);

    const std::string cfg_json = SerializeCfgBuildResultToJson(cfg_result);
    const std::string cfg_dot = SerializeCfgToDot(cfg_result);

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "success";
    result.fields["result"] = "cfg_artifact_query_success";
    result.fields["tool"] = "clang_cfg_artifact_query";
    SetArtifactResolutionFields(
        &result,
        artifact_json_path,
        artifact_summary_path,
        artifact_json_path_source,
        artifact_json_path_resolution_detail);
    result.fields["cfg_json"] = cfg_json;
    result.fields["include_dot"] = include_dot ? "true" : "false";
    if (include_dot) {
        result.fields["cfg_dot"] = cfg_dot;
    }
    result.fields["total_functions"] = std::to_string(cfg_result.total_functions);
    result.fields["total_blocks"] = std::to_string(cfg_result.total_blocks);
    result.fields["total_edges"] = std::to_string(cfg_result.total_edges);
    result.fields["original_total_functions"] = std::to_string(original_total_functions);
    result.fields["original_total_blocks"] = std::to_string(original_total_blocks);
    result.fields["original_total_edges"] = std::to_string(original_total_edges);
    result.fields["matched_total_functions"] = std::to_string(matched_total_functions);
    result.fields["matched_total_blocks"] = std::to_string(matched_total_blocks);
    result.fields["matched_total_edges"] = std::to_string(matched_total_edges);
    result.fields["cfg_filtered"] = cfg_filtered ? "true" : "false";
    result.fields["function_name_filter"] = function_name;
    result.fields["function_names_filter_count"] = std::to_string(function_names.size());
    SetCfgPaginationFields(&result, cfg_truncated, offset_functions, max_functions, cfg_page);
    result.fields["preflight_status"] = "ready";

    std::ostringstream summary;
    summary << "CFG artifact queried: "
            << cfg_result.total_functions << " functions, "
            << cfg_result.total_blocks << " blocks, "
            << cfg_result.total_edges << " edges";
    result.fields["summary"] = summary.str();

    if (!output_dir.empty()) {
        const std::filesystem::path bundle_dir_path(output_dir);
        const std::string summary_json =
            "{\n"
            "  \"tool\": \"clang_cfg_artifact_query\",\n"
            "  \"artifact_json_path\": \"" + EscapeJsonValue(artifact_json_path) + "\",\n"
            "  \"artifact_cfg_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "cfg.json").string()) + "\",\n"
            "  \"artifact_cfg_dot_path\": \"" + EscapeJsonValue((bundle_dir_path / "cfg.dot").string()) + "\",\n"
            "  \"artifact_summary_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "summary.json").string()) + "\",\n"
            "  \"summary\": \"" + EscapeJsonValue(result.fields["summary"]) + "\",\n"
            "  \"total_functions\": " + result.fields["total_functions"] + ",\n"
            "  \"total_blocks\": " + result.fields["total_blocks"] + ",\n"
            "  \"total_edges\": " + result.fields["total_edges"] + "\n"
            "}";
        if (!WriteArtifactBundleAndRecordPaths(
                output_dir,
                {
                    {"cfg.json", cfg_json},
                    {"cfg.dot", cfg_dot},
                    {"summary.json", summary_json}
                },
                {
                    {"artifact_cfg_json_path", "cfg.json"},
                    {"artifact_cfg_dot_path", "cfg.dot"},
                    {"artifact_summary_json_path", "summary.json"}
                },
                "CFG artifact query failed while creating output_dir",
                "CFG artifact query failed while writing output_dir artifacts",
                &result)) {
            return result;
        }
    }
    result.fields["next_action"] = "Use cfg_json for detailed block/edge structure or cfg_has_more/next_offset_functions for pagination.";
    return result;
}

CommandResult BuildRunCallGraphResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    using namespace ::codex_lan_agent;
    (void)config;

    CommandResult result;

    ClangIndexerOptions options;
    options.source_file = params.GetString("source_file");
    options.compile_db_dir = params.GetString("compile_db_dir");
    options.compilation_database_path = params.GetString("compilation_database_path");
    options.output_json_path = params.GetString("output_json_path");
    options.project_root = params.GetString("project_root");
    options.verbose = params.GetBool("verbose", false);
    const std::string output_dir = params.GetString("output_dir");
    const bool include_dot = params.GetBool("include_dot", true);
    int max_nodes = params.GetInt("max_nodes", 0);
    int offset_edges = params.GetInt("offset_edges", 0);
    int max_edges = params.GetInt("max_edges", 0);
    if (max_nodes < 0) max_nodes = 0;
    if (offset_edges < 0) offset_edges = 0;
    if (max_edges < 0) max_edges = 0;
    const std::string focus_symbol = params.GetString("focus_symbol");
    int neighborhood_depth = params.GetInt("neighborhood_depth", 1);
    if (neighborhood_depth < 0) neighborhood_depth = 0;
    if (neighborhood_depth > 16) neighborhood_depth = 16;
    std::string neighborhood_direction = params.GetString("neighborhood_direction", "both");
    if (neighborhood_direction != "incoming" &&
        neighborhood_direction != "outgoing" &&
        neighborhood_direction != "both") {
        neighborhood_direction = "both";
    }

    AppendStringListFromJsonish(
        params.GetString("target_namespaces"),
        &options.target_namespaces);
    AppendStringListFromJsonish(
        params.GetString("extra_include_dirs"),
        &options.extra_include_dirs);
    AppendStringListFromJsonish(
        params.GetString("extra_defines"),
        &options.extra_defines);

    if (options.source_file.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "source_file is required for Call Graph construction";
        result.fields["status"] = "blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "missing_source_file";
        result.fields["summary"] = "Call Graph construction blocked: missing source_file";
        result.fields["next_action"] = "provide source_file path to analyze";
        return result;
    }

    result.fields["source_file"] = options.source_file;
    result.fields["call_graph_scope"] = "source_file_call_sites";
    result.fields["filtered_to_source_file"] = "true";

    std::string resolved_compile_db_dir;
    std::string resolved_compile_db_file;
    std::string compile_db_error;
    if (!ResolveCompilationDatabaseLocation(
            options,
            &resolved_compile_db_dir,
            &resolved_compile_db_file,
            &compile_db_error)) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = compile_db_error;
        result.fields["status"] = "blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "invalid_compilation_database";
        result.fields["summary"] = "Call Graph construction blocked: invalid compilation database input";
        result.fields["next_action"] =
            "provide compile_db_dir pointing to a directory containing compile_commands.json, or compilation_database_path pointing to the compile_commands.json file";
        return result;
    }
    if (!resolved_compile_db_dir.empty()) {
        options.compile_db_dir = resolved_compile_db_dir;
        options.compilation_database_path = resolved_compile_db_file;
        result.fields["resolved_compile_db_dir"] = resolved_compile_db_dir;
        result.fields["resolved_compilation_database_path"] = resolved_compile_db_file;
        result.fields["compile_db_mode"] = "compile_commands_json";
    } else {
        result.fields["compile_db_mode"] = "fallback_arguments";
    }

    std::string fallback_block_reason;
    std::string fallback_block_detail;
    if (IsLikelyComplexCppTranslationUnit(
            options,
            &fallback_block_reason,
            &fallback_block_detail)) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = fallback_block_detail;
        result.fields["status"] = "blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = fallback_block_reason;
        result.fields["summary"] = "Call Graph construction blocked: compile_commands.json is required for this translation unit";
        result.fields["next_action"] =
            "generate and pass compile_db_dir/compilation_database_path, or provide extra_include_dirs for all project and third-party headers";
        return result;
    }

    ClangAstParseResult ast_result = RunClangAstParser(options);
    if (!ast_result.success) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["error"] = ast_result.error.empty()
            ? "Clang AST parsing failed"
            : ast_result.error;
        result.fields["status"] = "failed";
        result.fields["preflight_status"] = "failed";
        result.fields["preflight_reason_code"] = "clang_ast_parse_failed";
        result.fields["summary"] = "Call Graph construction failed while parsing AST";
        result.fields["error_detail"] = ast_result.error;
        return result;
    }

    const std::string normalized_source = NormalizeJsonPathProbe(options.source_file);
    std::set<std::string> node_set;
    std::map<std::pair<std::string, std::string>, CallGraphEdgeInfo> edge_map;
    int filtered_call_refs = 0;

    for (const auto & ref : ast_result.call_refs) {
        if (!normalized_source.empty() &&
            NormalizeJsonPathProbe(ref.source_file) != normalized_source) {
            continue;
        }

        const std::string caller = ref.caller_name.empty() ? "(global)" : ref.caller_name;
        const std::string callee = ref.callee_name.empty() ? "(unknown)" : ref.callee_name;
        node_set.insert(caller);
        node_set.insert(callee);

        auto key = std::make_pair(caller, callee);
        auto & edge = edge_map[key];
        if (edge.count == 0) {
            edge.caller = caller;
            edge.callee = callee;
            edge.first_source_file = ref.source_file;
            edge.first_source_line = ref.source_line;
            edge.first_source_col = ref.source_col;
        }
        ++edge.count;
        ++filtered_call_refs;
    }

    std::vector<std::string> nodes(node_set.begin(), node_set.end());
    std::vector<CallGraphEdgeInfo> edges;
    edges.reserve(edge_map.size());
    for (const auto & item : edge_map) {
        edges.push_back(item.second);
    }

    const std::size_t original_node_count = nodes.size();
    const std::size_t original_edge_count = edges.size();
    const bool graph_filtered =
        ApplyCallGraphNeighborhood(
            focus_symbol,
            neighborhood_depth,
            neighborhood_direction,
            &nodes,
            &edges);
    const std::size_t matched_node_count = nodes.size();
    const std::size_t matched_edge_count = edges.size();
    const PageWindowInfo graph_page =
        ComputePageWindowInfo(static_cast<int>(matched_edge_count), offset_edges, max_edges);
    const bool graph_truncated =
        ApplyCallGraphLimits(max_nodes, offset_edges, max_edges, &nodes, &edges);

    const std::string call_graph_json = SerializeCallGraphJson(nodes, edges);
    const std::string call_graph_dot = SerializeCallGraphDot(nodes, edges);

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "success";
    result.fields["result"] = "call_graph_success";
    result.fields["tool"] = "clang_call_graph_builder";
    result.fields["node_count"] = std::to_string(nodes.size());
    result.fields["edge_count"] = std::to_string(edges.size());
    result.fields["original_node_count"] = std::to_string(original_node_count);
    result.fields["original_edge_count"] = std::to_string(original_edge_count);
    result.fields["matched_node_count"] = std::to_string(matched_node_count);
    result.fields["matched_edge_count"] = std::to_string(matched_edge_count);
    result.fields["graph_filtered"] = graph_filtered ? "true" : "false";
    result.fields["focus_symbol"] = focus_symbol;
    result.fields["neighborhood_depth"] = std::to_string(neighborhood_depth);
    result.fields["neighborhood_direction"] = neighborhood_direction;
    SetGraphPaginationFields(&result, graph_truncated, max_nodes, offset_edges, max_edges, graph_page);
    result.fields["call_ref_count"] = std::to_string(ast_result.call_refs.size());
    result.fields["filtered_call_ref_count"] = std::to_string(filtered_call_refs);
    result.fields["elapsed_ms"] = std::to_string(ast_result.elapsed_ms);
    result.fields["preflight_status"] = "ready";
    result.fields["call_graph_json"] = call_graph_json;
    result.fields["include_dot"] = include_dot ? "true" : "false";
    if (include_dot) {
        result.fields["call_graph_dot"] = call_graph_dot;
    }

    if (!options.output_json_path.empty()) {
        result.fields["output_json_path"] = options.output_json_path;
        std::ofstream output(options.output_json_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            result.ok = false;
            result.exit_code = 500;
            result.fields["error"] = "failed to open output_json_path for writing";
            result.fields["status"] = "failed";
            result.fields["preflight_reason_code"] = "output_json_write_failed";
            result.fields["summary"] = "Call Graph construction failed while writing output_json_path";
            return result;
        }
        output << call_graph_json;
        output.close();
        result.fields["output_json_written"] = output.good() ? "true" : "false";
        if (!output.good()) {
            result.ok = false;
            result.exit_code = 500;
            result.fields["error"] = "failed to write call_graph_json to output_json_path";
            result.fields["status"] = "failed";
            result.fields["preflight_reason_code"] = "output_json_write_failed";
            result.fields["summary"] = "Call Graph construction failed while writing output_json_path";
            return result;
        }
    }

    std::ostringstream summary;
    summary << "Call Graph built: "
            << nodes.size() << " nodes, "
            << edges.size() << " edges, "
            << filtered_call_refs << " source-file call refs";
    result.fields["summary"] = summary.str();

    if (!output_dir.empty()) {
        const std::filesystem::path bundle_dir_path(output_dir);
        const std::string summary_json =
            "{\n"
            "  \"tool\": \"clang_call_graph_builder\",\n"
            "  \"source_file\": \"" + EscapeJsonValue(options.source_file) + "\",\n"
            "  \"artifact_call_graph_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "call_graph.json").string()) + "\",\n"
            "  \"artifact_call_graph_dot_path\": \"" + EscapeJsonValue((bundle_dir_path / "call_graph.dot").string()) + "\",\n"
            "  \"artifact_summary_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "summary.json").string()) + "\",\n"
            "  \"summary\": \"" + EscapeJsonValue(result.fields["summary"]) + "\",\n"
            "  \"node_count\": " + result.fields["node_count"] + ",\n"
            "  \"edge_count\": " + result.fields["edge_count"] + ",\n"
            "  \"original_node_count\": " + result.fields["original_node_count"] + ",\n"
            "  \"original_edge_count\": " + result.fields["original_edge_count"] + ",\n"
            "  \"matched_node_count\": " + result.fields["matched_node_count"] + ",\n"
            "  \"matched_edge_count\": " + result.fields["matched_edge_count"] + ",\n"
            "  \"graph_has_more\": " + result.fields["graph_has_more"] + ",\n"
            "  \"next_offset_edges\": " + result.fields["next_offset_edges"] + "\n"
            "}";
        if (!WriteArtifactBundleAndRecordPaths(
                output_dir,
                {
                    {"call_graph.json", call_graph_json},
                    {"call_graph.dot", call_graph_dot},
                    {"summary.json", summary_json}
                },
                {
                    {"artifact_call_graph_json_path", "call_graph.json"},
                    {"artifact_call_graph_dot_path", "call_graph.dot"},
                    {"artifact_summary_json_path", "summary.json"}
                },
                "Call Graph construction failed while creating output_dir",
                "Call Graph construction failed while writing output_dir artifacts",
                &result)) {
            return result;
        }
    }
    result.fields["next_action"] = "Use call_graph_json for dependency traversal or call_graph_dot for visualization. Build DFG next.";
    return result;
}

CommandResult BuildRunDfgResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    using namespace ::codex_lan_agent;
    (void)config;

    CommandResult result;

    ClangIndexerOptions options;
    options.source_file = params.GetString("source_file");
    options.compile_db_dir = params.GetString("compile_db_dir");
    options.compilation_database_path = params.GetString("compilation_database_path");
    options.output_json_path = params.GetString("output_json_path");
    options.project_root = params.GetString("project_root");
    options.verbose = params.GetBool("verbose", false);
    const std::string output_dir = params.GetString("output_dir");
    const bool include_dot = params.GetBool("include_dot", true);
    const bool include_path_metadata = params.GetBool("include_path_metadata", false);
    int max_nodes = params.GetInt("max_nodes", 0);
    int offset_edges = params.GetInt("offset_edges", 0);
    int max_edges = params.GetInt("max_edges", 0);
    if (max_nodes < 0) max_nodes = 0;
    if (offset_edges < 0) offset_edges = 0;
    if (max_edges < 0) max_edges = 0;
    const std::string focus_symbol = params.GetString("focus_symbol");
    int neighborhood_depth = params.GetInt("neighborhood_depth", 1);
    if (neighborhood_depth < 0) neighborhood_depth = 0;
    if (neighborhood_depth > 16) neighborhood_depth = 16;
    std::string neighborhood_direction = params.GetString("neighborhood_direction", "both");
    if (neighborhood_direction != "incoming" &&
        neighborhood_direction != "outgoing" &&
        neighborhood_direction != "both") {
        neighborhood_direction = "both";
    }

    AppendStringListFromJsonish(
        params.GetString("target_namespaces"),
        &options.target_namespaces);
    AppendStringListFromJsonish(
        params.GetString("extra_include_dirs"),
        &options.extra_include_dirs);
    AppendStringListFromJsonish(
        params.GetString("extra_defines"),
        &options.extra_defines);

    if (options.source_file.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "source_file is required for Data Flow Graph construction";
        result.fields["status"] = "blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "missing_source_file";
        result.fields["summary"] = "DFG construction blocked: missing source_file";
        result.fields["next_action"] = "provide source_file path to analyze";
        return result;
    }

    result.fields["source_file"] = options.source_file;
    result.fields["dfg_scope"] = "source_file_lexical_def_use";
    result.fields["filtered_to_source_file"] = "true";

    std::string resolved_compile_db_dir;
    std::string resolved_compile_db_file;
    std::string compile_db_error;
    if (!ResolveCompilationDatabaseLocation(
            options,
            &resolved_compile_db_dir,
            &resolved_compile_db_file,
            &compile_db_error)) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = compile_db_error;
        result.fields["status"] = "blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "invalid_compilation_database";
        result.fields["summary"] = "DFG construction blocked: invalid compilation database input";
        result.fields["next_action"] =
            "provide compile_db_dir pointing to a directory containing compile_commands.json, or compilation_database_path pointing to the compile_commands.json file";
        return result;
    }
    if (!resolved_compile_db_dir.empty()) {
        options.compile_db_dir = resolved_compile_db_dir;
        options.compilation_database_path = resolved_compile_db_file;
        result.fields["resolved_compile_db_dir"] = resolved_compile_db_dir;
        result.fields["resolved_compilation_database_path"] = resolved_compile_db_file;
        result.fields["compile_db_mode"] = "compile_commands_json";
    } else {
        result.fields["compile_db_mode"] = "fallback_arguments";
    }

    std::string fallback_block_reason;
    std::string fallback_block_detail;
    if (IsLikelyComplexCppTranslationUnit(
            options,
            &fallback_block_reason,
            &fallback_block_detail)) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = fallback_block_detail;
        result.fields["status"] = "blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = fallback_block_reason;
        result.fields["summary"] = "DFG construction blocked: compile_commands.json is required for this translation unit";
        result.fields["next_action"] =
            "generate and pass compile_db_dir/compilation_database_path, or provide extra_include_dirs for all project and third-party headers";
        return result;
    }

    ClangAstParseResult ast_result = RunClangAstParser(options);
    if (!ast_result.success) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["error"] = ast_result.error.empty()
            ? "Clang AST parsing failed"
            : ast_result.error;
        result.fields["status"] = "failed";
        result.fields["preflight_status"] = "failed";
        result.fields["preflight_reason_code"] = "clang_ast_parse_failed";
        result.fields["summary"] = "DFG construction failed while parsing AST";
        result.fields["error_detail"] = ast_result.error;
        return result;
    }

    const std::string source_text = ReadWholeFileText(options.source_file);
    if (source_text.empty()) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["error"] = "failed to read source_file for DFG construction";
        result.fields["status"] = "failed";
        result.fields["preflight_status"] = "failed";
        result.fields["preflight_reason_code"] = "source_read_failed";
        result.fields["summary"] = "DFG construction failed while reading source_file";
        return result;
    }

    std::set<std::string> nodes;
    std::map<std::string, DfgEdgeInfo> edge_map;
    int definition_count = 0;
    int use_count = 0;
    const bool used_ast_statement_dfg =
        BuildAstStatementDfg(
            ast_result,
            options.source_file,
            &nodes,
            &edge_map,
            &definition_count,
            &use_count);

    if (!used_ast_statement_dfg) {
        int line_number = 0;
        std::istringstream lines(source_text);
        std::string raw_line;
        while (std::getline(lines, raw_line)) {
            ++line_number;
            std::string line = StripLineComment(raw_line);
            if (line.find("#include") != std::string::npos ||
                line.find("#define") != std::string::npos) {
                continue;
            }

            const std::size_t assign_pos = FindAssignmentOperator(line);
            if (assign_pos == std::string::npos) {
                continue;
            }

            std::string lhs = line.substr(0, assign_pos);
            std::string rhs = line.substr(assign_pos + 1);
            const std::string def = LastDfgIdentifier(lhs);
            if (def.empty()) {
                continue;
            }

            nodes.insert(def);
            ++definition_count;

            const std::vector<std::string> uses = ExtractDfgIdentifiers(rhs);
            for (const auto & use : uses) {
                if (use == def) {
                    continue;
                }
                nodes.insert(use);
                ++use_count;
                AddDfgEdge(use, def, "def_use", line_number, &edge_map);
            }
        }
    }

    std::vector<std::string> node_list(nodes.begin(), nodes.end());
    std::vector<DfgEdgeInfo> edges;
    edges.reserve(edge_map.size());
    for (const auto & item : edge_map) {
        edges.push_back(item.second);
    }
    const std::vector<ClangMethodInfo> ast_functions = FlattenAstFunctions(ast_result);
    const std::set<std::string> dfg_ast_functions =
        CollectDfgEdgeAstFunctions(edges, ast_functions, options.source_file);
    CfgBuildResult dfg_cfg_result;
    CfgPathSensitivityInfo dfg_path_info;
    if (include_path_metadata) {
        dfg_cfg_result = RunCfgBuilder(options);
        if (dfg_cfg_result.success) {
            dfg_path_info = ComputeCfgPathSensitivityInfo(dfg_cfg_result, dfg_ast_functions);
        }
    }
    const int source_scoped_call_ref_count =
        CountSourceScopedCallRefs(ast_result, options.source_file, nullptr);

    const std::size_t original_node_count = node_list.size();
    const std::size_t original_edge_count = edges.size();
    const bool graph_filtered =
        ApplyDfgNeighborhood(
            focus_symbol,
            neighborhood_depth,
            neighborhood_direction,
            &node_list,
            &edges);
    const std::size_t matched_node_count = node_list.size();
    const std::size_t matched_edge_count = edges.size();
    const PageWindowInfo graph_page =
        ComputePageWindowInfo(static_cast<int>(matched_edge_count), offset_edges, max_edges);
    const bool graph_truncated =
        ApplyDfgLimits(max_nodes, offset_edges, max_edges, &node_list, &edges);

    const std::string dfg_json = SerializeDfgJson(node_list, edges);
    const std::string dfg_dot = SerializeDfgDot(node_list, edges);

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "success";
    result.fields["result"] = "dfg_success";
    result.fields["tool"] = "clang_dfg_builder";
    result.fields["node_count"] = std::to_string(node_list.size());
    result.fields["edge_count"] = std::to_string(edges.size());
    result.fields["original_node_count"] = std::to_string(original_node_count);
    result.fields["original_edge_count"] = std::to_string(original_edge_count);
    result.fields["matched_node_count"] = std::to_string(matched_node_count);
    result.fields["matched_edge_count"] = std::to_string(matched_edge_count);
    result.fields["graph_filtered"] = graph_filtered ? "true" : "false";
    result.fields["focus_symbol"] = focus_symbol;
    result.fields["neighborhood_depth"] = std::to_string(neighborhood_depth);
    result.fields["neighborhood_direction"] = neighborhood_direction;
    SetGraphPaginationFields(&result, graph_truncated, max_nodes, offset_edges, max_edges, graph_page);
    result.fields["definition_count"] = std::to_string(definition_count);
    result.fields["use_count"] = std::to_string(use_count);
    result.fields["ast_function_count"] = std::to_string(ast_result.schema.free_functions.size());
    result.fields["ast_class_count"] = std::to_string(ast_result.schema.classes.size());
    result.fields["analysis_level"] = used_ast_statement_dfg ? "ast_statement_v1" : "hybrid_v1";
    result.fields["dfg_precision"] = used_ast_statement_dfg ? "ast_statement_def_use_v1" : "lexical_v1_ast_anchored";
    result.fields["ast_statement_level_status"] = used_ast_statement_dfg ? "ast_statement_refs_available" : "ast_ready_function_scope";
    result.fields["ast_data_flow_ref_count"] = std::to_string(ast_result.data_flow_refs.size());
    result.fields["ast_anchor_function_count"] = std::to_string(dfg_ast_functions.size());
    result.fields["ast_source_scoped_call_ref_count"] = std::to_string(source_scoped_call_ref_count);
    result.fields["include_path_metadata"] = include_path_metadata ? "true" : "false";
    result.fields["path_sensitive_status"] = include_path_metadata
        ? (dfg_cfg_result.success ? "cfg_branch_metadata_available" : "cfg_branch_metadata_unavailable")
        : "cfg_branch_metadata_deferred";
    result.fields["path_condition_candidate_count"] = std::to_string(dfg_path_info.branch_count);
    result.fields["control_dependency_candidate_count"] = std::to_string(dfg_path_info.branch_count);
    result.fields["cyclic_function_candidate_count"] = std::to_string(dfg_path_info.cyclic_function_count);
    result.fields["interprocedural_status"] = "call_graph_metadata_available";
    result.fields["interprocedural_call_ref_count"] = std::to_string(source_scoped_call_ref_count);
    result.fields["elapsed_ms"] = std::to_string(ast_result.elapsed_ms);
    result.fields["preflight_status"] = "ready";
    result.fields["dfg_json"] = dfg_json;
    result.fields["include_dot"] = include_dot ? "true" : "false";
    if (include_dot) {
        result.fields["dfg_dot"] = dfg_dot;
    }

    if (!options.output_json_path.empty()) {
        result.fields["output_json_path"] = options.output_json_path;
        std::ofstream output(options.output_json_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            result.ok = false;
            result.exit_code = 500;
            result.fields["error"] = "failed to open output_json_path for writing";
            result.fields["status"] = "failed";
            result.fields["preflight_reason_code"] = "output_json_write_failed";
            result.fields["summary"] = "DFG construction failed while writing output_json_path";
            return result;
        }
        output << dfg_json;
        output.close();
        result.fields["output_json_written"] = output.good() ? "true" : "false";
        if (!output.good()) {
            result.ok = false;
            result.exit_code = 500;
            result.fields["error"] = "failed to write dfg_json to output_json_path";
            result.fields["status"] = "failed";
            result.fields["preflight_reason_code"] = "output_json_write_failed";
            result.fields["summary"] = "DFG construction failed while writing output_json_path";
            return result;
        }
    }

    std::ostringstream summary;
    summary << "DFG built: "
            << node_list.size() << " nodes, "
            << edges.size() << " edges, "
            << definition_count << " definitions, "
            << use_count << " uses";
    result.fields["summary"] = summary.str();

    if (!output_dir.empty()) {
        const std::filesystem::path bundle_dir_path(output_dir);
        const std::string summary_json =
            "{\n"
            "  \"tool\": \"clang_dfg_builder\",\n"
            "  \"source_file\": \"" + EscapeJsonValue(options.source_file) + "\",\n"
            "  \"artifact_dfg_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "dfg.json").string()) + "\",\n"
            "  \"artifact_dfg_dot_path\": \"" + EscapeJsonValue((bundle_dir_path / "dfg.dot").string()) + "\",\n"
            "  \"artifact_summary_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "summary.json").string()) + "\",\n"
            "  \"summary\": \"" + EscapeJsonValue(result.fields["summary"]) + "\",\n"
            "  \"node_count\": " + result.fields["node_count"] + ",\n"
            "  \"edge_count\": " + result.fields["edge_count"] + ",\n"
            "  \"original_node_count\": " + result.fields["original_node_count"] + ",\n"
            "  \"original_edge_count\": " + result.fields["original_edge_count"] + ",\n"
            "  \"matched_node_count\": " + result.fields["matched_node_count"] + ",\n"
            "  \"matched_edge_count\": " + result.fields["matched_edge_count"] + ",\n"
            "  \"graph_has_more\": " + result.fields["graph_has_more"] + ",\n"
            "  \"next_offset_edges\": " + result.fields["next_offset_edges"] + ",\n"
            "  \"analysis_level\": \"" + EscapeJsonValue(result.fields["analysis_level"]) + "\",\n"
            "  \"dfg_precision\": \"" + EscapeJsonValue(result.fields["dfg_precision"]) + "\",\n"
            "  \"path_condition_candidate_count\": " + result.fields["path_condition_candidate_count"] + ",\n"
            "  \"interprocedural_call_ref_count\": " + result.fields["interprocedural_call_ref_count"] + "\n"
            "}";
        if (!WriteArtifactBundleAndRecordPaths(
                output_dir,
                {
                    {"dfg.json", dfg_json},
                    {"dfg.dot", dfg_dot},
                    {"summary.json", summary_json}
                },
                {
                    {"artifact_dfg_json_path", "dfg.json"},
                    {"artifact_dfg_dot_path", "dfg.dot"},
                    {"artifact_summary_json_path", "summary.json"}
                },
                "DFG construction failed while creating output_dir",
                "DFG construction failed while writing output_dir artifacts",
                &result)) {
            return result;
        }
    }
    result.fields["next_action"] = "Use dfg_json for variable dependency traversal. Upgrade to AST statement-level def/use when deeper precision is needed.";
    return result;
}

CommandResult BuildQueryCallGraphArtifactResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    (void)config;
    CommandResult result;

    const std::string artifact_summary_path = params.GetString("artifact_summary_path");
    std::string artifact_json_path_source;
    std::string artifact_json_path_resolution_detail;
    const std::string artifact_json_path =
        ResolveArtifactJsonPathFromSummary(
            params.GetString("artifact_json_path"),
            artifact_summary_path,
            "call_graph.json",
            "artifact_call_graph_json_path",
            &artifact_json_path_source,
            &artifact_json_path_resolution_detail);
    const std::string output_dir = params.GetString("output_dir");
    const bool include_dot = params.GetBool("include_dot", true);
    const std::string focus_symbol = params.GetString("focus_symbol");
    int neighborhood_depth = params.GetInt("neighborhood_depth", 1);
    int max_nodes = params.GetInt("max_nodes", 0);
    int offset_edges = params.GetInt("offset_edges", 0);
    int max_edges = params.GetInt("max_edges", 0);
    if (neighborhood_depth < 0) neighborhood_depth = 0;
    if (neighborhood_depth > 16) neighborhood_depth = 16;
    if (max_nodes < 0) max_nodes = 0;
    if (offset_edges < 0) offset_edges = 0;
    if (max_edges < 0) max_edges = 0;
    std::string neighborhood_direction = params.GetString("neighborhood_direction", "both");
    if (neighborhood_direction != "incoming" &&
        neighborhood_direction != "outgoing" &&
        neighborhood_direction != "both") {
        neighborhood_direction = "both";
    }

    if (artifact_json_path.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["status"] = "blocked";
        result.fields["error"] = "artifact_json_path or artifact_summary_path is required for Call Graph artifact query";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "missing_artifact_json_path";
        result.fields["summary"] = "Call Graph artifact query blocked: missing artifact_json_path/artifact_summary_path";
        return result;
    }

    const std::string artifact_json = ReadWholeFileText(artifact_json_path);
    if (artifact_json.empty()) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["status"] = "failed";
        result.fields["error"] = "failed to read artifact_json_path";
        result.fields["preflight_status"] = "failed";
        result.fields["preflight_reason_code"] = "artifact_read_failed";
        result.fields["summary"] = "Call Graph artifact query failed while reading artifact_json_path";
        return result;
    }

    std::vector<std::string> nodes;
    std::vector<CallGraphEdgeInfo> edges;
    if (!ParseCallGraphArtifactJson(artifact_json, &nodes, &edges)) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["status"] = "failed";
        result.fields["error"] = "failed to parse call graph artifact JSON";
        result.fields["preflight_status"] = "failed";
        result.fields["preflight_reason_code"] = "artifact_parse_failed";
        result.fields["summary"] = "Call Graph artifact query failed while parsing artifact_json_path";
        return result;
    }

    const std::size_t original_node_count = nodes.size();
    const std::size_t original_edge_count = edges.size();
    const bool graph_filtered =
        ApplyCallGraphNeighborhood(
            focus_symbol,
            neighborhood_depth,
            neighborhood_direction,
            &nodes,
            &edges);
    const std::size_t matched_node_count = nodes.size();
    const std::size_t matched_edge_count = edges.size();
    const PageWindowInfo graph_page =
        ComputePageWindowInfo(static_cast<int>(matched_edge_count), offset_edges, max_edges);
    const bool graph_truncated =
        ApplyCallGraphLimits(max_nodes, offset_edges, max_edges, &nodes, &edges);

    const std::string call_graph_json = SerializeCallGraphJson(nodes, edges);
    const std::string call_graph_dot = SerializeCallGraphDot(nodes, edges);

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "success";
    result.fields["result"] = "call_graph_artifact_query_success";
    result.fields["tool"] = "clang_call_graph_artifact_query";
    SetArtifactResolutionFields(
        &result,
        artifact_json_path,
        artifact_summary_path,
        artifact_json_path_source,
        artifact_json_path_resolution_detail);
    result.fields["node_count"] = std::to_string(nodes.size());
    result.fields["edge_count"] = std::to_string(edges.size());
    result.fields["original_node_count"] = std::to_string(original_node_count);
    result.fields["original_edge_count"] = std::to_string(original_edge_count);
    result.fields["matched_node_count"] = std::to_string(matched_node_count);
    result.fields["matched_edge_count"] = std::to_string(matched_edge_count);
    result.fields["graph_filtered"] = graph_filtered ? "true" : "false";
    result.fields["focus_symbol"] = focus_symbol;
    result.fields["neighborhood_depth"] = std::to_string(neighborhood_depth);
    result.fields["neighborhood_direction"] = neighborhood_direction;
    SetGraphPaginationFields(&result, graph_truncated, max_nodes, offset_edges, max_edges, graph_page);
    result.fields["call_graph_json"] = call_graph_json;
    result.fields["include_dot"] = include_dot ? "true" : "false";
    if (include_dot) {
        result.fields["call_graph_dot"] = call_graph_dot;
    }

    std::ostringstream summary;
    summary << "Call Graph artifact queried: "
            << nodes.size() << " nodes, "
            << edges.size() << " edges";
    result.fields["summary"] = summary.str();

    if (!output_dir.empty()) {
        const std::filesystem::path bundle_dir_path(output_dir);
        const std::string summary_json =
            "{\n"
            "  \"tool\": \"clang_call_graph_artifact_query\",\n"
            "  \"artifact_json_path\": \"" + EscapeJsonValue(artifact_json_path) + "\",\n"
            "  \"artifact_call_graph_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "call_graph.json").string()) + "\",\n"
            "  \"artifact_call_graph_dot_path\": \"" + EscapeJsonValue((bundle_dir_path / "call_graph.dot").string()) + "\",\n"
            "  \"artifact_summary_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "summary.json").string()) + "\",\n"
            "  \"summary\": \"" + EscapeJsonValue(result.fields["summary"]) + "\",\n"
            "  \"node_count\": " + result.fields["node_count"] + ",\n"
            "  \"edge_count\": " + result.fields["edge_count"] + ",\n"
            "  \"matched_node_count\": " + result.fields["matched_node_count"] + ",\n"
            "  \"matched_edge_count\": " + result.fields["matched_edge_count"] + "\n"
            "}";
        if (!WriteArtifactBundleAndRecordPaths(
                output_dir,
                {
                    {"call_graph.json", call_graph_json},
                    {"call_graph.dot", call_graph_dot},
                    {"summary.json", summary_json}
                },
                {
                    {"artifact_call_graph_json_path", "call_graph.json"},
                    {"artifact_call_graph_dot_path", "call_graph.dot"},
                    {"artifact_summary_json_path", "summary.json"}
                },
                "Call Graph artifact query failed while creating output_dir",
                "Call Graph artifact query failed while writing output_dir artifacts",
                &result)) {
            return result;
        }
    }
    result.fields["next_action"] = "Use call_graph_json or artifact paths for local dependency neighborhood inspection.";
    return result;
}

CommandResult BuildQueryDfgArtifactResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    (void)config;
    CommandResult result;

    const std::string artifact_summary_path = params.GetString("artifact_summary_path");
    std::string artifact_json_path_source;
    std::string artifact_json_path_resolution_detail;
    const std::string artifact_json_path =
        ResolveArtifactJsonPathFromSummary(
            params.GetString("artifact_json_path"),
            artifact_summary_path,
            "dfg.json",
            "artifact_dfg_json_path",
            &artifact_json_path_source,
            &artifact_json_path_resolution_detail);
    const std::string output_dir = params.GetString("output_dir");
    const bool include_dot = params.GetBool("include_dot", true);
    const std::string focus_symbol = params.GetString("focus_symbol");
    int neighborhood_depth = params.GetInt("neighborhood_depth", 1);
    int max_nodes = params.GetInt("max_nodes", 0);
    int offset_edges = params.GetInt("offset_edges", 0);
    int max_edges = params.GetInt("max_edges", 0);
    if (neighborhood_depth < 0) neighborhood_depth = 0;
    if (neighborhood_depth > 16) neighborhood_depth = 16;
    if (max_nodes < 0) max_nodes = 0;
    if (offset_edges < 0) offset_edges = 0;
    if (max_edges < 0) max_edges = 0;
    std::string neighborhood_direction = params.GetString("neighborhood_direction", "both");
    if (neighborhood_direction != "incoming" &&
        neighborhood_direction != "outgoing" &&
        neighborhood_direction != "both") {
        neighborhood_direction = "both";
    }

    if (artifact_json_path.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["status"] = "blocked";
        result.fields["error"] = "artifact_json_path or artifact_summary_path is required for DFG artifact query";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "missing_artifact_json_path";
        result.fields["summary"] = "DFG artifact query blocked: missing artifact_json_path/artifact_summary_path";
        return result;
    }

    const std::string artifact_json = ReadWholeFileText(artifact_json_path);
    if (artifact_json.empty()) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["status"] = "failed";
        result.fields["error"] = "failed to read artifact_json_path";
        result.fields["preflight_status"] = "failed";
        result.fields["preflight_reason_code"] = "artifact_read_failed";
        result.fields["summary"] = "DFG artifact query failed while reading artifact_json_path";
        return result;
    }

    std::vector<std::string> nodes;
    std::vector<DfgEdgeInfo> edges;
    if (!ParseDfgArtifactJson(artifact_json, &nodes, &edges)) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["status"] = "failed";
        result.fields["error"] = "failed to parse DFG artifact JSON";
        result.fields["preflight_status"] = "failed";
        result.fields["preflight_reason_code"] = "artifact_parse_failed";
        result.fields["summary"] = "DFG artifact query failed while parsing artifact_json_path";
        return result;
    }

    const std::size_t original_node_count = nodes.size();
    const std::size_t original_edge_count = edges.size();
    const bool graph_filtered =
        ApplyDfgNeighborhood(
            focus_symbol,
            neighborhood_depth,
            neighborhood_direction,
            &nodes,
            &edges);
    const std::size_t matched_node_count = nodes.size();
    const std::size_t matched_edge_count = edges.size();
    const PageWindowInfo graph_page =
        ComputePageWindowInfo(static_cast<int>(matched_edge_count), offset_edges, max_edges);
    const bool graph_truncated =
        ApplyDfgLimits(max_nodes, offset_edges, max_edges, &nodes, &edges);

    const std::string dfg_json = SerializeDfgJson(nodes, edges);
    const std::string dfg_dot = SerializeDfgDot(nodes, edges);

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "success";
    result.fields["result"] = "dfg_artifact_query_success";
    result.fields["tool"] = "clang_dfg_artifact_query";
    SetArtifactResolutionFields(
        &result,
        artifact_json_path,
        artifact_summary_path,
        artifact_json_path_source,
        artifact_json_path_resolution_detail);
    result.fields["node_count"] = std::to_string(nodes.size());
    result.fields["edge_count"] = std::to_string(edges.size());
    result.fields["original_node_count"] = std::to_string(original_node_count);
    result.fields["original_edge_count"] = std::to_string(original_edge_count);
    result.fields["matched_node_count"] = std::to_string(matched_node_count);
    result.fields["matched_edge_count"] = std::to_string(matched_edge_count);
    result.fields["graph_filtered"] = graph_filtered ? "true" : "false";
    result.fields["focus_symbol"] = focus_symbol;
    result.fields["neighborhood_depth"] = std::to_string(neighborhood_depth);
    result.fields["neighborhood_direction"] = neighborhood_direction;
    SetGraphPaginationFields(&result, graph_truncated, max_nodes, offset_edges, max_edges, graph_page);
    result.fields["dfg_json"] = dfg_json;
    result.fields["include_dot"] = include_dot ? "true" : "false";
    if (include_dot) {
        result.fields["dfg_dot"] = dfg_dot;
    }

    std::ostringstream summary;
    summary << "DFG artifact queried: "
            << nodes.size() << " nodes, "
            << edges.size() << " edges";
    result.fields["summary"] = summary.str();

    if (!output_dir.empty()) {
        const std::filesystem::path bundle_dir_path(output_dir);
        const std::string summary_json =
            "{\n"
            "  \"tool\": \"clang_dfg_artifact_query\",\n"
            "  \"artifact_json_path\": \"" + EscapeJsonValue(artifact_json_path) + "\",\n"
            "  \"artifact_dfg_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "dfg.json").string()) + "\",\n"
            "  \"artifact_dfg_dot_path\": \"" + EscapeJsonValue((bundle_dir_path / "dfg.dot").string()) + "\",\n"
            "  \"artifact_summary_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "summary.json").string()) + "\",\n"
            "  \"summary\": \"" + EscapeJsonValue(result.fields["summary"]) + "\",\n"
            "  \"node_count\": " + result.fields["node_count"] + ",\n"
            "  \"edge_count\": " + result.fields["edge_count"] + ",\n"
            "  \"matched_node_count\": " + result.fields["matched_node_count"] + ",\n"
            "  \"matched_edge_count\": " + result.fields["matched_edge_count"] + "\n"
            "}";
        if (!WriteArtifactBundleAndRecordPaths(
                output_dir,
                {
                    {"dfg.json", dfg_json},
                    {"dfg.dot", dfg_dot},
                    {"summary.json", summary_json}
                },
                {
                    {"artifact_dfg_json_path", "dfg.json"},
                    {"artifact_dfg_dot_path", "dfg.dot"},
                    {"artifact_summary_json_path", "summary.json"}
                },
                "DFG artifact query failed while creating output_dir",
                "DFG artifact query failed while writing output_dir artifacts",
                &result)) {
            return result;
        }
    }
    result.fields["next_action"] = "Use dfg_json or artifact paths for local variable dependency neighborhood inspection.";
    return result;
}

CommandResult BuildRunProgramSliceResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    using namespace ::codex_lan_agent;
    (void)config;

    CommandResult result;

    ClangIndexerOptions options;
    options.source_file = params.GetString("source_file");
    options.compile_db_dir = params.GetString("compile_db_dir");
    options.compilation_database_path = params.GetString("compilation_database_path");
    options.output_json_path = params.GetString("output_json_path");
    options.project_root = params.GetString("project_root");
    options.verbose = params.GetBool("verbose", false);
    const std::string output_dir = params.GetString("output_dir");
    const bool include_dot = params.GetBool("include_dot", true);
    const bool include_path_metadata = params.GetBool("include_path_metadata", false);
    int max_nodes = params.GetInt("max_nodes", 0);
    int offset_edges = params.GetInt("offset_edges", 0);
    int max_edges = params.GetInt("max_edges", 0);
    if (max_nodes < 0) max_nodes = 0;
    if (offset_edges < 0) offset_edges = 0;
    if (max_edges < 0) max_edges = 0;

    AppendStringListFromJsonish(
        params.GetString("target_namespaces"),
        &options.target_namespaces);
    AppendStringListFromJsonish(
        params.GetString("extra_include_dirs"),
        &options.extra_include_dirs);
    AppendStringListFromJsonish(
        params.GetString("extra_defines"),
        &options.extra_defines);

    const std::string symbol = params.GetString("symbol");
    std::string direction = params.GetString("direction", "backward");
    if (direction != "forward" && direction != "backward") {
        direction = "backward";
    }
    int max_depth = params.GetInt("max_depth", 6);
    if (max_depth < 1) {
        max_depth = 1;
    }
    if (max_depth > 32) {
        max_depth = 32;
    }

    if (options.source_file.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "source_file is required for Program Slicing";
        result.fields["status"] = "blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "missing_source_file";
        result.fields["summary"] = "Program Slicing blocked: missing source_file";
        result.fields["next_action"] = "provide source_file path to analyze";
        return result;
    }
    if (symbol.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "symbol is required for Program Slicing";
        result.fields["status"] = "blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "missing_symbol";
        result.fields["summary"] = "Program Slicing blocked: missing symbol";
        result.fields["next_action"] = "provide symbol, for example result or modelcenter";
        return result;
    }

    result.fields["source_file"] = options.source_file;
    result.fields["symbol"] = symbol;
    result.fields["direction"] = direction;
    result.fields["max_depth"] = std::to_string(max_depth);
    result.fields["slice_scope"] = "source_file_lexical_dfg";
    result.fields["filtered_to_source_file"] = "true";

    std::string resolved_compile_db_dir;
    std::string resolved_compile_db_file;
    std::string compile_db_error;
    if (!ResolveCompilationDatabaseLocation(
            options,
            &resolved_compile_db_dir,
            &resolved_compile_db_file,
            &compile_db_error)) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = compile_db_error;
        result.fields["status"] = "blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "invalid_compilation_database";
        result.fields["summary"] = "Program Slicing blocked: invalid compilation database input";
        result.fields["next_action"] =
            "provide compile_db_dir pointing to a directory containing compile_commands.json, or compilation_database_path pointing to the compile_commands.json file";
        return result;
    }
    if (!resolved_compile_db_dir.empty()) {
        options.compile_db_dir = resolved_compile_db_dir;
        options.compilation_database_path = resolved_compile_db_file;
        result.fields["resolved_compile_db_dir"] = resolved_compile_db_dir;
        result.fields["resolved_compilation_database_path"] = resolved_compile_db_file;
        result.fields["compile_db_mode"] = "compile_commands_json";
    } else {
        result.fields["compile_db_mode"] = "fallback_arguments";
    }

    std::string fallback_block_reason;
    std::string fallback_block_detail;
    if (IsLikelyComplexCppTranslationUnit(
            options,
            &fallback_block_reason,
            &fallback_block_detail)) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = fallback_block_detail;
        result.fields["status"] = "blocked";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = fallback_block_reason;
        result.fields["summary"] = "Program Slicing blocked: compile_commands.json is required for this translation unit";
        result.fields["next_action"] =
            "generate and pass compile_db_dir/compilation_database_path, or provide extra_include_dirs for all project and third-party headers";
        return result;
    }

    ClangAstParseResult ast_result = RunClangAstParser(options);
    if (!ast_result.success) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["error"] = ast_result.error.empty()
            ? "Clang AST parsing failed"
            : ast_result.error;
        result.fields["status"] = "failed";
        result.fields["preflight_status"] = "failed";
        result.fields["preflight_reason_code"] = "clang_ast_parse_failed";
        result.fields["summary"] = "Program Slicing failed while parsing AST";
        result.fields["error_detail"] = ast_result.error;
        return result;
    }

    const std::string source_text = ReadWholeFileText(options.source_file);
    if (source_text.empty()) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["error"] = "failed to read source_file for Program Slicing";
        result.fields["status"] = "failed";
        result.fields["preflight_status"] = "failed";
        result.fields["preflight_reason_code"] = "source_read_failed";
        result.fields["summary"] = "Program Slicing failed while reading source_file";
        return result;
    }

    std::set<std::string> all_nodes;
    std::map<std::string, DfgEdgeInfo> all_edge_map;
    int definition_count = 0;
    int use_count = 0;
    const bool used_ast_statement_slice =
        BuildAstStatementDfg(
            ast_result,
            options.source_file,
            &all_nodes,
            &all_edge_map,
            &definition_count,
            &use_count);

    if (!used_ast_statement_slice) {
        int line_number = 0;
        std::istringstream lines(source_text);
        std::string raw_line;
        while (std::getline(lines, raw_line)) {
            ++line_number;
            std::string line = StripLineComment(raw_line);
            if (line.find("#include") != std::string::npos ||
                line.find("#define") != std::string::npos) {
                continue;
            }

            const std::size_t assign_pos = FindAssignmentOperator(line);
            if (assign_pos == std::string::npos) {
                continue;
            }

            const std::string def = LastDfgIdentifier(line.substr(0, assign_pos));
            if (def.empty()) {
                continue;
            }

            all_nodes.insert(def);
            ++definition_count;

            const std::vector<std::string> uses = ExtractDfgIdentifiers(line.substr(assign_pos + 1));
            for (const auto & use : uses) {
                if (use == def) {
                    continue;
                }
                all_nodes.insert(use);
                ++use_count;
                AddDfgEdge(use, def, "def_use", line_number, &all_edge_map);
            }
        }
    }

    std::vector<DfgEdgeInfo> all_edges;
    all_edges.reserve(all_edge_map.size());
    for (const auto & item : all_edge_map) {
        all_edges.push_back(item.second);
    }

    std::set<std::string> slice_nodes;
    std::set<int> slice_lines;
    std::map<std::string, DfgEdgeInfo> slice_edge_map;
    std::vector<std::pair<std::string, int>> worklist;
    slice_nodes.insert(symbol);
    worklist.push_back({symbol, 0});

    for (std::size_t cursor = 0; cursor < worklist.size(); ++cursor) {
        const std::string current = worklist[cursor].first;
        const int depth = worklist[cursor].second;
        if (depth >= max_depth) {
            continue;
        }

        for (const auto & edge : all_edges) {
            const bool follows = direction == "backward"
                ? edge.target == current
                : edge.source == current;
            if (!follows) {
                continue;
            }

            const std::string next = direction == "backward" ? edge.source : edge.target;
            const std::string key = edge.source + "\n" + edge.target + "\n" + edge.kind;
            slice_edge_map[key] = edge;
            if (edge.first_source_line > 0) {
                slice_lines.insert(edge.first_source_line);
            }
            if (slice_nodes.insert(next).second) {
                worklist.push_back({next, depth + 1});
            }
        }
    }

    std::vector<std::string> slice_node_list(slice_nodes.begin(), slice_nodes.end());
    std::vector<DfgEdgeInfo> slice_edges;
    slice_edges.reserve(slice_edge_map.size());
    for (const auto & item : slice_edge_map) {
        slice_edges.push_back(item.second);
    }
    const std::size_t original_node_count = slice_node_list.size();
    const std::size_t original_edge_count = slice_edges.size();
    const PageWindowInfo graph_page =
        ComputePageWindowInfo(static_cast<int>(original_edge_count), offset_edges, max_edges);
    const bool graph_truncated =
        ApplyDfgLimits(max_nodes, offset_edges, max_edges, &slice_node_list, &slice_edges);

    slice_lines.clear();
    for (const auto & edge : slice_edges) {
        if (edge.first_source_line > 0) {
            slice_lines.insert(edge.first_source_line);
        }
    }
    std::vector<int> slice_line_list(slice_lines.begin(), slice_lines.end());
    const std::vector<std::string> source_lines = SplitSourceLines(source_text);
    const std::vector<ClangMethodInfo> ast_functions = FlattenAstFunctions(ast_result);
    const std::set<std::string> slice_ast_functions =
        CollectDfgEdgeAstFunctions(slice_edges, ast_functions, options.source_file);
    CfgBuildResult slice_cfg_result;
    CfgPathSensitivityInfo slice_path_info;
    if (include_path_metadata) {
        slice_cfg_result = RunCfgBuilder(options);
        if (slice_cfg_result.success) {
            slice_path_info = ComputeCfgPathSensitivityInfo(slice_cfg_result, slice_ast_functions);
        }
    }
    const std::set<int> slice_line_set(slice_line_list.begin(), slice_line_list.end());
    const int slice_call_ref_count =
        CountSourceScopedCallRefs(ast_result, options.source_file, &slice_line_set);

    const std::string slice_json =
        SerializeProgramSliceJson(symbol, direction, slice_node_list, slice_edges, slice_line_list);
    const std::string slice_dot = SerializeProgramSliceDot(slice_node_list, slice_edges);
    const std::string source_lines_json =
        SerializeSourceLinesJson(slice_line_list, source_lines);

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "success";
    result.fields["result"] = "program_slice_success";
    result.fields["tool"] = "clang_program_slicer";
    result.fields["node_count"] = std::to_string(slice_node_list.size());
    result.fields["edge_count"] = std::to_string(slice_edges.size());
    result.fields["original_node_count"] = std::to_string(original_node_count);
    result.fields["original_edge_count"] = std::to_string(original_edge_count);
    result.fields["matched_node_count"] = std::to_string(original_node_count);
    result.fields["matched_edge_count"] = std::to_string(original_edge_count);
    SetGraphPaginationFields(&result, graph_truncated, max_nodes, offset_edges, max_edges, graph_page);
    result.fields["slice_line_count"] = std::to_string(slice_line_list.size());
    result.fields["definition_count"] = std::to_string(definition_count);
    result.fields["use_count"] = std::to_string(use_count);
    result.fields["analysis_level"] = used_ast_statement_slice ? "ast_statement_v1" : "hybrid_v1";
    result.fields["slice_precision"] = used_ast_statement_slice ? "ast_statement_def_use_cfg_callgraph_v1" : "lexical_v1_ast_cfg_callgraph_anchored";
    result.fields["ast_statement_level_status"] = used_ast_statement_slice ? "ast_statement_refs_available" : "ast_ready_function_scope";
    result.fields["ast_data_flow_ref_count"] = std::to_string(ast_result.data_flow_refs.size());
    result.fields["ast_anchor_function_count"] = std::to_string(slice_ast_functions.size());
    result.fields["ast_source_scoped_call_ref_count"] =
        std::to_string(CountSourceScopedCallRefs(ast_result, options.source_file, nullptr));
    result.fields["include_path_metadata"] = include_path_metadata ? "true" : "false";
    result.fields["path_sensitive_status"] = include_path_metadata
        ? (slice_cfg_result.success ? "cfg_branch_metadata_available" : "cfg_branch_metadata_unavailable")
        : "cfg_branch_metadata_deferred";
    result.fields["path_condition_candidate_count"] = std::to_string(slice_path_info.branch_count);
    result.fields["control_dependency_candidate_count"] = std::to_string(slice_path_info.branch_count);
    result.fields["cyclic_function_candidate_count"] = std::to_string(slice_path_info.cyclic_function_count);
    result.fields["interprocedural_status"] = "call_graph_metadata_available";
    result.fields["interprocedural_call_ref_count"] = std::to_string(slice_call_ref_count);
    result.fields["preflight_status"] = "ready";
    result.fields["slice_json"] = slice_json;
    result.fields["include_dot"] = include_dot ? "true" : "false";
    if (include_dot) {
        result.fields["slice_dot"] = slice_dot;
    }
    result.fields["source_lines_json"] = source_lines_json;

    std::ostringstream summary;
    summary << "Program Slice built: symbol="
            << symbol << ", direction=" << direction << ", "
            << slice_node_list.size() << " nodes, "
            << slice_edges.size() << " edges, "
            << slice_line_list.size() << " source lines";
    result.fields["summary"] = summary.str();

    if (!options.output_json_path.empty()) {
        result.fields["output_json_path"] = options.output_json_path;
        std::ofstream output(options.output_json_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            result.ok = false;
            result.exit_code = 500;
            result.fields["error"] = "failed to open output_json_path for writing";
            result.fields["status"] = "failed";
            result.fields["preflight_reason_code"] = "output_json_write_failed";
            result.fields["summary"] = "Program Slicing failed while writing output_json_path";
            return result;
        }
        output << slice_json;
        output.close();
        result.fields["output_json_written"] = output.good() ? "true" : "false";
        if (!output.good()) {
            result.ok = false;
            result.exit_code = 500;
            result.fields["error"] = "failed to write slice_json to output_json_path";
            result.fields["status"] = "failed";
            result.fields["preflight_reason_code"] = "output_json_write_failed";
            result.fields["summary"] = "Program Slicing failed while writing output_json_path";
            return result;
        }
    }

    if (!output_dir.empty()) {
        const std::filesystem::path bundle_dir_path(output_dir);
        const std::string summary_json =
            "{\n"
            "  \"tool\": \"clang_program_slicer\",\n"
            "  \"source_file\": \"" + EscapeJsonValue(options.source_file) + "\",\n"
            "  \"artifact_slice_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "slice.json").string()) + "\",\n"
            "  \"artifact_slice_dot_path\": \"" + EscapeJsonValue((bundle_dir_path / "slice.dot").string()) + "\",\n"
            "  \"artifact_source_lines_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "source_lines.json").string()) + "\",\n"
            "  \"artifact_summary_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "summary.json").string()) + "\",\n"
            "  \"symbol\": \"" + EscapeJsonValue(symbol) + "\",\n"
            "  \"direction\": \"" + EscapeJsonValue(direction) + "\",\n"
            "  \"summary\": \"" + EscapeJsonValue(result.fields["summary"]) + "\",\n"
            "  \"node_count\": " + result.fields["node_count"] + ",\n"
            "  \"edge_count\": " + result.fields["edge_count"] + ",\n"
            "  \"slice_line_count\": " + result.fields["slice_line_count"] + ",\n"
            "  \"analysis_level\": \"" + EscapeJsonValue(result.fields["analysis_level"]) + "\",\n"
            "  \"slice_precision\": \"" + EscapeJsonValue(result.fields["slice_precision"]) + "\",\n"
            "  \"path_condition_candidate_count\": " + result.fields["path_condition_candidate_count"] + ",\n"
            "  \"interprocedural_call_ref_count\": " + result.fields["interprocedural_call_ref_count"] + "\n"
            "}";
        if (!WriteArtifactBundleAndRecordPaths(
                output_dir,
                {
                    {"slice.json", slice_json},
                    {"slice.dot", slice_dot},
                    {"source_lines.json", source_lines_json},
                    {"summary.json", summary_json}
                },
                {
                    {"artifact_slice_json_path", "slice.json"},
                    {"artifact_slice_dot_path", "slice.dot"},
                    {"artifact_source_lines_json_path", "source_lines.json"},
                    {"artifact_summary_json_path", "summary.json"}
                },
                "Program Slicing failed while creating output_dir",
                "Program Slicing failed while writing output_dir artifacts",
                &result)) {
            return result;
        }
    }

    result.fields["next_action"] =
        "Use slice_json to inspect the variables and source lines in the lexical dependency slice. Upgrade to AST statement-level slicing for path-sensitive precision.";
    return result;
}

CommandResult BuildQueryProgramSliceArtifactResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    (void)config;
    CommandResult result;

    const std::string artifact_summary_path = params.GetString("artifact_summary_path");
    std::string artifact_json_path_source;
    std::string artifact_json_path_resolution_detail;
    const std::string artifact_json_path =
        ResolveArtifactJsonPathFromSummary(
            params.GetString("artifact_json_path"),
            artifact_summary_path,
            "slice.json",
            "artifact_slice_json_path",
            &artifact_json_path_source,
            &artifact_json_path_resolution_detail);

    std::string source_lines_path_source;
    std::string source_lines_path_resolution_detail;
    const std::string source_lines_path =
        ResolveArtifactJsonPathFromSummary(
            params.GetString("artifact_source_lines_json_path"),
            artifact_summary_path,
            "source_lines.json",
            "artifact_source_lines_json_path",
            &source_lines_path_source,
            &source_lines_path_resolution_detail);

    const std::string output_dir = params.GetString("output_dir");
    const bool include_dot = params.GetBool("include_dot", true);
    int max_nodes = params.GetInt("max_nodes", 0);
    int offset_edges = params.GetInt("offset_edges", 0);
    int max_edges = params.GetInt("max_edges", 0);
    if (max_nodes < 0) max_nodes = 0;
    if (offset_edges < 0) offset_edges = 0;
    if (max_edges < 0) max_edges = 0;

    if (artifact_json_path.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["status"] = "blocked";
        result.fields["error"] = "artifact_json_path or artifact_summary_path is required for Program Slice artifact query";
        result.fields["preflight_status"] = "blocked";
        result.fields["preflight_reason_code"] = "missing_artifact_json_path";
        result.fields["summary"] = "Program Slice artifact query blocked: missing artifact_json_path/artifact_summary_path";
        return result;
    }

    const std::string artifact_json = ReadWholeFileText(artifact_json_path);
    if (artifact_json.empty()) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["status"] = "failed";
        result.fields["error"] = "failed to read artifact_json_path";
        result.fields["preflight_status"] = "failed";
        result.fields["preflight_reason_code"] = "artifact_read_failed";
        result.fields["summary"] = "Program Slice artifact query failed while reading artifact_json_path";
        return result;
    }

    std::string symbol;
    std::string direction;
    ExtractJsonStringField(artifact_json, "symbol", &symbol);
    ExtractJsonStringField(artifact_json, "direction", &direction);

    std::vector<std::string> nodes;
    std::vector<DfgEdgeInfo> edges;
    if (!ParseDfgArtifactJson(artifact_json, &nodes, &edges)) {
        result.ok = false;
        result.exit_code = 500;
        result.fields["status"] = "failed";
        result.fields["error"] = "failed to parse Program Slice artifact JSON";
        result.fields["preflight_status"] = "failed";
        result.fields["preflight_reason_code"] = "artifact_parse_failed";
        result.fields["summary"] = "Program Slice artifact query failed while parsing artifact_json_path";
        return result;
    }

    std::string lines_array;
    std::vector<int> slice_lines;
    if (ExtractJsonArrayRaw(artifact_json, "lines", &lines_array)) {
        slice_lines = ParseJsonIntArray(lines_array);
    }

    const std::size_t original_node_count = nodes.size();
    const std::size_t original_edge_count = edges.size();
    const std::size_t matched_node_count = nodes.size();
    const std::size_t matched_edge_count = edges.size();
    const PageWindowInfo graph_page =
        ComputePageWindowInfo(static_cast<int>(matched_edge_count), offset_edges, max_edges);
    const bool graph_truncated =
        ApplyDfgLimits(max_nodes, offset_edges, max_edges, &nodes, &edges);

    std::set<int> returned_lines;
    for (const auto & edge : edges) {
        if (edge.first_source_line > 0) {
            returned_lines.insert(edge.first_source_line);
        }
    }
    if (!returned_lines.empty()) {
        slice_lines.assign(returned_lines.begin(), returned_lines.end());
    }

    const std::string slice_json =
        SerializeProgramSliceJson(symbol, direction, nodes, edges, slice_lines);
    const std::string slice_dot = SerializeProgramSliceDot(nodes, edges);
    const std::string source_lines_json = source_lines_path.empty()
        ? std::string()
        : ReadWholeFileText(source_lines_path);

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "success";
    result.fields["result"] = "program_slice_artifact_query_success";
    result.fields["tool"] = "clang_program_slice_artifact_query";
    SetArtifactResolutionFields(
        &result,
        artifact_json_path,
        artifact_summary_path,
        artifact_json_path_source,
        artifact_json_path_resolution_detail);
    result.fields["artifact_source_lines_json_path"] = source_lines_path;
    result.fields["artifact_source_lines_json_path_resolved_from"] = source_lines_path_source;
    result.fields["artifact_source_lines_json_path_resolution_detail"] = source_lines_path_resolution_detail;
    result.fields["symbol"] = symbol;
    result.fields["direction"] = direction;
    result.fields["node_count"] = std::to_string(nodes.size());
    result.fields["edge_count"] = std::to_string(edges.size());
    result.fields["original_node_count"] = std::to_string(original_node_count);
    result.fields["original_edge_count"] = std::to_string(original_edge_count);
    result.fields["matched_node_count"] = std::to_string(matched_node_count);
    result.fields["matched_edge_count"] = std::to_string(matched_edge_count);
    SetGraphPaginationFields(&result, graph_truncated, max_nodes, offset_edges, max_edges, graph_page);
    result.fields["slice_line_count"] = std::to_string(slice_lines.size());
    result.fields["slice_json"] = slice_json;
    result.fields["include_dot"] = include_dot ? "true" : "false";
    if (include_dot) {
        result.fields["slice_dot"] = slice_dot;
    }
    result.fields["source_lines_json"] = source_lines_json;
    result.fields["preflight_status"] = "ready";

    std::ostringstream summary;
    summary << "Program Slice artifact queried: symbol="
            << symbol << ", direction=" << direction << ", "
            << nodes.size() << " nodes, "
            << edges.size() << " edges, "
            << slice_lines.size() << " source lines";
    result.fields["summary"] = summary.str();

    if (!output_dir.empty()) {
        const std::filesystem::path bundle_dir_path(output_dir);
        const std::string summary_json =
            "{\n"
            "  \"tool\": \"clang_program_slice_artifact_query\",\n"
            "  \"artifact_json_path\": \"" + EscapeJsonValue(artifact_json_path) + "\",\n"
            "  \"artifact_slice_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "slice.json").string()) + "\",\n"
            "  \"artifact_slice_dot_path\": \"" + EscapeJsonValue((bundle_dir_path / "slice.dot").string()) + "\",\n"
            "  \"artifact_source_lines_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "source_lines.json").string()) + "\",\n"
            "  \"artifact_summary_json_path\": \"" + EscapeJsonValue((bundle_dir_path / "summary.json").string()) + "\",\n"
            "  \"symbol\": \"" + EscapeJsonValue(symbol) + "\",\n"
            "  \"direction\": \"" + EscapeJsonValue(direction) + "\",\n"
            "  \"summary\": \"" + EscapeJsonValue(result.fields["summary"]) + "\",\n"
            "  \"node_count\": " + result.fields["node_count"] + ",\n"
            "  \"edge_count\": " + result.fields["edge_count"] + ",\n"
            "  \"slice_line_count\": " + result.fields["slice_line_count"] + "\n"
            "}";
        if (!WriteArtifactBundleAndRecordPaths(
                output_dir,
                {
                    {"slice.json", slice_json},
                    {"slice.dot", slice_dot},
                    {"source_lines.json", source_lines_json},
                    {"summary.json", summary_json}
                },
                {
                    {"artifact_slice_json_path", "slice.json"},
                    {"artifact_slice_dot_path", "slice.dot"},
                    {"artifact_source_lines_json_path", "source_lines.json"},
                    {"artifact_summary_json_path", "summary.json"}
                },
                "Program Slice artifact query failed while creating output_dir",
                "Program Slice artifact query failed while writing output_dir artifacts",
                &result)) {
            return result;
        }
    }

    result.fields["next_action"] = "Use slice_json/source_lines_json for local dependency slice inspection.";
    return result;
}
}

CommandResult BuildRunClangAstParserResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    return ::codex_lan_agent::BuildRunClangAstParserResult(config, params);
}

CommandResult BuildRunCfgResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    return ::codex_lan_agent::BuildRunCfgResult(config, params);
}

CommandResult BuildQueryCfgArtifactResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    return ::codex_lan_agent::BuildQueryCfgArtifactResult(config, params);
}

CommandResult BuildRunCallGraphResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    return ::codex_lan_agent::BuildRunCallGraphResult(config, params);
}

CommandResult BuildRunDfgResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    return ::codex_lan_agent::BuildRunDfgResult(config, params);
}

CommandResult BuildQueryCallGraphArtifactResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    return ::codex_lan_agent::BuildQueryCallGraphArtifactResult(config, params);
}

CommandResult BuildQueryDfgArtifactResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    return ::codex_lan_agent::BuildQueryDfgArtifactResult(config, params);
}

CommandResult BuildRunProgramSliceResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    return ::codex_lan_agent::BuildRunProgramSliceResult(config, params);
}

CommandResult BuildQueryProgramSliceArtifactResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params)
{
    return ::codex_lan_agent::BuildQueryProgramSliceArtifactResult(config, params);
}
