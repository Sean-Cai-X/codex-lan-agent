#pragma once

std::string BuildJsonStringArrayFromStrings(const std::vector<std::string> & values) {
    std::ostringstream output;
    output << "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << "\"" << codex_lan_agent::JsonEscape(values[index]) << "\"";
    }
    output << "]";
    return output.str();
}

constexpr std::size_t kStructuredBodyPageByteLimit = 64 * 1024;

std::vector<std::filesystem::path> BuildDirectoryAccessHelperCandidates(const AgentConfig & config) {
#ifdef _WIN32
    const std::string executable_name = "directory_access.exe";
#else
    const std::string executable_name = "directory_access";
#endif
    const std::filesystem::path config_dir(config.config_dir);
    const std::filesystem::path workspace_root(config.workspace_root);
    return {
        config_dir / executable_name,
        config_dir / "Release" / executable_name,
        config_dir / "Debug" / executable_name,
        config_dir / "RelWithDebInfo" / executable_name,
        config_dir / "build" / executable_name,
        config_dir / "build" / "Release" / executable_name,
        config_dir / "build" / "Debug" / executable_name,
        config_dir / "build" / "RelWithDebInfo" / executable_name,
        workspace_root / "codex-lan-agent" / executable_name,
        workspace_root / "codex-lan-agent" / "Release" / executable_name,
        workspace_root / "codex-lan-agent" / "Debug" / executable_name,
        workspace_root / "codex-lan-agent" / "RelWithDebInfo" / executable_name,
        workspace_root / "codex-lan-agent" / "build" / executable_name,
        workspace_root / "codex-lan-agent" / "build" / "Release" / executable_name,
        workspace_root / "codex-lan-agent" / "build" / "Debug" / executable_name,
        workspace_root / "codex-lan-agent" / "build" / "RelWithDebInfo" / executable_name
    };
}

std::string FindDirectoryAccessHelperPath(
    const AgentConfig & config,
    std::string * searched_paths_json = nullptr) {
    const std::vector<std::filesystem::path> candidates = BuildDirectoryAccessHelperCandidates(config);
    std::ostringstream searched;
    searched << "[";
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (index != 0) {
            searched << ",";
        }
        searched << "\"" << codex_lan_agent::JsonEscape(candidates[index].string()) << "\"";
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidates[index], ec) && !ec) {
            if (searched_paths_json != nullptr) {
                searched << "]";
                *searched_paths_json = searched.str();
            }
            return candidates[index].string();
        }
    }
    searched << "]";
    if (searched_paths_json != nullptr) {
        *searched_paths_json = searched.str();
    }
    return std::string();
}

std::string ExtractHelperKvValue(const std::string & text, const std::string & key) {
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

std::string ExtractHelperContentBlock(const std::string & text) {
    const std::string begin_marker = "content_begin<<<";
    const std::string end_marker = ">>>content_end";
    const std::size_t begin = text.find(begin_marker);
    if (begin == std::string::npos) {
        return std::string();
    }
    std::size_t content_begin = begin + begin_marker.size();
    if (content_begin < text.size() && text[content_begin] == '\r') {
        ++content_begin;
    }
    if (content_begin < text.size() && text[content_begin] == '\n') {
        ++content_begin;
    }

    const std::size_t end = text.find(end_marker, content_begin);
    if (end == std::string::npos) {
        return text.substr(content_begin);
    }
    std::size_t content_end = end;
    while (content_end > content_begin &&
           (text[content_end - 1] == '\r' || text[content_end - 1] == '\n')) {
        --content_end;
    }
    return text.substr(content_begin, content_end - content_begin);
}

std::string QuoteDirectoryAccessArgument(const std::string & value) {
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted += "\"";
    return quoted;
}

CommandResult RunDirectoryAccessHelper(
    const AgentConfig & config,
    const std::string & mode,
    const std::vector<std::string> & arguments,
    const std::string & log_label) {
    CommandResult result;
    std::string searched_paths_json;
    const std::string helper_path = FindDirectoryAccessHelperPath(config, &searched_paths_json);
    result.fields["directory_access_helper_paths_json"] = searched_paths_json;
    if (helper_path.empty()) {
        result.ok = false;
        result.exit_code = 90;
        result.fields["error"] = "directory_access helper not found";
        return result;
    }

    std::ostringstream command_line;
    command_line << "\"" << helper_path << "\""
                 << " --mode \"" << mode << "\"";
    for (const std::string &argument : arguments) {
        command_line << " " << argument;
    }

    const std::string log_path = BuildLogPath(config, log_label);
    codex_lan_agent::ProcessRunResult run_result;
    std::string error_message;
    if (!codex_lan_agent::RunCommandWithLog(
            command_line.str(),
            std::string(),
            log_path,
            120,
            60,
            &run_result,
            &error_message)) {
        result.ok = false;
        result.exit_code = 91;
        result.fields["error"] = error_message;
        result.fields["log_path"] = log_path;
        return result;
    }

    std::string helper_output;
    std::string read_error;
    if (!ReadWholeFile(log_path, &helper_output, &read_error)) {
        result.ok = false;
        result.exit_code = 92;
        result.fields["error"] = read_error;
        result.fields["log_path"] = log_path;
        return result;
    }

    result.ok = run_result.exit_code == 0;
    result.exit_code = run_result.exit_code;
    result.fields["directory_access_helper_path"] = helper_path;
    result.fields["directory_access_helper_mode"] = mode;
    result.fields["directory_access_helper_log_path"] = log_path;
    result.fields["directory_access_helper_output"] = helper_output;
    result.fields["timed_out"] = run_result.timed_out ? "true" : "false";
    result.fields["stalled"] = run_result.stalled ? "true" : "false";
    return result;
}

std::string BuildDirectoryReadBatchRoot(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "directory_read_batches");
}

std::string BuildDirectoryAnalysisBundleRoot(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "directory_analysis_bundles");
}

std::string BuildDirectoryAnalysisBundlePath(
    const AgentConfig & config,
    const std::string & trace_id,
    const std::string & normalized_directory_path) {
    const std::string trace_token = !trace_id.empty()
        ? SanitizeDispatchToken(trace_id, "trace")
        : SanitizeDispatchToken(std::filesystem::path(normalized_directory_path).filename().string(), "directory");
    return codex_lan_agent::JoinPath(
        BuildDirectoryAnalysisBundleRoot(config),
        "directory_analysis_bundle_" + trace_token + ".txt");
}

bool ReadFileExcerptPreview(
    const std::filesystem::path & path,
    int max_lines,
    std::size_t max_chars,
    std::string * excerpt_text,
    int * excerpt_line_count,
    bool * truncated) {
    if (excerpt_text == nullptr || excerpt_line_count == nullptr || truncated == nullptr) {
        return false;
    }
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }

    const int bounded_max_lines = std::max(1, max_lines);
    const std::size_t bounded_max_chars = std::max<std::size_t>(256, max_chars);
    std::ostringstream excerpt;
    std::string line;
    int lines = 0;
    bool has_more = false;
    while (std::getline(input, line)) {
        if (lines >= bounded_max_lines) {
            has_more = true;
            break;
        }
        const std::string next_line = line + "\n";
        if (excerpt.tellp() > 0 &&
            static_cast<std::size_t>(excerpt.tellp()) + next_line.size() > bounded_max_chars) {
            has_more = true;
            break;
        }
        excerpt << next_line;
        ++lines;
    }

    *excerpt_text = excerpt.str();
    *excerpt_line_count = lines;
    *truncated = has_more;
    return true;
}

std::string BuildLocalMcpTraceAuditEventsPath(const AgentConfig & config) {
    return codex_lan_agent::JoinPath(config.log_root, "mcp_trace_audit_events.jsonl");
}

std::string BuildDirectoryReadManifestPath(
    const AgentConfig & config,
    const std::string & trace_id) {
    const std::string trace_token = trace_id.empty()
        ? "default"
        : SanitizeDispatchToken(trace_id, "trace");
    return codex_lan_agent::JoinPath(
        BuildDirectoryReadBatchRoot(config),
        "directory_read_manifest_" + trace_token + ".txt");
}

bool SaveDirectoryReadManifest(
    const AgentConfig & config,
    const std::string & trace_id,
    const std::string & normalized_directory_path,
    const std::vector<std::string> & file_paths,
    std::string * manifest_path) {
    const std::string path = BuildDirectoryReadManifestPath(config, trace_id);
    std::filesystem::create_directories(BuildDirectoryReadBatchRoot(config));
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output << "# directory_path=" << normalized_directory_path << "\n";
    for (const std::string & file_path : file_paths) {
        output << file_path << "\n";
    }
    if (manifest_path != nullptr) {
        *manifest_path = path;
    }
    return true;
}

std::vector<std::string> LoadDirectoryReadManifest(
    const AgentConfig & config,
    const std::string & trace_id,
    std::string * manifest_path = nullptr) {
    const std::string path = BuildDirectoryReadManifestPath(config, trace_id);
    if (manifest_path != nullptr) {
        *manifest_path = path;
    }
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::vector<std::string>();
    }
    std::vector<std::string> file_paths;
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed.rfind("#", 0) == 0) {
            continue;
        }
        file_paths.push_back(trimmed);
    }
    return file_paths;
}

std::unordered_set<std::string> LoadTraceReadFileSet(
    const AgentConfig & config,
    const std::string & trace_id) {
    std::unordered_set<std::string> read_files;
    if (trace_id.empty()) {
        return read_files;
    }
    std::string content;
    std::string read_error;
    if (!ReadWholeFile(BuildLocalMcpTraceAuditEventsPath(config), &content, &read_error)) {
        return read_files;
    }
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        if (line.find("\"trace_id\":\"" + codex_lan_agent::JsonEscape(trace_id) + "\"") == std::string::npos) {
            continue;
        }
        if (ExtractJsonString(line, "tool_name") != "lan_agent_read_text_file") {
            continue;
        }
        if (ExtractJsonString(line, "read_complete") != "true") {
            continue;
        }
        const std::string normalized_path = ExtractJsonString(line, "normalized_path");
        const std::string file_path = ExtractJsonString(line, "file_path");
        const std::string selected_path = !normalized_path.empty() ? normalized_path : file_path;
        if (!selected_path.empty()) {
            read_files.insert(selected_path);
        }
    }
    return read_files;
}

