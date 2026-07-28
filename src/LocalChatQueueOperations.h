#pragma once

#include "AgentConfig.h"
#include "DispatchEnhancementOperations.h"
#include "StructuredJsonOperations.h"

#include <array>
#include <map>
#include <set>

using codex_lan_agent::AgentConfig;

std::string Trim(const std::string & value);
std::string BuildLogPath(const AgentConfig & config, const std::string & prefix);
std::string DeriveLocalChatFallbackEndpoint(const AgentConfig & config);
bool ResolveReachableEndpoint(
    const std::string & primary_endpoint,
    const std::string & fallback_endpoint,
    int timeout_ms,
    std::string * resolved_endpoint,
    std::string * detail,
    std::string * source_label);
bool TryResolveAllowedPath(
    const AgentConfig & config,
    const std::string & raw_path,
    std::filesystem::path * normalized_path,
    std::string * error_message);
bool ReadWholeFile(
    const std::filesystem::path & path,
    std::string * content,
    std::string * error_message);
bool StartsWithPath(
    const std::filesystem::path & path,
    const std::filesystem::path & prefix);
template <typename... Args>
std::string FirstNonEmpty(const Args &... values);
std::string ExpectedMarkerForProfile(const std::string & profile_name);
CommandResult PrepareDirectoryAnalysisResult(
    const AgentConfig & config,
    const std::string & directory_path,
    const std::string & file_extensions_csv,
    int max_files,
    int max_excerpt_lines_per_file,
    int max_total_excerpt_lines,
    std::size_t max_excerpt_chars,
    const std::string & trace_id);

struct LocalChatEvidencePacket {
    std::string task_id;
    std::string result_ref;
    std::string evidence_ref;
    std::string resolved_log_path;
    std::string log_excerpt;
    std::string source_excerpt;

    bool empty() const {
        return task_id.empty()
            && result_ref.empty()
            && evidence_ref.empty()
            && resolved_log_path.empty()
            && log_excerpt.empty()
            && source_excerpt.empty();
    }
};

std::string TruncateLocalChatEvidenceText(const std::string & value, std::size_t max_chars) {
    if (value.size() <= max_chars) {
        return value;
    }
    return value.substr(0, max_chars) + "\n[truncated]";
}

std::string BuildLocalChatEvidenceInjectionText(const LocalChatEvidencePacket & evidence) {
    std::ostringstream packet;
    packet
        << "\n\nReview evidence packet (caller supplied; analysis-only; do not infer real execution beyond these refs):\n"
        << "task_id=" << evidence.task_id << "\n"
        << "result_ref=" << evidence.result_ref << "\n"
        << "evidence_ref=" << evidence.evidence_ref << "\n"
        << "resolved_log_path=" << evidence.resolved_log_path << "\n";
    if (!evidence.log_excerpt.empty()) {
        packet << "key_log_excerpt:\n"
               << TruncateLocalChatEvidenceText(evidence.log_excerpt, 4000)
               << "\n";
    }
    if (!evidence.source_excerpt.empty()) {
        packet << "key_source_excerpt:\n"
               << TruncateLocalChatEvidenceText(evidence.source_excerpt, 12000)
               << "\n";
    }
    packet
        << "Analysis rule: if evidence is insufficient, request the real MCP execution tool "
        << "or explicit refs; do not fabricate build logs, test logs, file paths, or executed changes.";
    return packet.str();
}

LocalChatEvidencePacket ExtractLocalChatEvidencePacket(const std::string & body) {
    LocalChatEvidencePacket evidence;
    evidence.task_id = ExtractJsonString(body, "task_id");
    evidence.result_ref = ExtractJsonString(body, "result_ref");
    evidence.evidence_ref = ExtractJsonString(body, "evidence_ref");
    evidence.resolved_log_path = ExtractJsonString(body, "resolved_log_path");
    evidence.log_excerpt = ExtractJsonString(body, "log_excerpt");
    if (evidence.log_excerpt.empty()) {
        evidence.log_excerpt = ExtractJsonString(body, "key_log_excerpt");
    }
    evidence.source_excerpt = ExtractJsonString(body, "source_excerpt");
    if (evidence.source_excerpt.empty()) {
        evidence.source_excerpt = ExtractJsonString(body, "key_source_excerpt");
    }
    return evidence;
}

bool LooksLikeDirectoryScope(
    const AgentConfig & config,
    const std::string & scope,
    std::filesystem::path * path) {
    if (scope.empty() || path == nullptr) {
        return false;
    }
    std::filesystem::path candidate(scope);
    if (candidate.is_relative()) {
        candidate = std::filesystem::path(config.workspace_root) / candidate;
    }
    std::error_code ec;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(candidate, ec);
    if (ec || !std::filesystem::is_directory(normalized, ec) || ec) {
        return false;
    }
    const std::filesystem::path workspace_root(config.workspace_root);
    if (!StartsWithPath(normalized, workspace_root)) {
        return false;
    }
    *path = normalized;
    return true;
}

std::string ExtractInlineContentBlock(const std::string & text) {
    const std::string begin_marker = "content_begin<<<";
    const std::string end_marker = ">>>content_end";
    const std::size_t begin = text.find(begin_marker);
    if (begin != std::string::npos) {
        const std::size_t content_begin = begin + begin_marker.size();
        const std::size_t end = text.find(end_marker, content_begin);
        if (end != std::string::npos) {
            std::size_t trimmed_begin = content_begin;
            while (trimmed_begin < end &&
                   (text[trimmed_begin] == '\r' || text[trimmed_begin] == '\n')) {
                ++trimmed_begin;
            }
            std::size_t trimmed_end = end;
            while (trimmed_end > trimmed_begin &&
                   (text[trimmed_end - 1] == '\r' || text[trimmed_end - 1] == '\n')) {
                --trimmed_end;
            }
            return text.substr(trimmed_begin, trimmed_end - trimmed_begin);
        }
    }

    const std::string content_key = "content_text=";
    const std::size_t content_pos = text.find(content_key);
    if (content_pos != std::string::npos) {
        const std::size_t value_begin = content_pos + content_key.size();
        const std::size_t value_end = text.find("\nnext_", value_begin);
        if (value_end != std::string::npos) {
            return text.substr(value_begin, value_end - value_begin);
        }
        return text.substr(value_begin);
    }

    return text;
}

std::string JoinLimitedStrings(
    const std::vector<std::string> & values,
    std::size_t max_items,
    const std::string & separator) {
    std::ostringstream output;
    const std::size_t limit = std::min(max_items, values.size());
    for (std::size_t index = 0; index < limit; ++index) {
        if (index > 0) {
            output << separator;
        }
        output << values[index];
    }
    if (values.size() > limit) {
        if (limit > 0) {
            output << separator;
        }
        output << "+" << (values.size() - limit) << " more";
    }
    return output.str();
}

std::string ExtractDelimitedValue(const std::string & text, const std::string & key) {
    const std::string prefix = key + "=";
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind(prefix, 0) == 0) {
            return Trim(line.substr(prefix.size()));
        }
    }
    return std::string();
}

