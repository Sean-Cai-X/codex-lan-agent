#pragma once

#include "AgentConfig.h"
#include "JsonRequestView.h"
#include "ProcessRunner.h"
#include "comm.h"
#include "types.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

bool TryResolveAllowedPath(
    const codex_lan_agent::AgentConfig & config,
    const std::string & raw_path,
    std::filesystem::path * normalized_path,
    std::string * error_message);

namespace codex_lan_agent {

namespace code_format_detail {

inline std::string BoolText(bool value) {
    return value ? "true" : "false";
}

inline std::string ToLowerAsciiLocal(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    return value;
}

inline std::string StableHash(const std::string & content) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : content) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    std::ostringstream output;
    output << std::hex << std::setw(16) << std::setfill('0') << hash;
    return output.str();
}

inline bool ReadFile(const std::filesystem::path & path, std::string * content, std::string * error) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        if (error) {
            *error = "failed to open file for read";
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (content) {
        *content = buffer.str();
    }
    return true;
}

inline bool WriteFile(const std::filesystem::path & path, const std::string & content, std::string * error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error) {
            *error = "failed to create parent directory: " + ec.message();
        }
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        if (error) {
            *error = "failed to open file for write";
        }
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output.good()) {
        if (error) {
            *error = "failed to write file";
        }
        return false;
    }
    return true;
}

inline std::string SanitizeToken(std::string value, const std::string & fallback) {
    std::string output;
    for (char ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) != 0 || ch == '_' || ch == '-' || ch == '.') {
            output.push_back(ch);
        } else {
            output.push_back('_');
        }
    }
    if (output.empty()) {
        return fallback;
    }
    if (output.size() > 80) {
        output.resize(80);
    }
    return output;
}

inline std::string QuoteProcessArgument(const std::string & value) {
#ifdef _WIN32
    std::string quoted = "\"";
    std::size_t backslashes = 0;
    for (char ch : value) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }
        if (ch == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back('"');
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, '\\');
        backslashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(backslashes * 2, '\\');
    quoted.push_back('"');
    return quoted;
#else
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
#endif
}

inline bool LooksLikeClangFormatExecutable(const std::filesystem::path & path) {
    const std::string filename = ToLowerAsciiLocal(path.filename().string());
#ifdef _WIN32
    return filename == "clang-format.exe" || filename == "clang-format";
#else
    return filename == "clang-format";
#endif
}

inline void AddCandidate(
    std::vector<std::filesystem::path> * candidates,
    const std::filesystem::path & candidate) {
    if (candidate.empty()) {
        return;
    }
    candidates->push_back(candidate);
}

inline std::vector<std::filesystem::path> BuildFormatterCandidates(
    const AgentConfig & config,
    const std::string & explicit_path) {
    std::vector<std::filesystem::path> candidates;
    AddCandidate(&candidates, explicit_path);

    const char * env_clang_format = std::getenv("CLANG_FORMAT");
    if (env_clang_format && env_clang_format[0] != '\0') {
        AddCandidate(&candidates, env_clang_format);
    }

    const char * path_env = std::getenv("PATH");
    if (path_env && path_env[0] != '\0') {
        std::string path_text(path_env);
#ifdef _WIN32
        const char separator = ';';
        const std::string exe_name = "clang-format.exe";
#else
        const char separator = ':';
        const std::string exe_name = "clang-format";
#endif
        std::size_t start = 0;
        while (start <= path_text.size()) {
            const std::size_t end = path_text.find(separator, start);
            const std::string entry = path_text.substr(
                start,
                end == std::string::npos ? std::string::npos : end - start);
            if (!entry.empty()) {
                AddCandidate(&candidates, std::filesystem::path(entry) / exe_name);
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
    }

    AddCandidate(&candidates, std::filesystem::path(config.workspace_root) / "third_party" / "llvm" / "bin" / "clang-format.exe");
    AddCandidate(&candidates, std::filesystem::current_path() / "third_party" / "llvm" / "bin" / "clang-format.exe");
    AddCandidate(&candidates, std::filesystem::current_path().parent_path() / "third_party" / "llvm" / "bin" / "clang-format.exe");

#ifdef _WIN32
    const char * llvm_home = std::getenv("LLVM_HOME");
    if (llvm_home && llvm_home[0] != '\0') {
        AddCandidate(&candidates, std::filesystem::path(llvm_home) / "bin" / "clang-format.exe");
    }
    const char * program_files = std::getenv("ProgramFiles");
    if (program_files && program_files[0] != '\0') {
        AddCandidate(&candidates, std::filesystem::path(program_files) / "LLVM" / "bin" / "clang-format.exe");
        const std::filesystem::path vs_root = std::filesystem::path(program_files) / "Microsoft Visual Studio" / "2022";
        for (const std::string & edition : {"Community", "Professional", "Enterprise", "BuildTools"}) {
            const std::filesystem::path llvm_root = vs_root / edition / "VC" / "Tools" / "Llvm";
            AddCandidate(&candidates, llvm_root / "bin" / "clang-format.exe");
            AddCandidate(&candidates, llvm_root / "x64" / "bin" / "clang-format.exe");
            AddCandidate(&candidates, llvm_root / "ARM64" / "bin" / "clang-format.exe");
        }
    }
    const char * program_files_x86 = std::getenv("ProgramFiles(x86)");
    if (program_files_x86 && program_files_x86[0] != '\0') {
        AddCandidate(&candidates, std::filesystem::path(program_files_x86) / "LLVM" / "bin" / "clang-format.exe");
    }
#endif
    return candidates;
}

inline std::string CandidatesJson(const std::vector<std::filesystem::path> & candidates) {
    std::ostringstream output;
    output << "[";
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (index > 0) {
            output << ",";
        }
        output << "\"" << JsonEscape(candidates[index].string()) << "\"";
    }
    output << "]";
    return output.str();
}

inline bool ResolveFormatterPath(
    const AgentConfig & config,
    const std::string & explicit_path,
    std::filesystem::path * resolved,
    std::string * source,
    std::string * candidates_json) {
    const std::vector<std::filesystem::path> candidates = BuildFormatterCandidates(config, explicit_path);
    if (candidates_json) {
        *candidates_json = CandidatesJson(candidates);
    }
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        std::error_code ec;
        const std::filesystem::path candidate = std::filesystem::weakly_canonical(candidates[index], ec);
        if (ec || !std::filesystem::is_regular_file(candidate, ec) || ec) {
            continue;
        }
        if (!LooksLikeClangFormatExecutable(candidate)) {
            continue;
        }
        if (resolved) {
            *resolved = candidate;
        }
        if (source) {
            if (!explicit_path.empty() && index == 0) {
                *source = "explicit_path";
            } else if (index == 1 && std::getenv("CLANG_FORMAT") != nullptr) {
                *source = "CLANG_FORMAT";
            } else {
                *source = "auto_search";
            }
        }
        return true;
    }
    return false;
}