std::string LoadDirectoryReadManifestDirectory(
    const AgentConfig & config,
    const std::string & trace_id) {
    const std::string path = BuildDirectoryReadManifestPath(config, trace_id);
    std::ifstream input(path);
    if (!input.is_open()) {
        return std::string();
    }
    std::string line;
    while (std::getline(input, line)) {
        const std::string prefix = "# directory_path=";
        if (line.rfind(prefix, 0) == 0) {
            return Trim(line.substr(prefix.size()));
        }
    }
    return std::string();
}

std::string BuildCxParserDirectoryFlowParamsJson(
    const std::string & directory_path,
    const std::string & file_extensions_csv,
    int max_files,
    int max_lines_per_file,
    int file_index,
    int start_line) {
    std::ostringstream output;
    output << "{\"directory_path\":\"" << codex_lan_agent::JsonEscape(directory_path) << "\","
           << "\"file_extensions_csv\":\"" << codex_lan_agent::JsonEscape(file_extensions_csv) << "\","
           << "\"max_files\":" << max_files << ","
           << "\"max_lines_per_file\":" << max_lines_per_file << ","
           << "\"file_index\":" << file_index << ","
           << "\"start_line\":" << start_line << "}";
    return output.str();
}

std::string BuildCxParserDirectoryFlowCallJson(
    const std::string & directory_path,
    const std::string & file_extensions_csv,
    int max_files,
    int max_lines_per_file,
    int file_index,
    int start_line,
    const std::string & trace_id) {
    const std::string params_json = BuildCxParserDirectoryFlowParamsJson(
        directory_path,
        file_extensions_csv,
        max_files,
        max_lines_per_file,
        file_index,
        start_line);
    std::ostringstream output;
    output << "{\"name\":\"lan_agent_run_cxparser_flow\",\"arguments\":{"
           << "\"flow_id\":\"read_directory_files\","
           << "\"params_json\":\"" << codex_lan_agent::JsonEscape(params_json) << "\"";
    if (!trace_id.empty()) {
        output << ",\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\"";
    }
    output << "}}";
    return output.str();
}

std::string BuildDirectoryReadContinuationCallJson(
    const std::string & directory_path,
    int max_files,
    int max_lines_per_file,
    int max_files_per_call,
    int max_total_lines,
    int file_index,
    int start_line,
    const std::string & trace_id) {
    std::ostringstream output;
    output << "{\"name\":\"lan_agent_read_directory_files\",\"arguments\":{"
           << "\"directory_path\":\"" << codex_lan_agent::JsonEscape(directory_path) << "\","
           << "\"max_files\":" << std::max(1, max_files) << ","
           << "\"max_lines_per_file\":" << std::max(1, max_lines_per_file) << ","
           << "\"max_files_per_call\":" << std::max(1, max_files_per_call) << ","
           << "\"max_total_lines\":" << std::max(1, max_total_lines) << ","
           << "\"file_index\":" << std::max(0, file_index) << ","
           << "\"start_line\":" << std::max(1, start_line);
    if (!trace_id.empty()) {
        output << ",\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\"";
    }
    output << "}}";
    return output.str();
}

void ApplyDirectoryBatchProgress(
    const AgentConfig & config,
    const std::string & trace_id,
    const std::string & normalized_file_path,
    CommandResult * result) {
    if (result == nullptr || trace_id.empty()) {
        return;
    }
    std::string manifest_path;
    const std::vector<std::string> manifest_files =
        LoadDirectoryReadManifest(config, trace_id, &manifest_path);
    if (manifest_files.empty()) {
        return;
    }

    result->fields["batch_trace_id"] = trace_id;
    result->fields["batch_manifest_path"] = manifest_path;
    result->fields["batch_total_files"] = std::to_string(manifest_files.size());
    result->fields["known_file_list_complete"] = "true";
    result->fields["directory_listing_complete"] = "true";
    result->fields["batch_manifest_complete"] = "true";
    result->fields["incomplete_scope"] = "remaining_file_contents";

    const auto current_it =
        std::find(manifest_files.begin(), manifest_files.end(), normalized_file_path);
    if (current_it == manifest_files.end()) {
        result->fields["batch_membership"] = "external_to_manifest";
        return;
    }

    std::unordered_set<std::string> read_files = LoadTraceReadFileSet(config, trace_id);
    read_files.insert(normalized_file_path);

    int read_count = 0;
    std::string next_batch_file_path;
    int next_batch_file_index = -1;
    int manifest_index = 0;
    for (const std::string & file_path : manifest_files) {
        if (read_files.find(file_path) != read_files.end()) {
            ++read_count;
            ++manifest_index;
            continue;
        }
        if (next_batch_file_path.empty()) {
            next_batch_file_path = file_path;
            next_batch_file_index = manifest_index;
        }
        ++manifest_index;
    }

    const int remaining_count = static_cast<int>(manifest_files.size()) - read_count;
    const bool batch_complete = remaining_count <= 0;
    result->fields["batch_membership"] = "manifest_tracked";
    result->fields["batch_read_file_count"] = std::to_string(std::max(0, read_count));
    result->fields["remaining_batch_file_count"] = std::to_string(std::max(0, remaining_count));
    result->fields["batch_completion"] = batch_complete ? "complete" : "incomplete";
    result->fields["directory_complete"] = batch_complete ? "true" : "false";
    result->fields["next_batch_file_path"] = batch_complete ? "" : next_batch_file_path;
    result->fields["next_batch_tool_name"] = batch_complete ? "" : "lan_agent_read_directory_files";

    const bool file_read_complete = GetFieldOrDefault(*result, "read_complete", "true") == "true";
    if (!file_read_complete) {
        result->fields["batch_next_action"] = "finish paging the current file before advancing to the next file";
        return;
    }

    if (!batch_complete) {
        result->fields["task_completion"] = "incomplete";
        result->fields["analysis_allowed"] = "false";
        result->fields["continue_required"] = "true";
        result->fields["auto_continue_required"] = "true";
        result->fields["user_confirmation_required"] = "false";
        result->fields["analysis_blocked_reason"] = "directory_batch_read_incomplete";
        result->fields["partial_read_policy"] =
            "the directory file list is already complete; do not relist; continue reading remaining files from the directory batch manifest";
        result->fields["stop_condition"] = "batch_completion=complete";
        result->fields["next_tool_name"] = "lan_agent_read_directory_files";
        result->fields["next_file_path"] = next_batch_file_path;
        result->fields["next_start_line"] = "1";
        result->fields["next_max_lines"] = GetFieldOrDefault(*result, "max_lines", "500");
        result->fields["truncated"] = "true";
        result->fields["result"] = "directory_batch_partial";
        result->fields["outcome_hint"] = "PARTIAL";
        result->fields["next_action"] = "directory file list is complete; read next_batch_file_path from the same manifest before any conclusion";
        result->fields["batch_next_action"] = "do not relist the directory; continue reading remaining files from the directory batch manifest";
        const std::string manifest_directory = LoadDirectoryReadManifestDirectory(config, trace_id);
        const std::string directory_for_flow = manifest_directory.empty()
            ? std::filesystem::path(next_batch_file_path).parent_path().string()
            : manifest_directory;
        result->fields["next_call_json"] = BuildDirectoryReadContinuationCallJson(
            directory_for_flow,
            static_cast<int>(manifest_files.size()),
            std::max(1, std::atoi(GetFieldOrDefault(*result, "max_lines", "500").c_str())),
            5,
            2500,
            std::max(0, next_batch_file_index),
            1,
            trace_id);
    } else {
        result->fields["task_completion"] = "complete";
        result->fields["analysis_allowed"] = "true";
        result->fields["continue_required"] = "false";
        result->fields["auto_continue_required"] = "false";
        result->fields["user_confirmation_required"] = "false";
        result->fields["analysis_blocked_reason"] = "";
        result->fields["truncated"] = "false";
        result->fields["result"] = "directory_batch_complete";
        result->fields["outcome_hint"] = "PASS";
        result->fields["next_action"] = "all files in the directory batch were read; analysis is allowed";
        result->fields["next_tool_name"] = "";
        result->fields["next_file_path"] = "";
        result->fields["next_start_line"] = "";
        result->fields["next_max_lines"] = "";
        result->fields["next_call_json"] = "";
        result->fields["required_tool_name"] = "";
        result->fields["required_tool_arguments_json"] = "";
        result->fields["batch_next_action"] = "directory batch is fully read";
        result->fields["content_read_completion"] = "complete";
        result->fields["continuation_status"] = "complete";
        result->fields["incomplete_scope"] = "";
    }
}

bool IsStructuredBodyReadCandidate(const std::filesystem::path & normalized_path) {
    const std::string extension = ToLowerAscii(normalized_path.extension().string());
    if (extension != ".json" && extension != ".jsonl") {
        return false;
    }
    const std::string path_text = ToLowerAscii(normalized_path.string());
    return path_text.find("rag_index") != std::string::npos
        || path_text.find("\\build\\") != std::string::npos
        || path_text.find("/build/") != std::string::npos;
}

std::string StructuredPayloadFormatForPath(const std::filesystem::path & normalized_path) {
    const std::string extension = ToLowerAscii(normalized_path.extension().string());
    if (extension == ".json") {
        return "json";
    }
    if (extension == ".jsonl") {
        return "jsonl";
    }
    return "plain_text";
}