bool TryReadEvidenceTextFromRefs(
    const AgentConfig & config,
    const LocalChatEvidencePacket & evidence,
    std::string * evidence_text,
    std::string * evidence_source_ref) {
    if (evidence_text == nullptr || evidence_source_ref == nullptr) {
        return false;
    }
    const std::array<std::string, 3> refs = {
        evidence.result_ref,
        evidence.evidence_ref,
        evidence.resolved_log_path,
    };
    for (const std::string & ref : refs) {
        if (ref.empty()) {
            continue;
        }
        std::filesystem::path normalized;
        std::string error_message;
        if (!TryResolveAllowedPath(config, ref, &normalized, &error_message)) {
            continue;
        }
        std::string raw_content;
        std::string read_error;
        if (!ReadWholeFile(normalized.string(), &raw_content, &read_error)) {
            continue;
        }
        *evidence_text = ExtractInlineContentBlock(raw_content);
        *evidence_source_ref = normalized.string();
        return !evidence_text->empty();
    }
    return false;
}

bool TryResolveDirectoryScopeFromEvidenceRefs(
    const AgentConfig & config,
    const LocalChatEvidencePacket & evidence,
    std::filesystem::path * directory_scope,
    std::string * evidence_source_ref) {
    if (directory_scope == nullptr || evidence_source_ref == nullptr) {
        return false;
    }
    const std::array<std::string, 3> refs = {
        evidence.result_ref,
        evidence.evidence_ref,
        evidence.resolved_log_path,
    };
    for (const std::string & ref : refs) {
        if (ref.empty()) {
            continue;
        }
        std::filesystem::path normalized;
        std::string error_message;
        if (!TryResolveAllowedPath(config, ref, &normalized, &error_message)) {
            continue;
        }

        std::string raw_content;
        std::string read_error;
        if (!ReadWholeFile(normalized.string(), &raw_content, &read_error)) {
            continue;
        }

        const std::string directory_path = FirstNonEmpty(
            ExtractDelimitedValue(raw_content, "normalized_directory_path"),
            ExtractDelimitedValue(raw_content, "directory_path"));
        if (directory_path.empty()) {
            continue;
        }

        std::filesystem::path candidate_scope;
        if (!LooksLikeDirectoryScope(config, directory_path, &candidate_scope)) {
            continue;
        }

        *directory_scope = candidate_scope;
        *evidence_source_ref = normalized.string();
        return true;
    }
    return false;
}

std::string DefaultDirectoryAnalysisExtensionsCsv() {
    return ".c,.cc,.cpp,.cxx,.h,.hh,.hpp,.inl,.ts,.tsx,.js,.jsx,.json,.py,.java,.go,.rs,.cs,.svelte,.html,.htm,.cmake,.txt,.toml,.yml,.yaml,.xml,.sh,.ps1";
}

bool IsPathUnderLogRoot(const AgentConfig & config, const std::string & raw_path) {
    if (raw_path.empty()) {
        return false;
    }
    std::error_code ec;
    const std::filesystem::path normalized =
        std::filesystem::weakly_canonical(std::filesystem::path(raw_path), ec);
    if (ec) {
        return false;
    }
    return StartsWithPath(normalized, std::filesystem::path(config.log_root));
}

bool TryPrepareDirectoryAnalysisEvidence(
    const AgentConfig & config,
    const std::filesystem::path & directory_scope,
    LocalChatEvidencePacket * evidence,
    std::string * evidence_input_mode,
    std::string * evidence_source_ref) {
    if (evidence == nullptr || evidence_input_mode == nullptr || evidence_source_ref == nullptr) {
        return false;
    }

    const CommandResult bundle_result = PrepareDirectoryAnalysisResult(
        config,
        directory_scope.string(),
        DefaultDirectoryAnalysisExtensionsCsv(),
        200,
        60,
        1200,
        24000,
        std::string());
    if (!bundle_result.ok) {
        return false;
    }

    const std::string bundle_text = GetFieldOrDefault(bundle_result, "content_text", "");
    if (bundle_text.empty()) {
        return false;
    }

    evidence->source_excerpt = bundle_text;
    if (evidence->result_ref.empty()) {
        evidence->result_ref = GetFieldOrDefault(bundle_result, "result_ref", "");
    }
    if (evidence->evidence_ref.empty()) {
        evidence->evidence_ref = GetFieldOrDefault(bundle_result, "evidence_ref", "");
    }
    if (evidence->resolved_log_path.empty()) {
        evidence->resolved_log_path = GetFieldOrDefault(bundle_result, "log_path", "");
    }

    *evidence_input_mode = "directory_analysis_bundle";
    *evidence_source_ref = FirstNonEmpty(
        GetFieldOrDefault(bundle_result, "analysis_bundle_ref", ""),
        GetFieldOrDefault(bundle_result, "result_ref", ""),
        GetFieldOrDefault(bundle_result, "evidence_ref", ""),
        GetFieldOrDefault(bundle_result, "log_path", ""));
    return true;
}