inline std::string BuildArtifactDirectory(const AgentConfig & config, const std::string & filename) {
    const std::filesystem::path root(config.log_root);
    const std::string token = SanitizeToken(filename, "source") + "_" + CommOperations::TimeStampForFileName();
    return (root / "code_format" / token).string();
}

inline std::string BuildFormatCommandLine(
    const std::filesystem::path & formatter,
    const std::string & style,
    const std::string & fallback_style,
    const std::filesystem::path & target_file) {
    std::ostringstream command;
    command << QuoteProcessArgument(formatter.string());
    command << " -i";
    command << " " << QuoteProcessArgument("-style=" + style);
    if (!fallback_style.empty()) {
        command << " " << QuoteProcessArgument("--fallback-style=" + fallback_style);
    }
    command << " " << QuoteProcessArgument(target_file.string());
    return command.str();
}

}  // namespace code_format_detail

inline CommandResult BuildFormatCodeFileResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    using namespace code_format_detail;

    CommandResult result;
    result.fields["task_type"] = "code_format";
    result.fields["formatter"] = "clang-format";

    const std::string source_file = params.GetString("source_file", params.GetString("file_path"));
    const std::string explicit_formatter_path = params.GetString("clang_format_path");
    const std::string style = params.GetString("style", "file");
    const std::string fallback_style = params.GetString("fallback_style", "LLVM");
    const bool dry_run = params.GetBool("dry_run", true);
    const bool output_formatted_content = params.GetBool("output_formatted_content", false);
    const int max_formatted_chars = std::max(0, params.GetInt("max_formatted_chars", 120000));
    const int timeout_ms = std::max(1000, std::min(params.GetInt("timeout_ms", 30000), 300000));
    const int timeout_sec = std::max(1, (timeout_ms + 999) / 1000);

    result.fields["source_file"] = source_file;
    result.fields["style"] = style;
    result.fields["fallback_style"] = fallback_style;
    result.fields["dry_run"] = BoolText(dry_run);

    if (source_file.empty()) {
        result.ok = false;
        result.exit_code = 30;
        result.fields["error"] = "source_file is required";
        result.fields["semantic_outcome"] = "invalid_input";
        return result;
    }
    if (style.find('\n') != std::string::npos || fallback_style.find('\n') != std::string::npos) {
        result.ok = false;
        result.exit_code = 31;
        result.fields["error"] = "style and fallback_style must be single-line clang-format values";
        result.fields["semantic_outcome"] = "invalid_input";
        return result;
    }

    std::filesystem::path normalized_source;
    std::string path_error;
    if (!::TryResolveAllowedPath(config, source_file, &normalized_source, &path_error)) {
        result.ok = false;
        result.exit_code = 32;
        result.fields["error"] = path_error;
        result.fields["semantic_outcome"] = "path_rejected";
        return result;
    }
    result.fields["normalized_path"] = normalized_source.string();
    result.fields["current_file_path"] = normalized_source.string();

    std::string candidates_json;
    std::filesystem::path formatter_path;
    std::string formatter_source;
    if (!ResolveFormatterPath(
            config,
            explicit_formatter_path,
            &formatter_path,
            &formatter_source,
            &candidates_json)) {
        result.ok = false;
        result.exit_code = 33;
        result.fields["error"] = "clang-format not found; pass clang_format_path, set CLANG_FORMAT, or install LLVM clang-format";
        result.fields["formatter_search_paths_json"] = candidates_json;
        result.fields["semantic_outcome"] = "formatter_not_found";
        result.fields["next_action"] = "install clang-format or call again with clang_format_path";
        return result;
    }
    result.fields["formatter_path"] = formatter_path.string();
    result.fields["formatter_source"] = formatter_source;
    result.fields["formatter_search_paths_json"] = candidates_json;

    std::string original_content;
    std::string io_error;
    if (!ReadFile(normalized_source, &original_content, &io_error)) {
        result.ok = false;
        result.exit_code = 34;
        result.fields["error"] = io_error;
        result.fields["semantic_outcome"] = "source_read_failed";
        return result;
    }
    result.fields["old_hash"] = StableHash(original_content);
    result.fields["source_bytes"] = std::to_string(original_content.size());

    const std::filesystem::path artifact_dir = BuildArtifactDirectory(config, normalized_source.filename().string());
    const std::filesystem::path backup_path = artifact_dir / (normalized_source.filename().string() + ".before");
    const std::filesystem::path formatted_path = artifact_dir / (normalized_source.filename().string() + ".formatted");
    if (!WriteFile(backup_path, original_content, &io_error) ||
        !WriteFile(formatted_path, original_content, &io_error)) {
        result.ok = false;
        result.exit_code = 35;
        result.fields["error"] = io_error;
        result.fields["semantic_outcome"] = "artifact_write_failed";
        return result;
    }
    result.fields["artifact_dir"] = artifact_dir.string();
    result.fields["backup_path"] = backup_path.string();
    result.fields["formatted_candidate_path"] = formatted_path.string();

    const std::string log_path = CommOperations::BuildLogPath(config, "code_format_clang_format");
    const std::string command_line = BuildFormatCommandLine(
        formatter_path,
        style.empty() ? "file" : style,
        fallback_style,
        formatted_path);
    ProcessRunResult run_result;
    std::string run_error;
    if (!RunCommandWithLog(
            command_line,
            formatted_path.parent_path().string(),
            log_path,
            timeout_sec,
            timeout_sec,
            &run_result,
            &run_error)) {
        result.ok = false;
        result.exit_code = 36;
        result.fields["error"] = run_error;
        result.fields["log_path"] = log_path;
        result.fields["semantic_outcome"] = "formatter_start_failed";
        return result;
    }
    result.fields["log_path"] = log_path;
    result.fields["format_exit_code"] = std::to_string(run_result.exit_code);
    result.fields["timed_out"] = BoolText(run_result.timed_out);
    result.fields["stalled"] = BoolText(run_result.stalled);
    result.fields["completion_reason"] = run_result.completion_reason;

    if (run_result.exit_code != 0) {
        result.ok = false;
        result.exit_code = run_result.exit_code;
        result.fields["error"] = "clang-format failed; inspect log_path";
        result.fields["semantic_outcome"] = "format_failed";
        return result;
    }

    std::string formatted_content;
    if (!ReadFile(formatted_path, &formatted_content, &io_error)) {
        result.ok = false;
        result.exit_code = 37;
        result.fields["error"] = io_error;
        result.fields["semantic_outcome"] = "formatted_read_failed";
        return result;
    }

    const bool would_change = formatted_content != original_content;
    result.fields["would_change"] = BoolText(would_change);
    result.fields["formatted_hash"] = StableHash(formatted_content);
    result.fields["formatted_bytes"] = std::to_string(formatted_content.size());

    if (output_formatted_content) {
        if (static_cast<int>(formatted_content.size()) <= max_formatted_chars) {
            result.fields["formatted_content"] = formatted_content;
        } else {
            result.fields["formatted_content_omitted"] = "true";
            result.fields["formatted_content_omit_reason"] = "formatted content exceeds max_formatted_chars";
        }
    }

    if (dry_run) {
        result.ok = true;
        result.exit_code = 0;
        result.fields["changed"] = "false";
        result.fields["semantic_outcome"] = would_change ? "would_format" : "already_formatted";
        result.fields["summary"] = would_change
            ? "clang-format would change the source file"
            : "source file is already clang-format stable";
        return result;
    }

    if (!WriteFile(normalized_source, formatted_content, &io_error)) {
        result.ok = false;
        result.exit_code = 38;
        result.fields["error"] = io_error;
        result.fields["semantic_outcome"] = "source_write_failed";
        return result;
    }

    std::string final_content;
    if (!ReadFile(normalized_source, &final_content, &io_error)) {
        result.ok = false;
        result.exit_code = 39;
        result.fields["error"] = io_error;
        result.fields["semantic_outcome"] = "source_verify_failed";
        return result;
    }
    result.ok = true;
    result.exit_code = 0;
    result.fields["changed"] = BoolText(would_change);
    result.fields["new_hash"] = StableHash(final_content);
    result.fields["semantic_outcome"] = would_change ? "formatted" : "already_formatted";
    result.fields["summary"] = would_change
        ? "source file formatted by clang-format"
        : "source file was already clang-format stable";
    return result;
}

}  // namespace codex_lan_agent