CommandResult ReadTextFileResult(
    const AgentConfig & config,
    const std::string & file_path,
    int max_lines = 500,
    int start_line = 1,
    const std::string & trace_id = std::string(),
    std::size_t start_byte_offset = 0) {
    CommandResult result;
    result.fields["task_type"] = "file_read";
    result.fields["file_path"] = file_path;
    if (!trace_id.empty()) {
        result.fields["trace_id"] = trace_id;
    }

    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 20;
        result.fields["error"] = "file_path is required";
        return result;
    }

    std::filesystem::path requested(file_path);
    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, requested.string(), &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 21;
        result.fields["error"] = path_error;
        return result;
    }

    const bool structured_body_read = IsStructuredBodyReadCandidate(normalized);
    result.fields["structured_body_read_mode"] = structured_body_read ? "native_structured_body" : "disabled";
    result.fields["structured_body_helper_bypassed"] = structured_body_read ? "true" : "false";
    result.fields["pagination_basis"] = "line_based";
    result.fields["start_byte_offset"] = std::to_string(start_byte_offset);
    result.fields["effective_page_byte_limit"] = std::to_string(kStructuredBodyPageByteLimit);

    if (!structured_body_read) {
        const CommandResult helper_result = RunDirectoryAccessHelper(
            config,
            "read-file-page",
            {
                "--file " + QuoteDirectoryAccessArgument(normalized.string()),
                "--start-line " + std::to_string(start_line > 0 ? start_line : 1),
                "--max-lines " + std::to_string(max_lines > 0 ? max_lines : 500)
            },
            "directory_access_read_file_page");
        if (helper_result.ok) {
            const int bounded_start_line = start_line > 0 ? start_line : 1;
            const int bounded_max_lines = max_lines > 0 ? max_lines : 500;
            const int line_count = std::max(0, std::atoi(ExtractHelperKvValue(
                helper_result.fields.at("directory_access_helper_output"), "line_count").c_str()));
            const int total_lines = std::max(0, std::atoi(ExtractHelperKvValue(
                helper_result.fields.at("directory_access_helper_output"), "total_lines").c_str()));
            const int end_line = std::max(0, std::atoi(ExtractHelperKvValue(
                helper_result.fields.at("directory_access_helper_output"), "end_line").c_str()));
            const bool has_more = ExtractHelperKvValue(
                helper_result.fields.at("directory_access_helper_output"), "has_more") == "true";
            const bool read_complete = !has_more;
            const int next_start_line = bounded_start_line + line_count;
            const int page_index = ((bounded_start_line - 1) / bounded_max_lines) + 1;
            const int page_count = std::max(1, (total_lines + bounded_max_lines - 1) / bounded_max_lines);

            CommandResult result_from_helper;
            result_from_helper.ok = true;
            result_from_helper.exit_code = 0;
            result_from_helper.fields["task_type"] = "file_read";
            result_from_helper.fields["file_path"] = file_path;
            if (!trace_id.empty()) {
                result_from_helper.fields["trace_id"] = trace_id;
            }
            result_from_helper.fields["normalized_path"] = normalized.string();
            result_from_helper.fields["current_file_path"] = normalized.string();
            result_from_helper.fields["start_line"] = std::to_string(bounded_start_line);
            result_from_helper.fields["end_line"] = line_count > 0 ? std::to_string(end_line) : "";
            result_from_helper.fields["line_count"] = std::to_string(line_count);
            result_from_helper.fields["returned_lines"] = std::to_string(line_count);
            result_from_helper.fields["total_lines"] = std::to_string(total_lines);
            result_from_helper.fields["remaining_lines"] = has_more
                ? std::to_string(std::max(0, total_lines - end_line))
                : "0";
            result_from_helper.fields["max_lines"] = std::to_string(bounded_max_lines);
            result_from_helper.fields["next_start_line"] = has_more ? std::to_string(next_start_line) : "";
            result_from_helper.fields["has_more"] = has_more ? "true" : "false";
            result_from_helper.fields["read_complete"] = read_complete ? "true" : "false";
            result_from_helper.fields["file_complete"] = read_complete ? "true" : "false";
            result_from_helper.fields["read_status"] = read_complete ? "complete" : "partial";
            result_from_helper.fields["task_completion"] = read_complete ? "complete" : "incomplete";
            result_from_helper.fields["page_status"] = has_more ? "partial_page" : "final_page";
            result_from_helper.fields["page_index"] = std::to_string(page_index);
            result_from_helper.fields["page_count"] = std::to_string(page_count);
            result_from_helper.fields["requires_followup"] = has_more ? "true" : "false";
            result_from_helper.fields["file_bytes"] = "0";
            result_from_helper.fields["content"] = ExtractHelperContentBlock(
                helper_result.fields.at("directory_access_helper_output"));
            result_from_helper.fields["content_text"] = result_from_helper.fields["content"];
            result_from_helper.fields["content_begin_marker"] = "content_begin<<<";
            result_from_helper.fields["content_end_marker"] = ">>>content_end";
            result_from_helper.fields["content_payload_format"] = StructuredPayloadFormatForPath(normalized);
            result_from_helper.fields["pagination_basis"] = "line_based";
            result_from_helper.fields["start_byte_offset"] = "0";
            result_from_helper.fields["returned_bytes"] =
                std::to_string(result_from_helper.fields["content"].size());
            result_from_helper.fields["total_bytes"] = "0";
            result_from_helper.fields["remaining_bytes"] = "0";
            result_from_helper.fields["next_byte_offset"] = "";
            result_from_helper.fields["effective_page_byte_limit"] =
                std::to_string(kStructuredBodyPageByteLimit);
            result_from_helper.fields["directory_access_helper_used"] = "true";
            result_from_helper.fields["directory_access_helper_path"] =
                GetFieldOrDefault(helper_result, "directory_access_helper_path", "");
            result_from_helper.fields["directory_access_helper_log_path"] =
                GetFieldOrDefault(helper_result, "directory_access_helper_log_path", "");
            result_from_helper.fields["result_ref"] =
                result_from_helper.fields["directory_access_helper_log_path"];
            result_from_helper.fields["evidence_ref"] =
                result_from_helper.fields["directory_access_helper_log_path"];
            result_from_helper.fields["structured_body_read_mode"] = "helper_line_page";
            result_from_helper.fields["structured_body_helper_bypassed"] = "false";
            if (has_more) {
            result_from_helper.fields["continue_required"] = "true";
            result_from_helper.fields["auto_continue_required"] = "true";
            result_from_helper.fields["user_confirmation_required"] = "false";
            result_from_helper.fields["analysis_allowed"] = "false";
            result_from_helper.fields["analysis_blocked_reason"] = "file_read_incomplete";
            result_from_helper.fields["result"] = "file_read_partial";
            result_from_helper.fields["outcome_hint"] = "PARTIAL";
            result_from_helper.fields["next_action"] = "continue reading the next page of the same file before concluding";
            result_from_helper.fields["next_tool_name"] = "lan_agent_read_text_file";
            result_from_helper.fields["next_call_json"] =
                "{\"name\":\"lan_agent_read_text_file\",\"arguments\":{\"file_path\":\""
                + codex_lan_agent::JsonEscape(file_path)
                + "\",\"max_lines\":" + std::to_string(bounded_max_lines)
                + ",\"start_line\":" + std::to_string(next_start_line)
                + (trace_id.empty() ? std::string() : ",\"trace_id\":\"" + codex_lan_agent::JsonEscape(trace_id) + "\"")
                + "}}";
            } else {
            result_from_helper.fields["continue_required"] = "false";
            result_from_helper.fields["auto_continue_required"] = "false";
            result_from_helper.fields["user_confirmation_required"] = "false";
            result_from_helper.fields["analysis_allowed"] = "true";
            result_from_helper.fields["analysis_blocked_reason"] = "";
            result_from_helper.fields["result"] = "file_read_complete";
            result_from_helper.fields["outcome_hint"] = "PASS";
            result_from_helper.fields["next_action"] = "file page read is complete";
            result_from_helper.fields["next_tool_name"] = "";
            result_from_helper.fields["next_call_json"] = "";
            }
            return result_from_helper;
        }
    }

    if (structured_body_read) {
        std::string raw_content;
        std::string read_error;
        if (!ReadWholeFile(normalized, &raw_content, &read_error)) {
            result.ok = false;
            result.exit_code = 23;
            result.fields["error"] = read_error;
            return result;
        }

        const std::size_t total_bytes = raw_content.size();
        const std::size_t bounded_offset = std::min(start_byte_offset, total_bytes);
        const bool use_byte_chunk_paging =
            bounded_offset > 0 || total_bytes > kStructuredBodyPageByteLimit;
        if (use_byte_chunk_paging) {
            const std::size_t returned_bytes =
                std::min(kStructuredBodyPageByteLimit, total_bytes - bounded_offset);
            const std::size_t next_byte_offset = bounded_offset + returned_bytes;
            const bool has_more = next_byte_offset < total_bytes;
            const bool read_complete = !has_more;
            const std::string chunk = raw_content.substr(bounded_offset, returned_bytes);

            const int total_lines = raw_content.empty()
                ? 0
                : static_cast<int>(std::count(raw_content.begin(), raw_content.end(), '\n')) + 1;
            const int prefix_line_count = bounded_offset == 0
                ? 0
                : static_cast<int>(std::count(raw_content.begin(), raw_content.begin() + bounded_offset, '\n'));
            const int chunk_newline_count =
                static_cast<int>(std::count(chunk.begin(), chunk.end(), '\n'));
            const int estimated_start_line = total_lines == 0 ? 1 : prefix_line_count + 1;
            const int estimated_end_line = chunk.empty()
                ? 0
                : (estimated_start_line + chunk_newline_count);
            const int estimated_line_count = chunk.empty()
                ? 0
                : std::max(1, estimated_end_line - estimated_start_line + 1);

            result.fields["normalized_path"] = normalized.string();
            result.fields["current_file_path"] = normalized.string();
            result.fields["start_line"] = std::to_string(estimated_start_line);
            result.fields["end_line"] = chunk.empty() ? "" : std::to_string(estimated_end_line);
            result.fields["line_count"] = std::to_string(estimated_line_count);
            result.fields["returned_lines"] = std::to_string(estimated_line_count);
            result.fields["total_lines"] = std::to_string(total_lines);
            result.fields["remaining_lines"] = has_more ? "1" : "0";
            result.fields["max_lines"] = std::to_string(max_lines > 0 ? max_lines : 500);
            result.fields["next_start_line"] = "";
            result.fields["has_more"] = has_more ? "true" : "false";
            result.fields["read_complete"] = read_complete ? "true" : "false";
            result.fields["file_complete"] = read_complete ? "true" : "false";
            result.fields["read_status"] = read_complete ? "complete" : "partial";
            result.fields["task_completion"] = read_complete ? "complete" : "incomplete";
            result.fields["page_status"] = has_more ? "partial_page" : "final_page";
            result.fields["page_index"] = std::to_string(
                static_cast<int>(bounded_offset / kStructuredBodyPageByteLimit) + 1);
            result.fields["page_count"] = std::to_string(
                static_cast<int>((total_bytes + kStructuredBodyPageByteLimit - 1) / kStructuredBodyPageByteLimit));
            result.fields["requires_followup"] = has_more ? "true" : "false";
            result.fields["continue_required"] = has_more ? "true" : "false";
            result.fields["auto_continue_required"] = has_more ? "true" : "false";
            result.fields["user_confirmation_required"] = "false";
            result.fields["analysis_allowed"] = read_complete ? "true" : "false";
            result.fields["analysis_blocked_reason"] = has_more ? "file_read_incomplete" : "";
            result.fields["partial_read_policy"] = has_more
                ? "structured body is chunk-paged by byte offset; continue with next_call_json until read_complete=true"
                : "structured body read is complete; analysis is allowed";
            result.fields["stop_condition"] = "read_complete=true";
            result.fields["next_tool_name"] = has_more ? "lan_agent_read_text_file" : "";
            result.fields["next_file_path"] = has_more ? file_path : "";
            result.fields["next_max_lines"] = has_more ? std::to_string(max_lines > 0 ? max_lines : 500) : "";
            result.fields["truncated"] = has_more ? "true" : "false";
            result.fields["file_bytes"] = std::to_string(total_bytes);
            result.fields["total_bytes"] = std::to_string(total_bytes);
            result.fields["returned_bytes"] = std::to_string(returned_bytes);
            result.fields["remaining_bytes"] =
                has_more ? std::to_string(total_bytes - next_byte_offset) : "0";
            result.fields["next_byte_offset"] = has_more ? std::to_string(next_byte_offset) : "";
            result.fields["read_mode"] = "structured_body_byte_page";
            result.fields["read_contract"] =
                "repeat lan_agent_read_text_file with start_byte_offset=next_byte_offset until has_more=false";
            result.fields["completion_rule"] = "do not claim the file is fully read unless read_complete=true";
            result.fields["result"] = has_more ? "partial_read" : "complete_read";
            result.fields["outcome_hint"] = has_more ? "PARTIAL" : "PASS";
            result.fields["next_action"] = has_more
                ? "continue reading with next_byte_offset"
                : "file read is complete";
            result.fields["content"] = chunk;
            result.fields["content_text"] = chunk;
            result.fields["content_payload_format"] = StructuredPayloadFormatForPath(normalized);
            result.fields["content_payload_scope"] = has_more ? "file_body_chunk" : "file_body_only";
            result.fields["content_payload_boundary_safe"] = "true";
            result.fields["structured_body_read_mode"] = "native_structured_body_byte_page";
            result.fields["structured_body_helper_bypassed"] = "true";
            result.fields["pagination_basis"] = "byte_offset_for_structured_body";
            result.fields["single_line_payload_warning"] =
                (total_lines <= 1 && total_bytes > kStructuredBodyPageByteLimit) ? "true" : "false";
            result.fields["next_call_json"] = has_more
                ? ("{\"name\":\"lan_agent_read_text_file\",\"arguments\":{\"file_path\":\""
                    + codex_lan_agent::JsonEscape(file_path)
                    + "\",\"start_byte_offset\":"
                    + std::to_string(next_byte_offset)
                    + ",\"max_lines\":"
                    + std::to_string(max_lines > 0 ? max_lines : 500)
                    + ",\"start_line\":1"
                    + (trace_id.empty() ? std::string() : ",\"trace_id\":\"" + codex_lan_agent::JsonEscape(trace_id) + "\"")
                    + "}}")
                : "";
            result.fields["required_tool_name"] = has_more ? "lan_agent_read_text_file" : "";
            result.fields["required_tool_arguments_json"] = result.fields["next_call_json"];
            return result;
        }
    }

    std::ifstream input(normalized);
    if (!input.is_open()) {
        result.ok = false;
        result.exit_code = 23;
        result.fields["error"] = "failed to open file";
        return result;
    }

    const int bounded_start_line = start_line > 0 ? start_line : 1;
    const int bounded_max_lines = max_lines > 0 ? max_lines : 500;
    std::uintmax_t file_bytes = 0;
    std::error_code size_ec;
    file_bytes = std::filesystem::file_size(normalized, size_ec);
    if (size_ec) {
        file_bytes = 0;
    }

    std::ostringstream content;
    std::string line;
    int current_line = 0;
    int line_count = 0;
    int last_line_read = 0;
    while (std::getline(input, line)) {
        ++current_line;
        if (current_line < bounded_start_line) {
            continue;
        }
        if (line_count >= bounded_max_lines) {
            continue;
        }
        content << line << "\n";
        ++line_count;
        last_line_read = current_line;
    }
    const int total_lines = current_line;
    const int next_start_line = bounded_start_line + line_count;
    const bool has_more = line_count > 0 && last_line_read < total_lines;
    const bool read_complete = !has_more;
    const int page_index = bounded_max_lines > 0
        ? ((bounded_start_line - 1) / bounded_max_lines) + 1
        : 1;
    const int page_count = bounded_max_lines > 0
        ? std::max(1, (total_lines + bounded_max_lines - 1) / bounded_max_lines)
        : 1;

    result.fields["normalized_path"] = normalized.string();
    result.fields["current_file_path"] = normalized.string();
    result.fields["start_line"] = std::to_string(bounded_start_line);
    result.fields["end_line"] = line_count > 0 ? std::to_string(last_line_read) : "";
    result.fields["line_count"] = std::to_string(line_count);
    result.fields["returned_lines"] = std::to_string(line_count);
    result.fields["total_lines"] = std::to_string(total_lines);
    result.fields["remaining_lines"] = has_more
        ? std::to_string(std::max(0, total_lines - last_line_read))
        : "0";
    result.fields["max_lines"] = std::to_string(bounded_max_lines);
    result.fields["next_start_line"] =
        has_more ? std::to_string(next_start_line) : "";
    result.fields["has_more"] = has_more ? "true" : "false";
    result.fields["read_complete"] = read_complete ? "true" : "false";
    result.fields["file_complete"] = read_complete ? "true" : "false";
    result.fields["read_status"] = read_complete ? "complete" : "partial";
    result.fields["task_completion"] = read_complete ? "complete" : "incomplete";
    result.fields["page_status"] = has_more ? "partial_page" : "final_page";
    result.fields["page_index"] = std::to_string(page_index);
    result.fields["page_count"] = std::to_string(page_count);
    result.fields["requires_followup"] = has_more ? "true" : "false";
    result.fields["continue_required"] = has_more ? "true" : "false";
    result.fields["auto_continue_required"] = has_more ? "true" : "false";
    result.fields["user_confirmation_required"] = "false";
    result.fields["analysis_allowed"] = read_complete ? "true" : "false";
    result.fields["analysis_blocked_reason"] = has_more ? "file_read_incomplete" : "";
    result.fields["partial_read_policy"] = has_more
        ? "do not ask user whether to continue; automatically call next_call_json until read_complete=true unless the user explicitly requested a partial range"
        : "file read is complete; analysis is allowed";
    result.fields["stop_condition"] = "read_complete=true";
    result.fields["next_tool_name"] = has_more ? "lan_agent_read_text_file" : "";
    result.fields["next_file_path"] = has_more ? file_path : "";
    result.fields["next_max_lines"] = has_more ? std::to_string(bounded_max_lines) : "";
    result.fields["truncated"] = has_more ? "true" : "false";
    result.fields["file_bytes"] = std::to_string(file_bytes);
    result.fields["read_mode"] = "paged_lines";
    result.fields["read_contract"] = "repeat lan_agent_read_text_file with start_line=next_start_line until has_more=false";
    result.fields["completion_rule"] = "do not claim the file is fully read unless read_complete=true";
    result.fields["result"] = has_more ? "partial_read" : "complete_read";
    result.fields["outcome_hint"] = has_more ? "PARTIAL" : "PASS";
    result.fields["next_action"] = has_more
        ? "continue reading with next_start_line"
        : "file read is complete";
    result.fields["next_call_json"] = has_more
        ? ("{\"name\":\"lan_agent_read_text_file\",\"arguments\":{\"file_path\":\""
            + codex_lan_agent::JsonEscape(file_path)
            + "\",\"start_line\":"
            + std::to_string(next_start_line)
            + ",\"max_lines\":"
            + std::to_string(bounded_max_lines)
            + (trace_id.empty() ? std::string() : ",\"trace_id\":\"" + codex_lan_agent::JsonEscape(trace_id) + "\"")
            + "}}")
        : "";
    result.fields["required_tool_name"] = has_more ? "lan_agent_read_text_file" : "";
    result.fields["required_tool_arguments_json"] = result.fields["next_call_json"];
    result.fields["content"] = content.str();
    result.fields["content_text"] = result.fields["content"];
    result.fields["content_payload_format"] = StructuredPayloadFormatForPath(normalized);
    result.fields["content_payload_scope"] = "file_body_only";
    result.fields["content_payload_boundary_safe"] = "true";
    result.fields["returned_bytes"] = std::to_string(result.fields["content"].size());
    result.fields["total_bytes"] = std::to_string(file_bytes);
    result.fields["remaining_bytes"] = "0";
    result.fields["next_byte_offset"] = "";
    ApplyDirectoryBatchProgress(config, trace_id, normalized.string(), &result);
    return result;
}