CommandResult BuildDirectoryScopeFallbackResult(
    const AgentConfig & config,
    const std::filesystem::path & scope_path,
    const std::string & scope,
    const std::string & question,
    const std::string & mode,
    const std::string & provider_detail) {
    CommandResult result;
    const std::set<std::string> allowed_extensions = {
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inl",
        ".ts", ".tsx", ".js", ".jsx", ".json", ".py", ".java",
        ".go", ".rs", ".cs", ".svelte", ".html", ".htm", ".cmake"
    };
    const std::set<std::string> interesting_filenames = {
        "cmakelists.txt", "package.json", "server.cpp", "main.cpp", "main.c",
        "main.ts", "index.ts", "index.js", "app.ts", "app.js"
    };
    const std::set<std::string> skipped_directories = {
        ".git", ".vs", ".vscode", "build", "out", "dist", "node_modules", "bin", "obj"
    };

    std::map<std::string, int> extension_counts;
    std::map<std::string, int> top_level_counts;
    std::vector<std::string> matching_files;
    std::vector<std::string> evidence_files;
    std::set<std::string> evidence_seen;

    const auto options = std::filesystem::directory_options::skip_permission_denied;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(scope_path, options, ec);
         it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const std::filesystem::path current = it->path();
        if (it->is_directory(ec)) {
            const std::string dir_name = ToLowerAscii(current.filename().string());
            if (skipped_directories.count(dir_name) > 0) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }

        const std::string filename = current.filename().string();
        const std::string filename_lower = ToLowerAscii(filename);
        const std::string extension = ToLowerAscii(current.extension().string());
        if (allowed_extensions.count(extension) == 0 &&
            interesting_filenames.count(filename_lower) == 0) {
            continue;
        }

        std::string relative_path = std::filesystem::relative(current, scope_path, ec).generic_string();
        if (ec || relative_path.empty()) {
            ec.clear();
            relative_path = filename;
        }

        matching_files.push_back(relative_path);
        extension_counts[extension.empty() ? "[no_ext]" : extension] += 1;

        const std::size_t slash_pos = relative_path.find('/');
        const std::string top_level = slash_pos == std::string::npos
            ? "[root]"
            : relative_path.substr(0, slash_pos);
        top_level_counts[top_level] += 1;

        if ((interesting_filenames.count(filename_lower) > 0 || filename_lower.find("main") != std::string::npos) &&
            evidence_seen.insert(relative_path).second) {
            evidence_files.push_back(relative_path);
        }
    }

    std::sort(matching_files.begin(), matching_files.end());
    std::sort(evidence_files.begin(), evidence_files.end());
    if (evidence_files.empty()) {
        for (const std::string & item : matching_files) {
            if (evidence_seen.insert(item).second) {
                evidence_files.push_back(item);
            }
            if (evidence_files.size() >= 6) {
                break;
            }
        }
    }

    std::vector<std::pair<std::string, int>> extension_pairs(extension_counts.begin(), extension_counts.end());
    std::sort(extension_pairs.begin(), extension_pairs.end(), [](const auto & left, const auto & right) {
        if (left.second != right.second) {
            return left.second > right.second;
        }
        return left.first < right.first;
    });
    std::vector<std::string> extension_summary;
    for (const auto & entry : extension_pairs) {
        extension_summary.push_back(entry.first + ":" + std::to_string(entry.second));
    }

    std::vector<std::pair<std::string, int>> module_pairs(top_level_counts.begin(), top_level_counts.end());
    std::sort(module_pairs.begin(), module_pairs.end(), [](const auto & left, const auto & right) {
        if (left.second != right.second) {
            return left.second > right.second;
        }
        return left.first < right.first;
    });
    std::vector<std::string> module_summary;
    for (const auto & entry : module_pairs) {
        module_summary.push_back(entry.first + ":" + std::to_string(entry.second));
    }

    const std::string normalized_scope = scope_path.string();
    const std::string concise_extensions = JoinLimitedStrings(extension_summary, 6, ", ");
    const std::string concise_modules = JoinLimitedStrings(module_summary, 6, ", ");
    const std::string concise_evidence = JoinLimitedStrings(evidence_files, 6, ", ");
    const bool sufficient = matching_files.size() >= 3;

    std::ostringstream direct_answer;
    direct_answer
        << "Project fallback summary for " << normalized_scope
        << ": scanned " << matching_files.size() << " code or config files.";
    if (!concise_modules.empty()) {
        direct_answer << " Main modules by directory: " << concise_modules << ".";
    }
    if (!concise_extensions.empty()) {
        direct_answer << " Dominant file types: " << concise_extensions << ".";
    }
    if (!concise_evidence.empty()) {
        direct_answer << " Likely entry or evidence files: " << concise_evidence << ".";
    }
    direct_answer
        << " This is a deterministic fallback because local analysis providers are unavailable.";

    std::ostringstream evidence_lines;
    evidence_lines
        << "question=" << question << "\n"
        << "scope=" << normalized_scope << "\n"
        << "mode=" << mode << "\n"
        << "provider_detail=" << provider_detail << "\n"
        << "matching_file_count=" << matching_files.size() << "\n"
        << "top_level_modules=" << concise_modules << "\n"
        << "extension_counts=" << concise_extensions << "\n"
        << "evidence_files=" << concise_evidence;

    const std::string log_path = BuildLogPath(config, "local_chat_project_fallback");
    std::ofstream output(log_path, std::ios::out | std::ios::trunc);
    output << evidence_lines.str() << "\n";

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "ok";
    result.fields["scope"] = scope;
    result.fields["mode"] = mode.empty() ? "code_analysis" : mode;
    result.fields["question"] = question;
    result.fields["question_effective"] = question;
    result.fields["analysis_only"] = "true";
    result.fields["execution_capability"] = "false";
    result.fields["review_analysis_only"] = "true";
    result.fields["implicit_evidence_lookup"] = "false";
    result.fields["evidence_injection_template"] =
        "task_id,result_ref,evidence_ref,resolved_log_path,log_excerpt,source_excerpt";
    result.fields["evidence_injection_used"] = "false";
    result.fields["local_chat_endpoint_primary"] = config.local_chat_endpoint;
    result.fields["local_chat_endpoint_fallback"] = DeriveLocalChatFallbackEndpoint(config);
    result.fields["local_chat_endpoint_effective"] = "";
    result.fields["local_chat_endpoint_mode"] = "deterministic_directory_fallback";
    result.fields["local_chat_endpoint_policy"] = "fallback_without_provider";
    result.fields["local_chat_detail"] = provider_detail;
    result.fields["dispatch_mode"] = "deterministic_directory_fallback";
    result.fields["trusted_execution_evidence_required"] = "true";
    result.fields["fallback_mode"] = "project_directory_summary";
    result.fields["project_summary_contract"] =
        "Preferred: pass scope as a workspace directory path. Optional: include source_excerpt or result_ref/evidence_ref from completed lan_agent_read_directory_files when the caller already has curated evidence.";
    result.fields["provider_recovery_hint"] =
        "Restore local_chat endpoint or generation endpoint for richer semantic analysis; current answer is filesystem-derived fallback.";
    result.fields["summary"] = direct_answer.str();
    result.fields["direct_answer"] = direct_answer.str();
    result.fields["result"] = "local_chat_project_directory_fallback";
    result.fields["source_refs"] = concise_evidence;
    result.fields["evidence_lines"] = evidence_lines.str();
    result.fields["confidence"] = sufficient ? "medium" : "low";
    result.fields["insufficient_context"] = sufficient ? "false" : "true";
    result.fields["next_action"] = sufficient
        ? "restore provider and rerun rag.query for deeper semantic synthesis if needed"
        : "provide a narrower directory scope or include source_excerpt/result_ref/evidence_ref for stronger fallback evidence";
    result.fields["log_path"] = log_path;
    result.fields["result_ref"] = log_path;
    result.fields["evidence_ref"] = log_path;
    result.fields["body"] = direct_answer.str();
    return result;
}

CommandResult BuildEvidenceFallbackResult(
    const AgentConfig & config,
    const std::string & scope,
    const std::string & question,
    const std::string & mode,
    const std::string & provider_detail,
    const std::string & evidence_text,
    const std::string & evidence_source_ref,
    const std::string & evidence_input_mode) {
    CommandResult result;
    const std::string excerpt = TruncateLocalChatEvidenceText(evidence_text, 4000);
    const bool sufficient = excerpt.size() >= 400;
    const std::string summary =
        "Provider unavailable. Returning deterministic fallback based on supplied evidence input"
        + std::string(evidence_source_ref.empty() ? "." : " from " + evidence_source_ref + ".");

    const std::string log_path = BuildLogPath(config, "local_chat_evidence_fallback");
    std::ofstream output(log_path, std::ios::out | std::ios::trunc);
    output << "question=" << question << "\n";
    output << "scope=" << scope << "\n";
    output << "mode=" << mode << "\n";
    output << "provider_detail=" << provider_detail << "\n";
    output << "evidence_input_mode=" << evidence_input_mode << "\n";
    output << "evidence_source_ref=" << evidence_source_ref << "\n";
    output << "content_begin<<<\n" << excerpt << "\n>>>content_end\n";

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "ok";
    result.fields["scope"] = scope;
    result.fields["mode"] = mode.empty() ? "code_analysis" : mode;
    result.fields["question"] = question;
    result.fields["question_effective"] = question;
    result.fields["analysis_only"] = "true";
    result.fields["execution_capability"] = "false";
    result.fields["review_analysis_only"] = "true";
    result.fields["implicit_evidence_lookup"] = "false";
    result.fields["local_chat_endpoint_primary"] = config.local_chat_endpoint;
    result.fields["local_chat_endpoint_fallback"] = DeriveLocalChatFallbackEndpoint(config);
    result.fields["local_chat_endpoint_effective"] = "";
    result.fields["local_chat_endpoint_mode"] = "deterministic_evidence_fallback";
    result.fields["local_chat_endpoint_policy"] = "fallback_without_provider";
    result.fields["local_chat_detail"] = provider_detail;
    result.fields["dispatch_mode"] = "deterministic_evidence_fallback";
    result.fields["fallback_mode"] = "evidence_excerpt_summary";
    result.fields["evidence_input_mode"] = evidence_input_mode;
    result.fields["project_summary_contract"] =
        "For project-level summary, prefer a directory scope or a completed directory-read evidence bundle. source_excerpt-only fallback is best-effort and may miss cross-file structure.";
    result.fields["provider_recovery_hint"] =
        "Restore local_chat or generation endpoint for true semantic synthesis; current answer is evidence-driven fallback.";
    result.fields["summary"] = summary;
    result.fields["direct_answer"] = summary;
    result.fields["result"] = "local_chat_evidence_fallback";
    result.fields["source_refs"] = evidence_source_ref;
    result.fields["evidence_lines"] = excerpt;
    result.fields["confidence"] = sufficient ? "low" : "very_low";
    result.fields["insufficient_context"] = sufficient ? "false" : "true";
    result.fields["next_action"] = sufficient
        ? "restore provider and rerun rag.query if cross-file architecture synthesis is needed"
        : "provide scope as a directory path or attach fuller source_excerpt/result_ref/evidence_ref";
    result.fields["log_path"] = log_path;
    result.fields["result_ref"] = log_path;
    result.fields["evidence_ref"] = log_path;
    result.fields["body"] = excerpt;
    return result;
}