CommandResult TailTextFileResult(
    const AgentConfig & config,
    const std::string & file_path,
    int max_lines = 120) {
    CommandResult result;
    result.fields["file_path"] = file_path;

    if (file_path.empty()) {
        result.ok = false;
        result.exit_code = 24;
        result.fields["error"] = "file_path is required";
        return result;
    }

    std::filesystem::path requested(file_path);
    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, requested.string(), &normalized, &path_error)) {
        result.ok = false;
        result.exit_code = 25;
        result.fields["error"] = path_error;
        return result;
    }

    std::ifstream input(normalized);
    if (!input.is_open()) {
        result.ok = false;
        result.exit_code = 27;
        result.fields["error"] = "failed to open file";
        return result;
    }

    std::deque<std::string> tail_lines;
    std::string line;
    int total_lines = 0;
    const int bounded_max_lines = max_lines > 0 ? max_lines : 1;
    while (std::getline(input, line)) {
        if (static_cast<int>(tail_lines.size()) >= bounded_max_lines) {
            tail_lines.pop_front();
        }
        tail_lines.push_back(line);
        ++total_lines;
    }

    std::ostringstream content;
    for (const std::string & tail_line : tail_lines) {
        content << tail_line << "\n";
    }

    result.fields["normalized_path"] = normalized.string();
    result.fields["line_count"] = std::to_string(static_cast<int>(tail_lines.size()));
    result.fields["total_lines"] = std::to_string(total_lines);
    result.fields["content"] = content.str();
    return result;
}