bool TryBuildProviderUnavailableFallback(
    const AgentConfig & config,
    const std::string & scope,
    const std::string & question,
    const std::string & mode,
    const LocalChatEvidencePacket * evidence,
    const std::string & provider_detail,
    CommandResult * result) {
    if (result == nullptr) {
        return false;
    }

    std::filesystem::path directory_scope;
    if (LooksLikeDirectoryScope(config, scope, &directory_scope)) {
        *result = BuildDirectoryScopeFallbackResult(
            config,
            directory_scope,
            scope,
            question,
            mode,
            provider_detail);
        return true;
    }

    if (evidence != nullptr) {
        std::filesystem::path directory_scope_from_ref;
        std::string directory_scope_ref;
        if (TryResolveDirectoryScopeFromEvidenceRefs(
                config,
                *evidence,
                &directory_scope_from_ref,
                &directory_scope_ref)) {
            *result = BuildDirectoryScopeFallbackResult(
                config,
                directory_scope_from_ref,
                directory_scope_from_ref.string(),
                question,
                mode,
                provider_detail + "; directory scope resolved from " + directory_scope_ref);
            return true;
        }

        if (!evidence->source_excerpt.empty()) {
            *result = BuildEvidenceFallbackResult(
                config,
                scope,
                question,
                mode,
                provider_detail,
                evidence->source_excerpt,
                "inline_source_excerpt",
                "source_excerpt");
            return true;
        }

        std::string evidence_text;
        std::string evidence_source_ref;
        if (TryReadEvidenceTextFromRefs(config, *evidence, &evidence_text, &evidence_source_ref)) {
            *result = BuildEvidenceFallbackResult(
                config,
                scope,
                question,
                mode,
                provider_detail,
                evidence_text,
                evidence_source_ref,
                "result_or_evidence_ref");
            return true;
        }
    }

    return false;
}

std::string BuildLocalChatFallbackBody(
    const std::string & scope,
    const std::string & question,
    const std::string & mode) {
    std::ostringstream body;
    body << "{"
         << "\"model\":\"gpt-4.1\","
         << "\"temperature\":0,"
         << "\"messages\":["
         << "{\"role\":\"system\",\"content\":\"You are the codex-lan-agent local chat fallback running on the generation endpoint. You are analysis-only. Do not claim to have edited files, run builds, or executed tests unless a real MCP execution tool produced task_id, result_ref, or evidence_ref. Return concise practical analysis and point to the required MCP tools for real execution.\"},"
         << "{\"role\":\"user\",\"content\":\"scope=" << codex_lan_agent::JsonEscape(scope)
         << "\\nmode=" << codex_lan_agent::JsonEscape(mode)
         << "\\nquestion=" << codex_lan_agent::JsonEscape(question) << "\"}"
         << "]"
         << "}";
    return body.str();
}

CommandResult RunLocalChat(
    const AgentConfig & config,
    const std::string & scope,
    const std::string & question,
    const std::string & mode,
    int timeout_ms,
    const LocalChatEvidencePacket * evidence) {
    CommandResult result;
    std::string resolved_endpoint;
    std::string endpoint_detail;
    std::string endpoint_source;
    const bool endpoint_ready = ResolveReachableEndpoint(
        config.local_chat_endpoint,
        DeriveLocalChatFallbackEndpoint(config),
        2000,
        &resolved_endpoint,
        &endpoint_detail,
        &endpoint_source);
    if (!endpoint_ready || resolved_endpoint.empty()) {
        if (TryBuildProviderUnavailableFallback(
                config,
                scope,
                question,
                mode,
                evidence,
                endpoint_detail,
                &result)) {
            return result;
        }
        const std::string log_path = BuildLogPath(config, "call_local_chat_unavailable");
        std::ofstream output(log_path, std::ios::out | std::ios::trunc);
        output << "configured_local_chat_endpoint=" << config.local_chat_endpoint << "\n";
        output << "endpoint_source=" << endpoint_source << "\n";
        output << "endpoint_detail=" << endpoint_detail << "\n";
        output << "endpoint_policy=supported_fallback_until_primary_restored\n";
        output << "scope=" << scope << "\n";
        output << "mode=" << mode << "\n";
        output << "question=" << question << "\n";
        output << "error=local_chat endpoint is unavailable\n";
        result.ok = false;
        result.exit_code = 42;
        result.fields["error"] = "local_chat endpoint is unavailable";
        result.fields["local_chat_endpoint"] = config.local_chat_endpoint;
        result.fields["local_chat_endpoint_primary"] = config.local_chat_endpoint;
        result.fields["local_chat_endpoint_fallback"] = DeriveLocalChatFallbackEndpoint(config);
        result.fields["local_chat_endpoint_effective"] = resolved_endpoint;
        result.fields["local_chat_endpoint_source"] = endpoint_source;
        result.fields["local_chat_endpoint_mode"] = "unavailable";
        result.fields["local_chat_endpoint_policy"] = "supported_fallback_until_primary_restored";
        result.fields["local_chat_detail"] = endpoint_detail;
        result.fields["analysis_only"] = "true";
        result.fields["execution_capability"] = "false";
        result.fields["review_analysis_only"] = "true";
        result.fields["implicit_evidence_lookup"] = "false";
        result.fields["status"] = "failed";
        result.fields["summary"] = "local_chat endpoint is unavailable";
        result.fields["next_action"] =
            "restore provider or retry with directory scope/source_excerpt/result_ref/evidence_ref for deterministic fallback";
        result.fields["project_summary_contract"] =
            "For project summary fallback pass scope as a workspace directory path, or attach source_excerpt/result_ref/evidence_ref from completed directory reads.";
        result.fields["provider_recovery_hint"] =
            "Restore local_chat endpoint, or ensure generation fallback is reachable at /v1/chat/completions.";
        result.fields["log_path"] = log_path;
        result.fields["result_ref"] = log_path;
        result.fields["evidence_ref"] = log_path;
        result.fields["evidence_injection_template"] =
            "task_id,result_ref,evidence_ref,resolved_log_path,log_excerpt,source_excerpt";
        return result;
    }

    const std::string resolved_mode = mode.empty() ? "code_analysis" : mode;
    const bool using_generation_fallback = endpoint_source == "generation_fallback";
    LocalChatEvidencePacket hydrated_evidence;
    std::string hydrated_evidence_input_mode;
    std::string hydrated_evidence_source_ref;
    bool implicit_evidence_lookup_used = false;
    if (evidence != nullptr) {
        hydrated_evidence = *evidence;
    }
    std::filesystem::path scope_directory;
    const bool scope_is_directory = LooksLikeDirectoryScope(config, scope, &scope_directory);
    if (hydrated_evidence.source_excerpt.empty()) {
        std::filesystem::path directory_scope_from_ref;
        std::string directory_scope_ref;
        if (evidence != nullptr
            && TryResolveDirectoryScopeFromEvidenceRefs(
                config,
                hydrated_evidence,
                &directory_scope_from_ref,
                &directory_scope_ref)
            && TryPrepareDirectoryAnalysisEvidence(
                config,
                directory_scope_from_ref,
                &hydrated_evidence,
                &hydrated_evidence_input_mode,
                &hydrated_evidence_source_ref)) {
            implicit_evidence_lookup_used = true;
        } else if (scope_is_directory
                   && TryPrepareDirectoryAnalysisEvidence(
                       config,
                       scope_directory,
                       &hydrated_evidence,
                       &hydrated_evidence_input_mode,
                       &hydrated_evidence_source_ref)) {
            implicit_evidence_lookup_used = true;
        } else if (evidence != nullptr) {
            std::string evidence_text;
            std::string evidence_source_ref;
            if (TryReadEvidenceTextFromRefs(
                    config,
                    hydrated_evidence,
                    &evidence_text,
                    &evidence_source_ref)) {
                if (IsPathUnderLogRoot(config, evidence_source_ref)) {
                    hydrated_evidence.log_excerpt = evidence_text;
                    hydrated_evidence_input_mode = "result_or_evidence_ref_log";
                } else {
                    hydrated_evidence.source_excerpt = evidence_text;
                    hydrated_evidence_input_mode = "result_or_evidence_ref";
                }
                hydrated_evidence_source_ref = evidence_source_ref;
                implicit_evidence_lookup_used = true;
            }
        }
    }
    const LocalChatEvidencePacket * effective_evidence =
        hydrated_evidence.empty() ? nullptr : &hydrated_evidence;
    const bool evidence_used = effective_evidence != nullptr && !effective_evidence->empty();
    const std::string evidence_injection = evidence_used
        ? BuildLocalChatEvidenceInjectionText(*effective_evidence)
        : std::string();
    const std::string guarded_question =
        "Execution policy: analysis only. Do not claim that edits, builds, or tests were executed unless a real MCP tool produced task_id, result_ref, or evidence_ref. "
        + question
        + evidence_injection;
    const std::string body = using_generation_fallback
        ? BuildLocalChatFallbackBody(scope, guarded_question, resolved_mode)
        : (std::string("{")
            + "\"scope\":\"" + codex_lan_agent::JsonEscape(scope) + "\","
            + "\"question\":\"" + codex_lan_agent::JsonEscape(guarded_question) + "\","
            + "\"mode\":\"" + codex_lan_agent::JsonEscape(resolved_mode) + "\""
            + "}");

    const codex_lan_agent::HttpResponse response =
        codex_lan_agent::PostJson(resolved_endpoint, body, timeout_ms);

    const std::string log_path = BuildLogPath(config, "call_local_chat");
    std::ofstream output(log_path, std::ios::out | std::ios::trunc);
    output << "endpoint=" << resolved_endpoint << "\n";
    output << "configured_local_chat_endpoint=" << config.local_chat_endpoint << "\n";
    output << "endpoint_source=" << endpoint_source << "\n";
    output << "endpoint_detail=" << endpoint_detail << "\n";
    output << "endpoint_policy=supported_fallback_until_primary_restored\n";
    output << "scope=" << scope << "\n";
    output << "mode=" << resolved_mode << "\n";
    output << "timeout_ms=" << timeout_ms << "\n";
    output << "implicit_evidence_lookup=" << (implicit_evidence_lookup_used ? "true" : "false") << "\n";
    output << "evidence_input_mode=" << hydrated_evidence_input_mode << "\n";
    output << "evidence_source_ref=" << hydrated_evidence_source_ref << "\n";
    output << "evidence_injection_used=" << (evidence_used ? "true" : "false") << "\n";
    output << "question=" << question << "\n";
    output << "question_effective=" << guarded_question << "\n";
    output << "status_code=" << response.status_code << "\n";
    output << "ok=" << (response.ok ? "true" : "false") << "\n";
    output << "error=" << response.error_message << "\n";
    output << "body=\n" << response.body << "\n";

    result.ok = response.ok;
    result.exit_code = response.ok ? 0 : 43;
    result.fields["status"] = response.ok ? "ok" : "failed";
    result.fields["scope"] = scope;
    result.fields["mode"] = resolved_mode;
    result.fields["question"] = question;
    result.fields["question_effective"] = guarded_question;
    result.fields["local_chat_endpoint"] = config.local_chat_endpoint;
    result.fields["local_chat_endpoint_primary"] = config.local_chat_endpoint;
    result.fields["local_chat_endpoint_fallback"] = DeriveLocalChatFallbackEndpoint(config);
    result.fields["local_chat_endpoint_effective"] = resolved_endpoint;
    result.fields["local_chat_endpoint_source"] = endpoint_source;
    result.fields["local_chat_endpoint_mode"] = using_generation_fallback
        ? "supported_generation_fallback"
        : "configured_primary";
    result.fields["local_chat_endpoint_policy"] = "supported_fallback_until_primary_restored";
    result.fields["local_chat_detail"] = endpoint_detail;
    result.fields["dispatch_mode"] = using_generation_fallback ? "generation_endpoint_fallback" : "configured_local_chat";
    result.fields["analysis_only"] = "true";
    result.fields["execution_capability"] = "false";
    result.fields["review_analysis_only"] = "true";
    result.fields["implicit_evidence_lookup"] = implicit_evidence_lookup_used ? "true" : "false";
    result.fields["evidence_injection_template"] =
        "task_id,result_ref,evidence_ref,resolved_log_path,log_excerpt,source_excerpt";
    result.fields["evidence_injection_used"] = evidence_used ? "true" : "false";
    if (!hydrated_evidence_input_mode.empty()) {
        result.fields["evidence_input_mode"] = hydrated_evidence_input_mode;
    }
    if (!hydrated_evidence_source_ref.empty()) {
        result.fields["source_refs"] = hydrated_evidence_source_ref;
    }
    if (scope_is_directory) {
        result.fields["project_summary_contract"] =
            "Directory scope can auto-prepare a bounded analysis bundle when source_excerpt is missing; explicit source_excerpt/result_ref/evidence_ref still take precedence.";
    }
    if (effective_evidence != nullptr) {
        result.fields["evidence_task_id"] = effective_evidence->task_id;
        result.fields["evidence_result_ref"] = effective_evidence->result_ref;
        result.fields["evidence_evidence_ref"] = effective_evidence->evidence_ref;
        result.fields["evidence_resolved_log_path"] = effective_evidence->resolved_log_path;
        result.fields["evidence_log_excerpt_chars"] = std::to_string(effective_evidence->log_excerpt.size());
        result.fields["evidence_source_excerpt_chars"] = std::to_string(effective_evidence->source_excerpt.size());
    }
    result.fields["trusted_execution_evidence_required"] = "true";
    result.fields["execution_evidence_contract"] =
        "For source edits use lan_agent_apply_diff_patch or lan_agent_preview_patch plus "
        "lan_agent_apply_single_file_patch and lan_agent_verify_single_file_patch. "
        "Use lan_agent_write_text_file only for non-source text files. "
        "For builds use lan_agent_configure_project or lan_agent_build_target. "
        "For tests use lan_agent_run_ctest_target. "
        "You may also use lan_agent_execute_semantic_action as the execution bridge. "
        "Return real task_id, result_ref, evidence_ref, patch_id, or log_path fields.";
    result.fields["real_execution_toolchain_json"] =
        "[\"lan_agent_apply_diff_patch\",\"lan_agent_preview_patch\","
        "\"lan_agent_apply_single_file_patch\",\"lan_agent_verify_single_file_patch\","
        "\"lan_agent_write_text_file\",\"lan_agent_execute_semantic_action\","
        "\"lan_agent_configure_project\",\"lan_agent_build_target\","
        "\"lan_agent_run_ctest_target\",\"lan_agent_get_task\","
        "\"lan_agent_resolve_task_result\"]";
    result.fields["status_code"] = std::to_string(response.status_code);
    result.fields["log_path"] = log_path;
    result.fields["body"] = response.body;
    const std::string structured_conclusion = ExtractStructuredConclusionRaw(response.body);
    if (!structured_conclusion.empty()) {
        result.fields["structured_conclusion"] = structured_conclusion;
        result.fields["task_state"] = ExtractStructuredConclusionString(response.body, "task_state");
        result.fields["reasoning_level"] = ExtractStructuredConclusionString(response.body, "reasoning_level");
        result.fields["primary_intent"] = ExtractStructuredConclusionString(response.body, "primary_intent");
        result.fields["intent_confidence"] = NormalizeIntentConfidenceRaw(
            ExtractStructuredConclusionRawValue(response.body, "intent_confidence"));
        result.fields["association_scope"] = ExtractStructuredConclusionString(response.body, "association_scope");
        result.fields["next_action"] = ExtractStructuredConclusionString(response.body, "next_action");
        result.fields["session_id"] = ExtractStructuredConclusionString(response.body, "session_id");
        result.fields["turn_id"] = ExtractStructuredConclusionString(response.body, "turn_id");
        result.fields["slice_summary"] = ExtractStructuredConclusionString(response.body, "slice_summary");
        result.fields["expression_keys"] = ExtractStructuredConclusionRawValue(response.body, "expression_keys");
        result.fields["summary"] = ExtractStructuredConclusionString(response.body, "summary");
        result.fields["insufficient_context"] = ToLowerAscii(
            NormalizeIntentConfidenceRaw(ExtractStructuredConclusionRawValue(response.body, "insufficient_context")));
    }
    if (!response.error_message.empty()) {
        result.fields["error"] = response.error_message;
    }
    AddRagEvidenceFields(
        &result,
        scope.empty() ? "local_chat" : scope,
        response.body.substr(0, std::min<std::size_t>(response.body.size(), 2000)),
        !response.ok || response.body.empty(),
        response.ok && !response.body.empty() ? "medium" : "low");
    if (!hydrated_evidence_source_ref.empty()) {
        result.fields["source_refs"] = hydrated_evidence_source_ref;
    }
    if (GetFieldOrDefault(result, "summary", "").empty()) {
        const std::string output_text = ExtractOutputTextFallback(result);
        result.fields["summary"] = output_text.empty()
            ? (response.ok ? "local chat ok" : "local chat failed")
            : output_text.substr(0, std::min<std::size_t>(output_text.size(), 160));
    }
    if (GetFieldOrDefault(result, "direct_answer", "").empty()) {
        const std::string output_text = ExtractOutputTextFallback(result);
        const std::string fallback_answer =
            output_text.empty() ? GetFieldOrDefault(result, "summary", "") : output_text;
        if (!fallback_answer.empty()) {
            result.fields["direct_answer"] =
                fallback_answer.substr(0, std::min<std::size_t>(fallback_answer.size(), 240));
        }
    }
    if (GetFieldOrDefault(result, "next_action", "").empty()) {
        result.fields["next_action"] = response.ok
            ? "inspect output_text or log_path"
            : "inspect log_path";
    }
    result.fields["result"] = response.ok ? "local_chat_completed" : "local_chat_failed";
    return result;
}