CommandResult ListDirectoryResult(
    const AgentConfig & config,
    const std::string & directory_path,
    int max_entries = 200,
    const std::string & trace_id = std::string()) {
    CommandResult result;
    result.fields["task_type"] = "directory_list";
    result.fields["directory_path"] = directory_path;
    if (!trace_id.empty()) {
        result.fields["trace_id"] = trace_id;
    }

    if (directory_path.empty()) {
        result.ok = false;
        result.exit_code = 30;
        result.fields["error"] = "directory_path is required";
        return result;
    }

    std::filesystem::path requested(directory_path);
    std::error_code ec;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(requested, ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 31;
        result.fields["error"] = "failed to normalize directory path";
        return result;
    }

    const std::filesystem::path logs_root = std::filesystem::path(config.log_root);
    const std::filesystem::path workspace_root = std::filesystem::path(config.workspace_root);
    if (!StartsWithPath(normalized, logs_root) && !StartsWithPath(normalized, workspace_root)) {
        result.ok = false;
        result.exit_code = 32;
        result.fields["error"] = "directory is outside allowed roots";
        return result;
    }

    if (!std::filesystem::exists(normalized)) {
        result.ok = false;
        result.exit_code = 33;
        result.fields["error"] = "directory does not exist";
        return result;
    }

    if (!std::filesystem::is_directory(normalized)) {
        result.ok = false;
        result.exit_code = 34;
        result.fields["error"] = "path is not a directory";
        return result;
    }

    const CommandResult helper_result = RunDirectoryAccessHelper(
        config,
        "list-directory",
        {
            "--directory " + QuoteDirectoryAccessArgument(normalized.string()),
            "--max-entries " + std::to_string(max_entries > 0 ? max_entries : 200)
        },
        "directory_access_list_directory");
    if (helper_result.ok) {
        std::vector<std::string> manifest_files;
        std::vector<std::string> preview_files;
        std::vector<std::string> file_names;
        std::vector<std::string> preview_file_names;
        std::vector<std::string> all_entry_labels;
        const std::string helper_output =
            GetFieldOrDefault(helper_result, "directory_access_helper_output", "");
        const int total_entries = std::max(0, std::atoi(ExtractHelperKvValue(helper_output, "total_entries").c_str()));
        const int returned_count = std::max(0, std::atoi(ExtractHelperKvValue(helper_output, "returned_count").c_str()));
        const int file_count = std::max(0, std::atoi(ExtractHelperKvValue(helper_output, "file_count").c_str()));
        const int directory_count = std::max(0, std::atoi(ExtractHelperKvValue(helper_output, "directory_count").c_str()));
        for (int index = 0; index < returned_count; ++index) {
            const std::string label = ExtractHelperKvValue(helper_output, "entry_" + std::to_string(index));
            if (!label.empty()) {
                result.fields["entry_" + std::to_string(index)] = label;
                all_entry_labels.push_back(label);
            }
        }
        for (int index = 0; index < file_count; ++index) {
            const std::string path = ExtractHelperKvValue(helper_output, "file_path_" + std::to_string(index));
            const std::string name = ExtractHelperKvValue(helper_output, "file_name_" + std::to_string(index));
            if (!path.empty()) {
                manifest_files.push_back(path);
                if (static_cast<int>(preview_files.size()) < returned_count) {
                    preview_files.push_back(path);
                }
            }
            if (!name.empty()) {
                file_names.push_back(name);
                if (static_cast<int>(preview_file_names.size()) < returned_count) {
                    preview_file_names.push_back(name);
                }
            }
        }

        result.ok = true;
        result.exit_code = 0;
        result.fields["normalized_path"] = normalized.string();
        result.fields["entry_count"] = std::to_string(returned_count);
        result.fields["returned_count"] = std::to_string(returned_count);
        result.fields["total_entries"] = std::to_string(total_entries);
        result.fields["file_count"] = std::to_string(file_count);
        result.fields["directory_count"] = std::to_string(directory_count);
        result.fields["entry_labels_json"] = BuildJsonStringArrayFromStrings(all_entry_labels);
        result.fields["file_paths_json"] = BuildJsonStringArrayFromStrings(preview_files);
        result.fields["file_names_json"] = BuildJsonStringArrayFromStrings(preview_file_names);
        result.fields["file_paths_total_count"] = std::to_string(manifest_files.size());
        result.fields["file_names_total_count"] = std::to_string(file_names.size());
        result.fields["response_preview_truncated"] =
            (manifest_files.size() > preview_files.size() || file_names.size() > preview_file_names.size())
                ? "true"
                : "false";
        result.fields["remaining_entries"] = std::to_string(std::max(0, total_entries - returned_count));
        result.fields["read_mode"] = "directory_manifest";
        result.fields["directory_listing_complete"] = "true";
        result.fields["known_file_list_complete"] = "true";
        result.fields["directory_access_helper_used"] = "true";
        result.fields["directory_access_helper_path"] =
            GetFieldOrDefault(helper_result, "directory_access_helper_path", "");
        result.fields["directory_access_helper_log_path"] =
            GetFieldOrDefault(helper_result, "directory_access_helper_log_path", "");
        result.fields["result_ref"] = result.fields["directory_access_helper_log_path"];
        result.fields["evidence_ref"] = result.fields["directory_access_helper_log_path"];

        std::string manifest_path;
        const bool manifest_saved =
            !trace_id.empty() && SaveDirectoryReadManifest(config, trace_id, normalized.string(), manifest_files, &manifest_path);
        result.fields["batch_manifest_path"] = manifest_saved ? manifest_path : "";
        result.fields["batch_manifest_ready"] = manifest_saved ? "true" : "false";
        result.fields["batch_manifest_complete"] = manifest_saved ? "true" : "false";
        result.fields["batch_total_files"] = std::to_string(manifest_files.size());
        const bool can_continue_directory_batch = manifest_saved && !manifest_files.empty();
        result.fields["batch_read_file_count"] = "0";
        result.fields["remaining_batch_file_count"] = can_continue_directory_batch
            ? std::to_string(manifest_files.size())
            : "0";
        result.fields["batch_completion"] = can_continue_directory_batch ? "incomplete" : "complete";
        result.fields["content_read_completion"] = can_continue_directory_batch ? "incomplete" : "complete";
        result.fields["incomplete_scope"] = can_continue_directory_batch ? "remaining_file_contents" : "";
        result.fields["current_file_path"] = "";
        result.fields["current_file_index"] = "";
        result.fields["next_batch_file_path"] = can_continue_directory_batch ? manifest_files.front() : "";
        result.fields["next_batch_tool_name"] = can_continue_directory_batch ? "lan_agent_read_directory_files" : "";
        result.fields["continue_required"] = can_continue_directory_batch ? "true" : "false";
        result.fields["auto_continue_required"] = can_continue_directory_batch ? "true" : "false";
        result.fields["user_confirmation_required"] = "false";
        result.fields["analysis_allowed"] = can_continue_directory_batch ? "false" : "true";
        result.fields["analysis_blocked_reason"] = can_continue_directory_batch ? "directory_batch_read_incomplete" : "";
        result.fields["task_completion"] = can_continue_directory_batch ? "incomplete" : "complete";
        result.fields["partial_read_policy"] = can_continue_directory_batch
            ? "directory listing is complete; continue with lan_agent_read_directory_files until batch_completion=complete"
            : "directory listing is complete; no implicit file read continuation is emitted";
        result.fields["stop_condition"] = can_continue_directory_batch
            ? "batch_completion=complete"
            : "directory_listing_complete=true";
        result.fields["result"] = can_continue_directory_batch ? "directory_list_manifest_ready" : "directory_list_complete";
        result.fields["outcome_hint"] = can_continue_directory_batch ? "PARTIAL" : "PASS";
        result.fields["next_action"] = can_continue_directory_batch
            ? "directory listing is complete; continue reading files from the generated manifest before any conclusion"
            : "directory listing is complete";
        result.fields["next_tool_name"] = can_continue_directory_batch ? "lan_agent_read_directory_files" : "";
        result.fields["next_file_path"] = can_continue_directory_batch ? manifest_files.front() : "";
        result.fields["next_start_line"] = can_continue_directory_batch ? "1" : "";
        result.fields["next_max_lines"] = can_continue_directory_batch ? "500" : "";
        result.fields["truncated"] = can_continue_directory_batch ? "true" : "false";
        result.fields["next_call_json"] = can_continue_directory_batch
            ? BuildDirectoryReadContinuationCallJson(
                normalized.string(),
                static_cast<int>(manifest_files.size()),
                500,
                5,
                2500,
                0,
                1,
                trace_id)
            : "";
        result.fields["required_tool_name"] = can_continue_directory_batch ? "lan_agent_read_directory_files" : "";
        result.fields["required_tool_arguments_json"] = result.fields["next_call_json"];
        return result;
    }

    std::vector<std::filesystem::directory_entry> all_entries;
    std::vector<std::string> manifest_files;
    std::vector<std::string> preview_files;
    std::vector<std::string> all_entry_labels;
    std::vector<std::string> file_names;
    std::vector<std::string> preview_file_names;
    for (const auto & entry : std::filesystem::directory_iterator(normalized)) {
        all_entries.push_back(entry);
        if (entry.is_regular_file()) {
            manifest_files.push_back(entry.path().string());
            file_names.push_back(entry.path().filename().string());
        }
    }
    std::sort(
        all_entries.begin(),
        all_entries.end(),
        [](const std::filesystem::directory_entry & left, const std::filesystem::directory_entry & right) {
            return ToLowerAscii(left.path().filename().string()) < ToLowerAscii(right.path().filename().string());
        });
    std::sort(
        manifest_files.begin(),
        manifest_files.end(),
        [](const std::string & left, const std::string & right) {
            return ToLowerAscii(std::filesystem::path(left).filename().string())
                < ToLowerAscii(std::filesystem::path(right).filename().string());
        });

    int index = 0;
    for (const auto & entry : all_entries) {
        const std::string type = entry.is_directory() ? "[dir] " : "[file] ";
        if (index >= max_entries) {
            break;
        }
        all_entry_labels.push_back(type + entry.path().filename().string());
        const std::string key = "entry_" + std::to_string(index);
        result.fields[key] = type + entry.path().filename().string();
        if (entry.is_regular_file()) {
            preview_files.push_back(entry.path().string());
            preview_file_names.push_back(entry.path().filename().string());
        }
        ++index;
    }

    result.fields["normalized_path"] = normalized.string();
    result.fields["entry_count"] = std::to_string(index);
    result.fields["returned_count"] = std::to_string(index);
    result.fields["total_entries"] = std::to_string(all_entries.size());
    result.fields["file_count"] = std::to_string(manifest_files.size());
    result.fields["directory_count"] = std::to_string(static_cast<int>(all_entries.size()) - static_cast<int>(manifest_files.size()));
    result.fields["entry_labels_json"] = BuildJsonStringArrayFromStrings(all_entry_labels);
    result.fields["file_paths_json"] = BuildJsonStringArrayFromStrings(preview_files);
    result.fields["file_names_json"] = BuildJsonStringArrayFromStrings(preview_file_names);
    result.fields["file_paths_total_count"] = std::to_string(manifest_files.size());
    result.fields["file_names_total_count"] = std::to_string(file_names.size());
    result.fields["response_preview_truncated"] =
        (manifest_files.size() > preview_files.size() || file_names.size() > preview_file_names.size())
            ? "true"
            : "false";
    result.fields["remaining_entries"] =
        std::to_string(std::max(0, static_cast<int>(all_entries.size()) - index));
    result.fields["read_mode"] = "directory_manifest";
    result.fields["directory_listing_complete"] = "true";
    result.fields["known_file_list_complete"] = "true";

    std::string manifest_path;
    const bool manifest_saved =
        !trace_id.empty() && SaveDirectoryReadManifest(config, trace_id, normalized.string(), manifest_files, &manifest_path);
    result.fields["batch_manifest_path"] = manifest_saved ? manifest_path : "";
    result.fields["batch_manifest_ready"] = manifest_saved ? "true" : "false";
    result.fields["batch_manifest_complete"] = manifest_saved ? "true" : "false";
    result.fields["batch_total_files"] = std::to_string(manifest_files.size());
    const bool can_continue_directory_batch = manifest_saved && !manifest_files.empty();
    result.fields["batch_read_file_count"] = "0";
    result.fields["remaining_batch_file_count"] = can_continue_directory_batch
        ? std::to_string(manifest_files.size())
        : "0";
    result.fields["batch_completion"] = can_continue_directory_batch ? "incomplete" : "complete";
    result.fields["content_read_completion"] = can_continue_directory_batch ? "incomplete" : "complete";
    result.fields["incomplete_scope"] = can_continue_directory_batch ? "remaining_file_contents" : "";
    result.fields["current_file_path"] = "";
    result.fields["current_file_index"] = "";
    result.fields["next_batch_file_path"] = can_continue_directory_batch ? manifest_files.front() : "";
    result.fields["next_batch_tool_name"] = can_continue_directory_batch ? "lan_agent_read_directory_files" : "";
    result.fields["continue_required"] = can_continue_directory_batch ? "true" : "false";
    result.fields["auto_continue_required"] = can_continue_directory_batch ? "true" : "false";
    result.fields["user_confirmation_required"] = "false";
    result.fields["analysis_allowed"] = can_continue_directory_batch ? "false" : "true";
    result.fields["analysis_blocked_reason"] = can_continue_directory_batch ? "directory_batch_read_incomplete" : "";
    result.fields["task_completion"] = can_continue_directory_batch ? "incomplete" : "complete";
    result.fields["partial_read_policy"] = can_continue_directory_batch
        ? "directory listing is complete; continue with lan_agent_read_directory_files until batch_completion=complete"
        : "directory listing is complete; no implicit file read continuation is emitted";
    result.fields["stop_condition"] = can_continue_directory_batch
        ? "batch_completion=complete"
        : "directory_listing_complete=true";
    result.fields["result"] = can_continue_directory_batch ? "directory_list_manifest_ready" : "directory_list_complete";
    result.fields["outcome_hint"] = can_continue_directory_batch ? "PARTIAL" : "PASS";
    result.fields["next_action"] = can_continue_directory_batch
        ? "directory listing is complete; continue reading files from the generated manifest before any conclusion"
        : "directory listing is complete";
    result.fields["next_tool_name"] = can_continue_directory_batch ? "lan_agent_read_directory_files" : "";
    result.fields["next_file_path"] = can_continue_directory_batch ? manifest_files.front() : "";
    result.fields["next_start_line"] = can_continue_directory_batch ? "1" : "";
    result.fields["next_max_lines"] = can_continue_directory_batch ? "500" : "";
    result.fields["truncated"] = can_continue_directory_batch ? "true" : "false";
    result.fields["next_call_json"] = can_continue_directory_batch
        ? BuildDirectoryReadContinuationCallJson(
            normalized.string(),
            static_cast<int>(manifest_files.size()),
            500,
            5,
            2500,
            0,
            1,
            trace_id)
        : "";
    result.fields["required_tool_name"] = can_continue_directory_batch ? "lan_agent_read_directory_files" : "";
    result.fields["required_tool_arguments_json"] = result.fields["next_call_json"];
    return result;
}