CommandResult BuildProfileListResult(const AgentConfig & config) {
    CommandResult result;
    int index = 0;
    for (const auto & entry : config.profiles) {
        result.fields["profile_" + std::to_string(index)] = entry.first;
        ++index;
    }
    result.fields["profile_count"] = std::to_string(index);
    return result;
}

TaskManager::TaskManager(const AgentConfig & config)
    : config_(config),
      worker_(&TaskManager::WorkerLoop, this) {
}

TaskManager::~TaskManager() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

std::string TaskManager::StatusTimeStamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t current_time = std::chrono::system_clock::to_time_t(now);
    std::tm local_tm{};
#ifdef _WIN32
    localtime_s(&local_tm, &current_time);
#else
    localtime_r(&current_time, &local_tm);
#endif
    char buffer[64];
    std::snprintf(
        buffer,
        sizeof(buffer),
        "%04d-%02d-%02d %02d:%02d:%02d",
        local_tm.tm_year + 1900,
        local_tm.tm_mon + 1,
        local_tm.tm_mday,
        local_tm.tm_hour,
        local_tm.tm_min,
        local_tm.tm_sec);
    return buffer;
}

std::string TaskManager::TaskKindName(TaskKind kind) {
    switch (kind) {
    case TaskKind::kCliProfile:
        return "cli_profile";
    case TaskKind::kCxParserRuntime:
        return "cxparser_runtime";
    case TaskKind::kCase:
        return "case";
    case TaskKind::kRagFlow:
        return "rag_flow";
    case TaskKind::kLocalChat:
        return "local_chat";
    }
    return "unknown";
}

std::string TaskManager::EnqueueTask(
    TaskKind kind,
    const std::string & arg1,
    const std::string & arg2,
    int timeout_sec_override,
    int stall_timeout_sec_override) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream id_builder;
    id_builder << "task-" << TimeStampForFileName() << "-" << next_id_++;

    TaskRecord record;
    record.task_id = id_builder.str();
    record.kind = kind;
    record.arg1 = arg1;
    record.arg2 = arg2;
    record.timeout_sec_override = timeout_sec_override;
    record.stall_timeout_sec_override = stall_timeout_sec_override;
    if (kind == TaskKind::kCliProfile || kind == TaskKind::kCxParserRuntime) {
        record.resource_key = BuildTaskResourceKey(arg1, arg2);
        record.pending_log_path = kind == TaskKind::kCliProfile
            ? BuildLogPath(config_, arg1)
            : BuildLogPath(config_, "cxparser_runtime_" + arg1);
    }
    record.status = "queued";
    record.submitted_at = StatusTimeStamp();

    tasks_[record.task_id] = record;
    pending_ids_.push_back(record.task_id);
    condition_.notify_one();
    return record.task_id;
}

std::string TaskManager::EnqueueCliProfile(
    const std::string & profile,
    const std::string & args,
    int timeout_sec_override,
    int stall_timeout_sec_override) {
    return EnqueueTask(
        TaskKind::kCliProfile,
        profile,
        args,
        timeout_sec_override,
        stall_timeout_sec_override);
}

std::string TaskManager::EnqueueCxParserRuntime(const std::string & flow_id, const std::string & args) {
    return EnqueueTask(TaskKind::kCxParserRuntime, flow_id, args);
}

std::string TaskManager::EnqueueCase(const std::string & case_path) {
    return EnqueueTask(TaskKind::kCase, case_path, "");
}

std::string TaskManager::EnqueueRagFlow(const std::string & query, const std::string & mode) {
    return EnqueueTask(TaskKind::kRagFlow, query, mode.empty() ? "review" : mode);
}

std::string TaskManager::EnqueueLocalChat(
    const std::string & scope,
    const std::string & question,
    const std::string & mode) {
    return EnqueueTask(
        TaskKind::kLocalChat,
        scope,
        std::string("{\"question\":\"")
            + codex_lan_agent::JsonEscape(question)
            + "\",\"mode\":\""
            + codex_lan_agent::JsonEscape(mode.empty() ? "code_analysis" : mode)
            + "\"}");
}

CommandResult TaskManager::GetTaskResult(const std::string & task_id) const {
    CommandResult result;
    result.fields["task_id"] = task_id;

    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        result.ok = false;
        result.exit_code = 40;
        result.fields["error"] = "task not found";
        result.fields["failure_mode"] = "task_evicted_from_history";
        result.fields["summary"] =
            "task not found; prefer result_ref, evidence_ref, or resolved_log_path for completed work";
        result.fields["next_action"] =
            "use result_ref/evidence_ref/resolved_log_path instead of long-lived task polling";
        result.fields["task_retention_model"] = "ephemeral_task_long_lived_result_refs";
        result.fields["task_history_window"] = "bounded_completed_history";
        result.fields["max_completed_history"] = std::to_string(max_completed_history_);
        return result;
    }

    const TaskRecord & record = it->second;
    result.ok = record.status == "succeeded";
    result.exit_code = (record.status == "failed") ? record.result.exit_code : 0;
    result.fields["task_type"] = TaskKindName(record.kind);
    result.fields["status"] = record.status;
    result.fields["submitted_at"] = record.submitted_at;
    result.fields["started_at"] = record.started_at;
    result.fields["completed_at"] = record.completed_at;
    result.fields["queue_depth"] = std::to_string(static_cast<int>(pending_ids_.size()));
    result.fields["task_retention_model"] = "ephemeral_task_long_lived_result_refs";
    result.fields["max_completed_history"] = std::to_string(max_completed_history_);
    if (!record.arg1.empty()) {
        result.fields["arg1"] = record.arg1;
        result.fields["expected_marker"] = ExpectedMarkerForProfile(record.arg1);
    }
    if (!record.arg2.empty()) {
        result.fields["arg2"] = record.arg2;
    }
    if (!record.resource_key.empty()) {
        result.fields["resource_key"] = record.resource_key;
    }
    if (record.timeout_sec_override >= 0) {
        result.fields["timeout_sec_override"] = std::to_string(record.timeout_sec_override);
    }
    if (record.stall_timeout_sec_override >= 0) {
        result.fields["stall_timeout_sec_override"] = std::to_string(record.stall_timeout_sec_override);
    }
    if (!record.pending_log_path.empty()) {
        result.fields["log_path"] = record.pending_log_path;
    }
    for (const auto & entry : record.result.fields) {
        result.fields["result_" + entry.first] = entry.second;
    }
    auto promote_result_field = [&](const std::string & key) {
        const auto result_it = record.result.fields.find(key);
        if (result_it != record.result.fields.end() && !result_it->second.empty()) {
            result.fields[key] = result_it->second;
        }
    };
    promote_result_field("log_path");
    promote_result_field("result_log_path");
    promote_result_field("trace_log_path");
    promote_result_field("result_ref");
    promote_result_field("evidence_ref");
    promote_result_field("effective_timeout_sec");
    promote_result_field("effective_stall_timeout_sec");
    promote_result_field("effective_timeout_scope");
    promote_result_field("execution_completion_reason");
    promote_result_field("process_progress_signal");
    promote_result_field("timeout_diagnostic");
    promote_result_field("runtime_sec");
    promote_result_field("quiet_sec_at_finish");
    promote_result_field("heartbeat_count");
    promote_result_field("process_output_observed");
    const auto semantic_it = record.result.fields.find("semantic_outcome");
    if (semantic_it != record.result.fields.end()) {
        result.fields["semantic_outcome"] = semantic_it->second;
    }
    const auto expected_it = record.result.fields.find("expected_marker");
    if (expected_it != record.result.fields.end()) {
        result.fields["expected_marker"] = expected_it->second;
    }
    if (record.status == "running" || record.status == "queued") {
        result.ok = true;
        result.exit_code = 0;
    }
    if (record.status == "queued") {
        result.fields["summary"] = "queued";
        result.fields["next_action"] = "wait or query task again";
    } else if (record.status == "running") {
        result.fields["summary"] = "running";
        result.fields["next_action"] = "query task again or tail task log";
    } else {
        const auto stalled_it = record.result.fields.find("stalled");
        const auto timed_out_it = record.result.fields.find("timed_out");
        const auto log_it = record.result.fields.find("log_path");
        const bool stalled =
            stalled_it != record.result.fields.end() && stalled_it->second == "true";
        const bool timed_out =
            timed_out_it != record.result.fields.end() && timed_out_it->second == "true";

        if (stalled) {
            result.fields["summary"] = "stalled; inspect log tail";
            result.fields["next_action"] = "tail task log";
        } else if (timed_out) {
            result.fields["summary"] = "timed out; inspect log tail";
            result.fields["next_action"] = "tail task log";
        } else if (!record.result.ok) {
            result.fields["summary"] = "failed; inspect log tail";
            result.fields["next_action"] = "tail task log";
        } else if (log_it != record.result.fields.end() && !log_it->second.empty()) {
            result.fields["summary"] = "ok; log available";
            result.fields["next_action"] = "none";
        } else {
            result.fields["summary"] = record.result.ok ? "ok" : "failed";
            result.fields["next_action"] = record.result.ok ? "none" : "tail task log";
        }
    }

    const std::string resolved_log_path = FirstNonEmpty(
        GetFieldOrDefault(result, "result_log_path", ""),
        GetFieldOrDefault(result, "log_path", ""),
        record.pending_log_path);
    if (!resolved_log_path.empty()) {
        result.fields["resolved_log_path"] = resolved_log_path;
    }

    const std::string task_log_ref = "task-log(" + task_id + ")";
    result.fields["task_log_ref"] = task_log_ref;

    if (GetFieldOrDefault(result, "result_ref", "").empty() && !resolved_log_path.empty()) {
        result.fields["result_ref"] = resolved_log_path;
    }
    if (GetFieldOrDefault(result, "evidence_ref", "").empty()) {
        result.fields["evidence_ref"] = FirstNonEmpty(
            GetFieldOrDefault(result, "trace_log_path", ""),
            resolved_log_path,
            task_log_ref);
    }

    return result;
}