std::vector<std::string> ParseDirectoryReadExtensionsCsv(const std::string & raw_csv) {
    std::vector<std::string> extensions;
    std::istringstream input(raw_csv);
    std::string token;
    while (std::getline(input, token, ',')) {
        std::string normalized = ToLowerAscii(Trim(token));
        if (normalized.empty()) {
            continue;
        }
        if (normalized.front() != '.') {
            normalized.insert(normalized.begin(), '.');
        }
        if (std::find(extensions.begin(), extensions.end(), normalized) == extensions.end()) {
            extensions.push_back(normalized);
        }
    }
    return extensions;
}

std::string JoinDirectoryReadExtensionsCsv(const std::vector<std::string> & extensions) {
    std::ostringstream output;
    for (std::size_t index = 0; index < extensions.size(); ++index) {
        if (index != 0) {
            output << ",";
        }
        output << extensions[index];
    }
    return output.str();
}

bool MatchesDirectoryReadExtensions(
    const std::filesystem::path & path,
    const std::vector<std::string> & extensions) {
    if (extensions.empty()) {
        return true;
    }
    const std::string extension = ToLowerAscii(path.extension().string());
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

CommandResult ReadDirectoryFilesResult(
    const AgentConfig & config,
    const std::string & directory_path,
    const std::string & file_extensions_csv,
    int max_files = 200,
    int max_lines_per_file = 500,
    int max_files_per_call = 5,
    int max_total_lines = 2500,
    int file_index = 0,
    int start_line = 1,
    const std::string & trace_id = std::string(),
    std::size_t start_byte_offset = 0) {
    (void)max_files_per_call;
    (void)max_total_lines;

    CommandResult result;
    result.fields["task_type"] = "directory_file_read";
    result.fields["directory_path"] = directory_path;
    result.fields["trace_id"] = trace_id;

    if (directory_path.empty()) {
        result.ok = false;
        result.exit_code = 60;
        result.fields["error"] = "directory_path is required";
        return result;
    }

    const std::vector<std::string> extension_filters =
        ParseDirectoryReadExtensionsCsv(file_extensions_csv);
    result.fields["file_extensions_csv"] = JoinDirectoryReadExtensionsCsv(extension_filters);
    result.fields["file_type_filter_applied"] = extension_filters.empty() ? "false" : "true";

    std::filesystem::path requested(directory_path);
    std::error_code ec;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(requested, ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 61;
        result.fields["error"] = "failed to normalize directory path";
        return result;
    }

    const std::filesystem::path logs_root = std::filesystem::path(config.log_root);
    const std::filesystem::path workspace_root = std::filesystem::path(config.workspace_root);
    if (!StartsWithPath(normalized, logs_root) && !StartsWithPath(normalized, workspace_root)) {
        result.ok = false;
        result.exit_code = 62;
        result.fields["error"] = "directory is outside allowed roots";
        return result;
    }
    if (!std::filesystem::exists(normalized)) {
        result.ok = false;
        result.exit_code = 63;
        result.fields["error"] = "directory does not exist";
        return result;
    }
    if (!std::filesystem::is_directory(normalized)) {
        result.ok = false;
        result.exit_code = 64;
        result.fields["error"] = "path is not a directory";
        return result;
    }

    std::vector<std::string> matching_files;
    for (const auto & entry : std::filesystem::directory_iterator(normalized)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (!MatchesDirectoryReadExtensions(entry.path(), extension_filters)) {
            continue;
        }
        matching_files.push_back(entry.path().string());
    }

    std::sort(
        matching_files.begin(),
        matching_files.end(),
        [](const std::string & left, const std::string & right) {
            return ToLowerAscii(std::filesystem::path(left).filename().string())
                < ToLowerAscii(std::filesystem::path(right).filename().string());
        });

    const int bounded_max_files = max_files > 0 ? max_files : 200;
    if (static_cast<int>(matching_files.size()) > bounded_max_files) {
        matching_files.resize(static_cast<std::size_t>(bounded_max_files));
    }

    result.fields["normalized_path"] = normalized.string();
    result.fields["directory_listing_complete"] = "true";
    result.fields["known_file_list_complete"] = "true";
    result.fields["matched_file_count"] = std::to_string(matching_files.size());
    result.fields["batch_total_files"] = std::to_string(matching_files.size());

    std::string manifest_path;
    const bool manifest_saved =
        !trace_id.empty() && SaveDirectoryReadManifest(config, trace_id, normalized.string(), matching_files, &manifest_path);
    result.fields["batch_manifest_path"] = manifest_saved ? manifest_path : "";
    result.fields["batch_manifest_ready"] = manifest_saved ? "true" : "false";
    result.fields["batch_manifest_complete"] = matching_files.empty() ? "true" : (manifest_saved ? "true" : "false");

    if (matching_files.empty()) {
        result.fields["task_type"] = "directory_file_read";
        result.fields["analysis_allowed"] = "true";
        result.fields["directory_complete"] = "true";
        result.fields["task_completion"] = "complete";
        result.fields["batch_completion"] = "complete";
        result.fields["content_read_completion"] = "complete";
        result.fields["incomplete_scope"] = "";
        result.fields["continue_required"] = "false";
        result.fields["auto_continue_required"] = "false";
        result.fields["remaining_batch_file_count"] = "0";
        result.fields["batch_read_file_count"] = "0";
        result.fields["current_file_index"] = "";
        result.fields["current_file_path"] = "";
        result.fields["next_file_index"] = "";
        result.fields["next_file_path"] = "";
        result.fields["next_batch_file_path"] = "";
        result.fields["next_batch_tool_name"] = "";
        result.fields["result"] = "directory_file_batch_complete";
        result.fields["next_action"] = "no matching files remain to read";
        result.fields["stop_condition"] = "batch_completion=complete";
        result.fields["read_contract"] =
            "repeat lan_agent_read_directory_files with next_file_index and next_start_line until batch_completion=complete";
        result.fields["next_tool_name"] = "";
        result.fields["next_call_json"] = "";
        result.fields["required_tool_name"] = "";
        result.fields["required_tool_arguments_json"] = "";
        return result;
    }

    const int bounded_file_index =
        std::min(
            std::max(0, file_index),
            static_cast<int>(matching_files.size()) - 1);
    const int bounded_max_lines_per_file = max_lines_per_file > 0 ? max_lines_per_file : 500;
    const int bounded_start_line = start_line > 0 ? start_line : 1;
    const std::string current_file_path = matching_files[bounded_file_index];
    const CommandResult page_result = ReadTextFileResult(
        config,
        current_file_path,
        bounded_max_lines_per_file,
        bounded_start_line,
        trace_id,
        start_byte_offset);
    result = page_result;
    if (!result.ok) {
        result.fields["task_type"] = "directory_file_read";
        result.fields["directory_path"] = directory_path;
        result.fields["normalized_directory_path"] = normalized.string();
        result.fields["file_extensions_csv"] = JoinDirectoryReadExtensionsCsv(extension_filters);
        result.fields["matched_file_count"] = std::to_string(matching_files.size());
        result.fields["current_file_index"] = std::to_string(bounded_file_index);
        result.fields["current_file_path"] = current_file_path;
        return result;
    }

    const bool current_file_complete = GetFieldOrDefault(result, "read_complete", "false") == "true";
    const int files_read = bounded_file_index + (current_file_complete ? 1 : 0);
    const int remaining_files =
        std::max(0, static_cast<int>(matching_files.size()) - files_read);
    const bool batch_complete = current_file_complete && remaining_files == 0;
    int next_file_index = bounded_file_index;
    int next_start_line = std::atoi(GetFieldOrDefault(result, "next_start_line", "0").c_str());
    const std::string next_byte_offset = GetFieldOrDefault(result, "next_byte_offset", "");
    std::string next_file_path = current_file_path;
    if (current_file_complete && !batch_complete) {
        next_file_index = bounded_file_index + 1;
        next_start_line = 1;
        next_file_path = matching_files[static_cast<std::size_t>(next_file_index)];
    }

    result.fields["directory_path"] = directory_path;
    result.fields["task_type"] = "directory_file_read";
    result.fields["normalized_directory_path"] = normalized.string();
    result.fields["trace_id"] = trace_id;
    result.fields["file_extensions_csv"] = JoinDirectoryReadExtensionsCsv(extension_filters);
    result.fields["file_type_filter_applied"] = extension_filters.empty() ? "false" : "true";
    result.fields["directory_listing_complete"] = "true";
    result.fields["known_file_list_complete"] = "true";
    result.fields["batch_manifest_path"] = manifest_saved ? manifest_path : "";
    result.fields["batch_manifest_ready"] = manifest_saved ? "true" : "false";
    result.fields["batch_manifest_complete"] = manifest_saved ? "true" : "false";
    result.fields["matched_file_count"] = std::to_string(matching_files.size());
    result.fields["batch_total_files"] = std::to_string(matching_files.size());
    result.fields["batch_read_file_count"] = std::to_string(files_read);
    result.fields["remaining_batch_file_count"] = std::to_string(remaining_files);
    result.fields["directory_complete"] = batch_complete ? "true" : "false";
    result.fields["batch_completion"] = batch_complete ? "complete" : "incomplete";
    result.fields["content_read_completion"] = batch_complete ? "complete" : "incomplete";
    result.fields["incomplete_scope"] = batch_complete ? "" : "remaining_file_contents";
    result.fields["analysis_allowed"] = batch_complete ? "true" : "false";
    result.fields["continue_required"] = batch_complete ? "false" : "true";
    result.fields["auto_continue_required"] = batch_complete ? "false" : "true";
    result.fields["task_completion"] = batch_complete ? "complete" : "incomplete";
    result.fields["result"] = batch_complete ? "directory_file_batch_complete" : "directory_file_batch_partial";
    result.fields["continuation_status"] = batch_complete ? "complete" : "needs_continue";
    result.fields["current_file_index"] = std::to_string(bounded_file_index);
    result.fields["current_file_path"] = current_file_path;
    result.fields["last_file_index"] = std::to_string(static_cast<int>(matching_files.size()) - 1);
    result.fields["last_file_path"] = matching_files.back();
    result.fields["next_batch_file_path"] = batch_complete ? "" : next_file_path;
    result.fields["next_batch_tool_name"] = batch_complete ? "" : "lan_agent_read_directory_files";
    result.fields["next_file_path"] = batch_complete ? "" : next_file_path;
    result.fields["next_file_index"] = batch_complete ? "" : std::to_string(next_file_index);
    result.fields["next_start_line"] = batch_complete ? "" : std::to_string(next_start_line);
    result.fields["next_byte_offset"] = batch_complete ? "" : next_byte_offset;
    result.fields["next_max_lines"] = batch_complete ? "" : std::to_string(bounded_max_lines_per_file);
    result.fields["next_tool_name"] = batch_complete ? "" : "lan_agent_read_directory_files";
    result.fields["required_tool_name"] = batch_complete ? "" : "lan_agent_read_directory_files";
    result.fields["next_action"] = batch_complete
        ? "all matching files were read"
        : (current_file_complete
            ? "read the next file page from the directory batch"
            : "continue reading the current file page before advancing");
    result.fields["stop_condition"] = "batch_completion=complete";
    result.fields["partial_read_policy"] =
        "repeat lan_agent_read_directory_files with next_file_index and next_start_line until batch_completion=complete";
    result.fields["read_contract"] =
        "repeat lan_agent_read_directory_files with next_file_index and next_start_line until batch_completion=complete";
    result.fields["next_call_json"] = batch_complete
        ? ""
        : ("{\"name\":\"lan_agent_read_directory_files\",\"arguments\":{\"directory_path\":\""
            + codex_lan_agent::JsonEscape(directory_path)
            + "\",\"file_extensions_csv\":\""
            + codex_lan_agent::JsonEscape(JoinDirectoryReadExtensionsCsv(extension_filters))
            + "\",\"max_files\":"
            + std::to_string(bounded_max_files)
            + ",\"max_lines_per_file\":"
            + std::to_string(bounded_max_lines_per_file)
            + ",\"max_files_per_call\":"
            + std::to_string(max_files_per_call > 0 ? max_files_per_call : 1)
            + ",\"max_total_lines\":"
            + std::to_string(max_total_lines > 0 ? max_total_lines : 1)
            + ",\"file_index\":"
            + std::to_string(next_file_index)
            + (next_byte_offset.empty()
                ? ",\"start_line\":" + std::to_string(next_start_line)
                : ",\"start_byte_offset\":" + next_byte_offset + ",\"start_line\":1")
            + (trace_id.empty() ? std::string() : ",\"trace_id\":\"" + codex_lan_agent::JsonEscape(trace_id) + "\"")
            + "}}");
    result.fields["required_tool_arguments_json"] = result.fields["next_call_json"];
    result.fields["server_side_atomic_read"] = "false";
    return result;
}

CommandResult PrepareDirectoryAnalysisResult(
    const AgentConfig & config,
    const std::string & directory_path,
    const std::string & file_extensions_csv,
    int max_files = 200,
    int max_excerpt_lines_per_file = 80,
    int max_total_excerpt_lines = 1200,
    std::size_t max_excerpt_chars = 24000,
    const std::string & trace_id = std::string()) {
    CommandResult result;
    result.fields["task_type"] = "directory_analysis_bundle";
    result.fields["directory_path"] = directory_path;
    if (!trace_id.empty()) {
        result.fields["trace_id"] = trace_id;
    }

    if (directory_path.empty()) {
        result.ok = false;
        result.exit_code = 68;
        result.fields["error"] = "directory_path is required";
        return result;
    }

    std::filesystem::path requested(directory_path);
    std::error_code ec;
    const std::filesystem::path normalized = std::filesystem::weakly_canonical(requested, ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 69;
        result.fields["error"] = "failed to normalize directory path";
        return result;
    }

    const std::filesystem::path logs_root = std::filesystem::path(config.log_root);
    const std::filesystem::path workspace_root = std::filesystem::path(config.workspace_root);
    if (!StartsWithPath(normalized, logs_root) && !StartsWithPath(normalized, workspace_root)) {
        result.ok = false;
        result.exit_code = 70;
        result.fields["error"] = "directory is outside allowed roots";
        return result;
    }
    if (!std::filesystem::exists(normalized)) {
        result.ok = false;
        result.exit_code = 71;
        result.fields["error"] = "directory does not exist";
        return result;
    }
    if (!std::filesystem::is_directory(normalized)) {
        result.ok = false;
        result.exit_code = 72;
        result.fields["error"] = "path is not a directory";
        return result;
    }

    const std::vector<std::string> extension_filters =
        ParseDirectoryReadExtensionsCsv(file_extensions_csv);
    result.fields["file_extensions_csv"] = JoinDirectoryReadExtensionsCsv(extension_filters);
    result.fields["file_type_filter_applied"] = extension_filters.empty() ? "false" : "true";

    std::vector<std::string> matching_files;
    for (const auto & entry : std::filesystem::directory_iterator(normalized)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (!MatchesDirectoryReadExtensions(entry.path(), extension_filters)) {
            continue;
        }
        matching_files.push_back(entry.path().string());
    }

    std::sort(
        matching_files.begin(),
        matching_files.end(),
        [](const std::string & left, const std::string & right) {
            return ToLowerAscii(std::filesystem::path(left).filename().string())
                < ToLowerAscii(std::filesystem::path(right).filename().string());
        });

    const int bounded_max_files = max_files > 0 ? max_files : 200;
    if (static_cast<int>(matching_files.size()) > bounded_max_files) {
        matching_files.resize(static_cast<std::size_t>(bounded_max_files));
    }

    const int bounded_max_excerpt_lines_per_file = std::max(1, max_excerpt_lines_per_file);
    const int bounded_max_total_excerpt_lines = std::max(1, max_total_excerpt_lines);
    const std::size_t bounded_max_excerpt_chars = std::max<std::size_t>(1024, max_excerpt_chars);

    std::ostringstream bundle;
    bundle << "directory_path=" << normalized.string() << "\n";
    bundle << "matched_file_count=" << matching_files.size() << "\n";
    bundle << "file_extensions_csv=" << JoinDirectoryReadExtensionsCsv(extension_filters) << "\n";
    bundle << "analysis_contract=directory framework bundle for downstream AI summarization\n\n";

    int excerpt_file_count = 0;
    int truncated_file_count = 0;
    int total_excerpt_lines = 0;
    std::vector<std::string> excerpted_file_paths;
    for (const std::string & file_path : matching_files) {
        if (total_excerpt_lines >= bounded_max_total_excerpt_lines) {
            break;
        }
        std::string excerpt_text;
        int excerpt_lines = 0;
        bool truncated = false;
        const int remaining_line_budget = bounded_max_total_excerpt_lines - total_excerpt_lines;
        const int per_file_line_budget = std::min(bounded_max_excerpt_lines_per_file, remaining_line_budget);
        const std::size_t remaining_char_budget =
            bounded_max_excerpt_chars > static_cast<std::size_t>(bundle.tellp())
                ? bounded_max_excerpt_chars - static_cast<std::size_t>(bundle.tellp())
                : static_cast<std::size_t>(0);
        if (remaining_char_budget < 256) {
            break;
        }
        if (!ReadFileExcerptPreview(
                std::filesystem::path(file_path),
                per_file_line_budget,
                remaining_char_budget,
                &excerpt_text,
                &excerpt_lines,
                &truncated)) {
            continue;
        }
        if (excerpt_lines <= 0 && excerpt_text.empty()) {
            continue;
        }
        ++excerpt_file_count;
        total_excerpt_lines += excerpt_lines;
        if (truncated) {
            ++truncated_file_count;
        }
        excerpted_file_paths.push_back(file_path);
        bundle << "===== FILE " << excerpt_file_count << ": " << file_path << " =====\n";
        bundle << excerpt_text;
        if (truncated) {
            bundle << "[truncated]\n";
        }
        bundle << "\n";
    }

    const int omitted_file_count = std::max(0, static_cast<int>(matching_files.size()) - excerpt_file_count);
    const std::string bundle_text = bundle.str();
    const std::string bundle_path = BuildDirectoryAnalysisBundlePath(config, trace_id, normalized.string());
    std::filesystem::create_directories(BuildDirectoryAnalysisBundleRoot(config));
    std::ofstream output(bundle_path, std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        result.ok = false;
        result.exit_code = 73;
        result.fields["error"] = "failed to write directory analysis bundle";
        return result;
    }
    output << bundle_text;
    output.close();

    result.ok = true;
    result.exit_code = 0;
    result.fields["status"] = "success";
    result.fields["result"] = "directory_analysis_bundle_ready";
    result.fields["summary"] = excerpt_file_count > 0
        ? "directory analysis bundle prepared"
        : "directory analysis bundle prepared with no readable excerpts";
    result.fields["normalized_path"] = normalized.string();
    result.fields["directory_listing_complete"] = "true";
    result.fields["known_file_list_complete"] = "true";
    result.fields["analysis_allowed"] = "true";
    result.fields["task_completion"] = "complete";
    result.fields["batch_completion"] = "complete";
    result.fields["content_read_completion"] = "complete";
    result.fields["continue_required"] = "false";
    result.fields["auto_continue_required"] = "false";
    result.fields["user_confirmation_required"] = "false";
    result.fields["incomplete_scope"] = "";
    result.fields["stop_condition"] = "single_call_complete";
    result.fields["next_action"] =
        "pass content_text or analysis_bundle_ref to analysis tooling for project overview";
    result.fields["content"] = bundle_text;
    result.fields["content_text"] = bundle_text;
    result.fields["content_payload_format"] = "plain_text";
    result.fields["content_payload_scope"] = "directory_analysis_bundle";
    result.fields["content_payload_boundary_safe"] = "true";
    result.fields["matched_file_count"] = std::to_string(matching_files.size());
    result.fields["excerpt_file_count"] = std::to_string(excerpt_file_count);
    result.fields["truncated_file_count"] = std::to_string(truncated_file_count);
    result.fields["omitted_file_count"] = std::to_string(omitted_file_count);
    result.fields["total_excerpt_lines"] = std::to_string(total_excerpt_lines);
    result.fields["max_excerpt_lines_per_file"] = std::to_string(bounded_max_excerpt_lines_per_file);
    result.fields["max_total_excerpt_lines"] = std::to_string(bounded_max_total_excerpt_lines);
    result.fields["max_excerpt_chars"] = std::to_string(bounded_max_excerpt_chars);
    result.fields["source_excerpt_chars"] = std::to_string(bundle_text.size());
    result.fields["excerpted_file_paths_json"] = BuildJsonStringArrayFromStrings(excerpted_file_paths);
    result.fields["analysis_bundle_ref"] = bundle_path;
    result.fields["result_ref"] = bundle_path;
    result.fields["evidence_ref"] = bundle_path;
    result.fields["log_path"] = bundle_path;
    result.fields["analysis_bundle_contract"] =
        "single-call directory overview bundle; use content_text for immediate analysis and analysis_bundle_ref for auditable replay";
    result.fields["server_side_atomic_read"] = "true";
    return result;
}

CommandResult DiscoverLogsResult(
    const AgentConfig & config,
    int max_entries,
    int tail_lines) {
    CommandResult result;
    result.fields["log_root"] = config.log_root;
    const int bounded_max_entries = max_entries > 0 ? max_entries : 20;
    const int bounded_tail_lines = tail_lines > 0 ? tail_lines : 20;

    struct LogEntry {
        std::filesystem::path path;
        std::filesystem::file_time_type write_time;
        std::uintmax_t size = 0;
    };

    std::vector<LogEntry> entries;
    std::error_code ec;
    for (const auto & entry : std::filesystem::directory_iterator(config.log_root, ec)) {
        if (ec) {
            result.ok = false;
            result.exit_code = 49;
            result.fields["error"] = "failed to list log_root";
            return result;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".log") {
            continue;
        }
        LogEntry item;
        item.path = entry.path();
        item.write_time = entry.last_write_time(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        item.size = entry.file_size(ec);
        if (ec) {
            item.size = 0;
            ec.clear();
        }
        entries.push_back(item);
    }

    std::sort(
        entries.begin(),
        entries.end(),
        [](const LogEntry & left, const LogEntry & right) {
            return left.write_time > right.write_time;
        });

    const std::size_t count =
        std::min<std::size_t>(entries.size(), static_cast<std::size_t>(bounded_max_entries));
    result.fields["log_count"] = std::to_string(entries.size());
    result.fields["returned_count"] = std::to_string(count);
    for (std::size_t index = 0; index < count; ++index) {
        const LogEntry & item = entries[index];
        const auto system_now = std::chrono::system_clock::now();
        const auto file_now = std::filesystem::file_time_type::clock::now();
        const auto system_write_time =
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                item.write_time - file_now + system_now);
        const std::time_t write_time = std::chrono::system_clock::to_time_t(system_write_time);
        const std::string prefix = "log_" + std::to_string(index);
        result.fields[prefix + "_path"] = item.path.string();
        result.fields[prefix + "_name"] = item.path.filename().string();
        result.fields[prefix + "_time"] = std::to_string(static_cast<long long>(write_time));
        result.fields[prefix + "_bytes"] = std::to_string(item.size);
    }

    if (count > 0) {
        CommandResult tail = TailTextFileResult(config, entries.front().path.string(), bounded_tail_lines);
        result.fields["latest_log_path"] = entries.front().path.string();
        result.fields["latest_log_name"] = entries.front().path.filename().string();
        result.fields["latest_log_tail"] = GetFieldOrDefault(tail, "content", "");
    } else {
        result.fields["latest_log_path"] = "";
        result.fields["latest_log_name"] = "";
        result.fields["latest_log_tail"] = "";
    }
    return result;
}