CommandResult TaskManager::GetLatestTaskResult() const {
    std::string latest_task_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto & entry : tasks_) {
            if (latest_task_id.empty() || entry.first > latest_task_id) {
                latest_task_id = entry.first;
            }
        }
    }
    if (latest_task_id.empty()) {
        CommandResult result;
        result.ok = false;
        result.exit_code = 40;
        result.fields["error"] = "no task found";
        result.fields["status"] = "empty";
        return result;
    }
    return GetTaskResult(latest_task_id);
}

CommandResult TaskManager::ListTaskResults(int max_entries) const {
    std::vector<std::string> task_ids;
    int queue_depth = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        task_ids.reserve(tasks_.size());
        for (const auto & entry : tasks_) {
            task_ids.push_back(entry.first);
        }
        queue_depth = static_cast<int>(pending_ids_.size());
    }
    std::sort(task_ids.begin(), task_ids.end());

    const int bounded_max_entries = max_entries > 0 ? max_entries : 20;
    const std::size_t start =
        task_ids.size() > static_cast<std::size_t>(bounded_max_entries)
            ? task_ids.size() - static_cast<std::size_t>(bounded_max_entries)
            : 0;

    CommandResult result;
    result.fields["status"] = "ok";
    result.fields["queue_depth"] = std::to_string(queue_depth);
    result.fields["task_retention_model"] = "ephemeral_task_long_lived_result_refs";
    result.fields["max_completed_history"] = std::to_string(max_completed_history_);
    result.fields["total_task_count"] = std::to_string(task_ids.size());

    std::ostringstream tasks_json;
    tasks_json << "[";
    int visible_index = 0;
    for (std::size_t index = start; index < task_ids.size(); ++index) {
        const CommandResult task = GetTaskResult(task_ids[index]);
        const std::string prefix = "item_" + std::to_string(visible_index) + "_";
        result.fields[prefix + "task_id"] = GetFieldOrDefault(task, "task_id", "");
        result.fields[prefix + "status"] = GetFieldOrDefault(task, "status", "");
        result.fields[prefix + "task_type"] = GetFieldOrDefault(task, "task_type", "");
        result.fields[prefix + "submitted_at"] = GetFieldOrDefault(task, "submitted_at", "");
        result.fields[prefix + "started_at"] = GetFieldOrDefault(task, "started_at", "");
        result.fields[prefix + "completed_at"] = GetFieldOrDefault(task, "completed_at", "");
        result.fields[prefix + "summary"] = GetFieldOrDefault(task, "summary", "");
        result.fields[prefix + "result_ref"] = GetFieldOrDefault(task, "result_ref", "");
        result.fields[prefix + "evidence_ref"] = GetFieldOrDefault(task, "evidence_ref", "");
        result.fields[prefix + "resolved_log_path"] = GetFieldOrDefault(task, "resolved_log_path", "");
        result.fields[prefix + "task_log_ref"] = GetFieldOrDefault(task, "task_log_ref", "");

        if (visible_index > 0) {
            tasks_json << ",";
        }
        tasks_json << "{"
                   << "\"task_id\":\"" << codex_lan_agent::JsonEscape(result.fields[prefix + "task_id"]) << "\","
                   << "\"status\":\"" << codex_lan_agent::JsonEscape(result.fields[prefix + "status"]) << "\","
                   << "\"task_type\":\"" << codex_lan_agent::JsonEscape(result.fields[prefix + "task_type"]) << "\","
                   << "\"submitted_at\":\"" << codex_lan_agent::JsonEscape(result.fields[prefix + "submitted_at"]) << "\","
                   << "\"started_at\":\"" << codex_lan_agent::JsonEscape(result.fields[prefix + "started_at"]) << "\","
                   << "\"completed_at\":\"" << codex_lan_agent::JsonEscape(result.fields[prefix + "completed_at"]) << "\","
                   << "\"summary\":\"" << codex_lan_agent::JsonEscape(result.fields[prefix + "summary"]) << "\","
                   << "\"task_log_ref\":\"" << codex_lan_agent::JsonEscape(result.fields[prefix + "task_log_ref"]) << "\","
                   << "\"result_ref\":\"" << codex_lan_agent::JsonEscape(result.fields[prefix + "result_ref"]) << "\","
                   << "\"evidence_ref\":\"" << codex_lan_agent::JsonEscape(result.fields[prefix + "evidence_ref"]) << "\","
                   << "\"resolved_log_path\":\"" << codex_lan_agent::JsonEscape(result.fields[prefix + "resolved_log_path"]) << "\""
                   << "}";
        ++visible_index;
    }
    tasks_json << "]";

    result.fields["visible_count"] = std::to_string(visible_index);
    result.fields["tasks_json"] = tasks_json.str();
    result.fields["summary"] = visible_index > 0 ? "task list returned" : "no task found";
    return result;
}

int TaskManager::QueueDepth() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(pending_ids_.size());
}

void TaskManager::PruneCompletedTasksLocked() {
    while (completed_ids_.size() > max_completed_history_) {
        const std::string oldest_id = completed_ids_.front();
        completed_ids_.pop_front();
        const auto it = tasks_.find(oldest_id);
        if (it != tasks_.end() &&
            it->second.status != "queued" &&
            it->second.status != "running") {
            tasks_.erase(it);
        }
    }
}

void TaskManager::WorkerLoop() {
    while (true) {
        TaskRecord task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this]() { return stop_ || !pending_ids_.empty(); });
            if (stop_ && pending_ids_.empty()) {
                return;
            }
            const std::string task_id = pending_ids_.front();
            pending_ids_.pop_front();
            TaskRecord & record = tasks_[task_id];
            record.status = "running";
            record.started_at = StatusTimeStamp();
            task = record;
        }

        CommandResult task_result;
        if (task.kind == TaskKind::kCliProfile) {
            task_result = RunCliProfile(
                config_,
                task.arg1,
                task.arg2,
                task.pending_log_path,
                task.timeout_sec_override,
                task.stall_timeout_sec_override);
        } else if (task.kind == TaskKind::kCxParserRuntime) {
            task_result = RunCxParserRuntimeCommand(config_, task.arg1, task.arg2, task.pending_log_path);
        } else if (task.kind == TaskKind::kCase) {
            task_result = RunCase(config_, task.arg1);
        } else if (task.kind == TaskKind::kLocalChat) {
            const LocalChatEvidencePacket evidence = ExtractLocalChatEvidencePacket(task.arg2);
            task_result = RunLocalChat(
                config_,
                task.arg1,
                ExtractJsonString(task.arg2, "question"),
                ExtractJsonString(task.arg2, "mode"),
                30000,
                &evidence);
        } else {
            task_result = RunRagFlow(config_, task.arg1, task.arg2);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            TaskRecord & record = tasks_[task.task_id];
            record.result = task_result;
            record.completed_at = StatusTimeStamp();
            record.status = task_result.ok ? "succeeded" : "failed";
            completed_ids_.push_back(task.task_id);
            PruneCompletedTasksLocked();
        }
    }
}
