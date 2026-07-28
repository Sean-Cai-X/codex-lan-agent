#pragma once

#include <regex>

std::string Trim(const std::string & value);
std::string BuildLogPath(const AgentConfig & config, const std::string & prefix);
bool TryResolveAllowedPath(
    const AgentConfig & config,
    const std::string & raw_path,
    std::filesystem::path * normalized_path,
    std::string * error_message);
bool ReadWholeFile(
    const std::filesystem::path & path,
    std::string * content,
    std::string * error_message);
std::string StableContentChecksum(const std::string & content);

std::string BuildJsonStringArray(
    const std::vector<std::string> & values,
    std::size_t start_index,
    std::size_t max_entries) {
    std::ostringstream output;
    output << "[";
    const std::size_t safe_start = std::min(start_index, values.size());
    const std::size_t safe_end = std::min(values.size(), safe_start + max_entries);
    for (std::size_t index = safe_start; index < safe_end; ++index) {
        if (index > safe_start) {
            output << ",";
        }
        output << "\"" << codex_lan_agent::JsonEscape(values[index]) << "\"";
    }
    output << "]";
    return output.str();
}

std::string ParseCtestTestNameToken(const std::string & raw_text) {
    const std::string text = Trim(raw_text);
    if (text.empty()) {
        return std::string();
    }
    if (text[0] == '"') {
        const std::size_t quote_end = text.find('"', 1);
        return quote_end == std::string::npos ? std::string() : text.substr(1, quote_end - 1);
    }
    std::size_t token_end = 0;
    while (token_end < text.size()) {
        const char ch = text[token_end];
        if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == ')') {
            break;
        }
        ++token_end;
    }
    return text.substr(0, token_end);
}

void AppendUniqueValue(std::vector<std::string> * values, const std::string & value) {
    if (values == nullptr || value.empty()) {
        return;
    }
    if (std::find(values->begin(), values->end(), value) == values->end()) {
        values->push_back(value);
    }
}

std::vector<std::string> ParseCtestNamesFromOutput(const std::string & log_content) {
    std::vector<std::string> names;
    std::istringstream input(log_content);
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t marker = line.find("Test #");
        if (marker == std::string::npos) {
            continue;
        }
        const std::size_t colon = line.find(':', marker);
        if (colon == std::string::npos || colon + 1 >= line.size()) {
            continue;
        }
        AppendUniqueValue(&names, Trim(line.substr(colon + 1)));
    }
    return names;
}

std::vector<std::string> ParseCmakeArgumentTokens(const std::string & text) {
    std::vector<std::string> tokens;
    std::size_t index = 0;
    while (index < text.size()) {
        while (index < text.size() &&
               std::isspace(static_cast<unsigned char>(text[index])) != 0) {
            ++index;
        }
        if (index >= text.size()) {
            break;
        }
        if (text[index] == '"') {
            const std::size_t quote_end = text.find('"', index + 1);
            if (quote_end == std::string::npos) {
                break;
            }
            tokens.push_back(text.substr(index + 1, quote_end - index - 1));
            index = quote_end + 1;
            continue;
        }
        if (index + 2 < text.size() && text.compare(index, 3, "[=[") == 0) {
            const std::size_t bracket_end = text.find("]=]", index + 3);
            if (bracket_end == std::string::npos) {
                break;
            }
            tokens.push_back(text.substr(index + 3, bracket_end - index - 3));
            index = bracket_end + 3;
            continue;
        }

        std::size_t token_end = index;
        while (token_end < text.size()) {
            const char ch = text[token_end];
            if (std::isspace(static_cast<unsigned char>(ch)) != 0 || ch == ')') {
                break;
            }
            ++token_end;
        }
        tokens.push_back(text.substr(index, token_end - index));
        index = token_end;
    }
    return tokens;
}

bool TryRegexSearch(const std::string & text, const std::string & regex_text) {
    if (regex_text.empty()) {
        return true;
    }
    try {
        const std::regex pattern(regex_text, std::regex::ECMAScript);
        return std::regex_search(text, pattern);
    } catch (const std::regex_error &) {
        return text.find(regex_text) != std::string::npos;
    }
}

std::vector<std::string> FilterValuesByRegex(
    const std::vector<std::string> & values,
    const std::string & regex_text) {
    if (regex_text.empty()) {
        return values;
    }
    std::vector<std::string> filtered;
    for (const std::string & value : values) {
        if (TryRegexSearch(value, regex_text)) {
            AppendUniqueValue(&filtered, value);
        }
    }
    return filtered;
}

std::string EscapeRegexLiteral(const std::string & value) {
    std::string escaped;
    escaped.reserve(value.size() * 2);
    for (const char ch : value) {
        switch (ch) {
            case '\\':
            case '^':
            case '$':
            case '.':
            case '|':
            case '?':
            case '*':
            case '+':
            case '(':
            case ')':
            case '[':
            case ']':
            case '{':
            case '}':
                escaped.push_back('\\');
                break;
            default:
                break;
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string BuildRegexUnionForExactTests(const std::vector<std::string> & test_names) {
    if (test_names.empty()) {
        return std::string();
    }
    std::ostringstream output;
    output << "^(";
    for (std::size_t index = 0; index < test_names.size(); ++index) {
        if (index > 0) {
            output << "|";
        }
        output << EscapeRegexLiteral(test_names[index]);
    }
    output << ")$";
    return output.str();
}

std::vector<std::string> ParseCtestNamesFromTestfiles(const std::filesystem::path & build_dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto & entry : std::filesystem::recursive_directory_iterator(build_dir, ec)) {
        if (ec) {
            ec.clear();
            break;
        }
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().filename() != "CTestTestfile.cmake") {
            continue;
        }
        std::ifstream input(entry.path());
        if (!input.is_open()) {
            continue;
        }
        std::string line;
        while (std::getline(input, line)) {
            const std::size_t add_test_pos = line.find("add_test(");
            if (add_test_pos == std::string::npos) {
                continue;
            }
            std::string arguments = Trim(line.substr(add_test_pos + 9));
            if (!arguments.empty() && arguments.back() == ')') {
                arguments.pop_back();
                arguments = Trim(arguments);
            }
            if (ToLowerAscii(arguments).rfind("name ", 0) == 0) {
                arguments = Trim(arguments.substr(5));
            }
            AppendUniqueValue(&names, ParseCtestTestNameToken(arguments));
        }
    }
    return names;
}

std::vector<std::string> ParseCtestNamesFromLabelMatch(
    const std::filesystem::path & build_dir,
    const std::string & label_regex,
    std::vector<std::string> * matched_labels) {
    std::vector<std::string> names;
    if (label_regex.empty()) {
        return names;
    }

    std::error_code ec;
    for (const auto & entry : std::filesystem::recursive_directory_iterator(build_dir, ec)) {
        if (ec) {
            ec.clear();
            break;
        }
        if (!entry.is_regular_file() || entry.path().filename() != "CTestTestfile.cmake") {
            continue;
        }
        std::ifstream input(entry.path());
        if (!input.is_open()) {
            continue;
        }
        std::string line;
        while (std::getline(input, line)) {
            const std::size_t set_pos = line.find("set_tests_properties(");
            const std::size_t labels_pos = line.find(" PROPERTIES LABELS ");
            if (set_pos == std::string::npos || labels_pos == std::string::npos || labels_pos <= set_pos + 20) {
                continue;
            }

            std::string names_part = Trim(line.substr(set_pos + 20, labels_pos - (set_pos + 20)));
            std::string label_part = Trim(line.substr(labels_pos + 19));
            if (!label_part.empty() && label_part.back() == ')') {
                label_part.pop_back();
                label_part = Trim(label_part);
            }

            const std::vector<std::string> test_names = ParseCmakeArgumentTokens(names_part);
            const std::vector<std::string> label_tokens = ParseCmakeArgumentTokens(label_part);
            if (test_names.empty() || label_tokens.empty()) {
                continue;
            }

            bool matched = false;
            for (const std::string & token : label_tokens) {
                std::stringstream token_stream(token);
                std::string label;
                while (std::getline(token_stream, label, ';')) {
                    label = Trim(label);
                    if (label.empty()) {
                        continue;
                    }
                    if (TryRegexSearch(label, label_regex)) {
                        matched = true;
                        AppendUniqueValue(matched_labels, label);
                    }
                }
            }

            if (!matched) {
                continue;
            }
            for (const std::string & test_name : test_names) {
                AppendUniqueValue(&names, test_name);
            }
        }
    }
    return names;
}

CommandResult DiscoverCtestTestsResult(
    const AgentConfig & config,
    const std::string & build_dir,
    const std::string & config_name,
    const std::string & test_regex,
    int start_index,
    int max_entries) {
    CommandResult result;
    result.fields["build_dir"] = build_dir;
    result.fields["config"] = config_name.empty() ? "Release" : config_name;
    result.fields["test_regex"] = test_regex;
    result.fields["start_index"] = std::to_string(std::max(start_index, 0));
    result.fields["max_entries"] = std::to_string(max_entries > 0 ? max_entries : 200);

    if (build_dir.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "build_dir is required";
        result.fields["semantic_outcome"] = "build_dir_missing";
        result.fields["test_count"] = "0";
        result.fields["test_names"] = "[]";
        result.fields["process_exit_ok"] = "false";
        result.fields["verification_ok"] = "false";
        result.fields["next_action"] = "provide build_dir and rerun lan_agent_discover_ctest_tests";
        return result;
    }

    std::filesystem::path normalized_build_dir;
    std::string path_error;
    if (!TryResolveAllowedPath(config, build_dir, &normalized_build_dir, &path_error)) {
        result.ok = false;
        result.exit_code = 404;
        result.fields["error"] = path_error;
        result.fields["semantic_outcome"] = "build_dir_missing";
        result.fields["test_count"] = "0";
        result.fields["test_names"] = "[]";
        result.fields["process_exit_ok"] = "false";
        result.fields["verification_ok"] = "false";
        result.fields["next_action"] = "use a valid build_dir under workspace_root and rerun discovery";
        return result;
    }

    result.fields["normalized_build_dir"] = normalized_build_dir.string();
    if (!std::filesystem::exists(normalized_build_dir) || !std::filesystem::is_directory(normalized_build_dir)) {
        result.ok = false;
        result.exit_code = 404;
        result.fields["semantic_outcome"] = "build_dir_missing";
        result.fields["test_count"] = "0";
        result.fields["test_names"] = "[]";
        result.fields["process_exit_ok"] = "false";
        result.fields["verification_ok"] = "false";
        result.fields["next_action"] = "create or select the correct build_dir before discovery";
        return result;
    }

    const std::filesystem::path cmake_cache_path = normalized_build_dir / "CMakeCache.txt";
    const std::filesystem::path ctest_file_path = normalized_build_dir / "CTestTestfile.cmake";
    const bool configured = std::filesystem::exists(cmake_cache_path);
    const bool ctest_registered = std::filesystem::exists(ctest_file_path);
    result.fields["cmake_cache_path"] = cmake_cache_path.string();
    result.fields["ctest_file_path"] = ctest_file_path.string();
    result.fields["configured"] = configured ? "true" : "false";
    result.fields["ctest_registered"] = ctest_registered ? "true" : "false";

    const std::string log_path = BuildLogPath(config, "discover_ctest_tests");
    result.fields["log_path"] = log_path;
    result.fields["evidence_ref"] = log_path;
    result.fields["result_ref"] =
        "discover_ctest_tests:" + StableContentChecksum(normalized_build_dir.string() + "|" + result.fields["config"] + "|" + test_regex);

    if (!configured) {
        std::ofstream output(log_path, std::ios::out | std::ios::trunc);
        output << "build_dir=" << normalized_build_dir.string() << "\n";
        output << "config=" << result.fields["config"] << "\n";
        output << "configured=false\n";
        output << "ctest_registered=false\n";
        result.ok = false;
        result.exit_code = 65;
        result.fields["semantic_outcome"] = "not_configured";
        result.fields["test_count"] = "0";
        result.fields["test_names"] = "[]";
        result.fields["process_exit_ok"] = "false";
        result.fields["verification_ok"] = "false";
        result.fields["next_action"] = "run lan_agent_configure_project for this build_dir before discovering tests";
        return result;
    }

    std::vector<std::string> test_names;
    std::vector<std::string> all_registered_test_names;
    std::vector<std::string> matched_labels;
    codex_lan_agent::ProcessRunResult run_result;
    std::string run_error;
    std::string ctest_command = "ctest -N -C " + result.fields["config"];
    if (!test_regex.empty()) {
        ctest_command += " -R \"" + test_regex + "\"";
    }
    const bool process_started = codex_lan_agent::RunCommandWithLog(
        ctest_command,
        normalized_build_dir.string(),
        log_path,
        config.task_timeout_sec,
        30,
        &run_result,
        &run_error);

    std::string log_content;
    std::string log_read_error;
    const bool log_read_ok = ReadWholeFile(log_path, &log_content, &log_read_error);
    if (process_started && log_read_ok) {
        test_names = ParseCtestNamesFromOutput(log_content);
    }
    if (ctest_registered) {
        all_registered_test_names = ParseCtestNamesFromTestfiles(normalized_build_dir);
    }
    if (test_names.empty() && !all_registered_test_names.empty()) {
        test_names = FilterValuesByRegex(all_registered_test_names, test_regex);
        if (test_names.empty() && !test_regex.empty()) {
            test_names = ParseCtestNamesFromLabelMatch(normalized_build_dir, test_regex, &matched_labels);
            if (!test_names.empty()) {
                result.fields["selector_mode"] = "label_fallback";
                result.fields["matched_labels"] = BuildJsonStringArray(matched_labels, 0, matched_labels.size());
                result.fields["matched_label_count"] = std::to_string(matched_labels.size());
                result.fields["resolved_test_regex"] = BuildRegexUnionForExactTests(test_names);
            }
        }
    }
    if (GetFieldOrDefault(result, "selector_mode", "").empty()) {
        result.fields["selector_mode"] = "test_name";
    }

    const bool process_exit_ok =
        process_started && run_result.exit_code == 0 && !run_result.timed_out && !run_result.stalled;
    result.fields["process_exit_ok"] = process_exit_ok ? "true" : "false";
    result.fields["timed_out"] = process_started && run_result.timed_out ? "true" : "false";
    result.fields["stalled"] = process_started && run_result.stalled ? "true" : "false";
    if (process_started) {
        result.fields["process_id"] = std::to_string(run_result.process_id);
        result.fields["started_at"] = run_result.started_at;
        result.fields["finished_at"] = run_result.finished_at;
        result.fields["last_output_at"] = run_result.last_output_at;
    } else if (!run_error.empty()) {
        result.fields["process_error"] = run_error;
    }
    if (!log_read_ok && !log_read_error.empty()) {
        result.fields["log_read_error"] = log_read_error;
    }

    const std::size_t total_tests = test_names.size();
    const std::size_t safe_start_index = static_cast<std::size_t>(std::max(start_index, 0));
    const std::size_t safe_max_entries = static_cast<std::size_t>(max_entries > 0 ? max_entries : 200);
    const std::size_t returned_count =
        safe_start_index >= total_tests ? 0 : std::min(total_tests - safe_start_index, safe_max_entries);
    const bool has_more = safe_start_index + returned_count < total_tests;

    result.fields["test_count"] = std::to_string(total_tests);
    result.fields["returned_test_count"] = std::to_string(returned_count);
    result.fields["remaining_test_count"] = std::to_string(has_more ? (total_tests - safe_start_index - returned_count) : 0);
    result.fields["has_more"] = has_more ? "true" : "false";
    result.fields["next_start_index"] = has_more ? std::to_string(safe_start_index + returned_count) : "";
    result.fields["test_names"] = BuildJsonStringArray(test_names, safe_start_index, safe_max_entries);

    if (!test_names.empty()) {
        result.ok = true;
        result.exit_code = 0;
        const bool label_fallback = GetFieldOrDefault(result, "selector_mode", "") == "label_fallback";
        result.fields["semantic_outcome"] = label_fallback ? "tests_discovered_via_label" : "tests_discovered";
        result.fields["verification_ok"] = "true";
        result.fields["next_action"] =
            has_more
                ? "continue with next_start_index to page through the remaining discovered tests"
                : (label_fallback
                    ? "label fallback matched registered tests; run_ctest_target can queue the resolved exact-name regex"
                    : "use the discovered test_names in the browser list or run_ctest_target");
        return result;
    }

    result.ok = true;
    result.exit_code = 0;
    result.fields["semantic_outcome"] = ctest_registered ? "no_tests_found" : "no_tests_found";
    result.fields["verification_ok"] = "false";
    result.fields["next_action"] =
        ctest_registered
            ? "no tests matched discovery; confirm build_dir, check add_test registration, or rerun with a broader test_regex"
            : "configure succeeded but no CTest registration was found; check add_test registration or confirm the build_dir";
    return result;
}

void AddCtestDiscoveryHints(
    const AgentConfig & config,
    const std::string & extra_arguments,
    CommandResult * result) {
    if (result == nullptr) {
        return;
    }
    const std::string build_dir = ExtractCliNamedArgument(extra_arguments, "-BuildDir");
    if (build_dir.empty()) {
        result->fields["ctest_discovery_build_dir"] = "";
        result->fields["ctest_discovery_configured"] = "unknown";
        result->fields["ctest_discovery_ctest_registered"] = "unknown";
        result->fields["ctest_discovery_next_action"] =
            "rerun with an explicit build_dir and then verify CTest registration";
        return;
    }

    result->fields["ctest_discovery_build_dir"] = build_dir;
    std::filesystem::path normalized;
    std::string path_error;
    if (!TryResolveAllowedPath(config, build_dir, &normalized, &path_error)) {
        result->fields["ctest_discovery_configured"] = "unknown";
        result->fields["ctest_discovery_ctest_registered"] = "unknown";
        result->fields["ctest_discovery_error"] = path_error;
        result->fields["ctest_discovery_next_action"] =
            "use a valid build_dir under workspace_root before rerunning ctest";
        return;
    }

    const std::filesystem::path cache_path = normalized / "CMakeCache.txt";
    const std::filesystem::path ctest_file = normalized / "CTestTestfile.cmake";
    const bool cache_exists = std::filesystem::exists(cache_path);
    const bool ctest_registered = std::filesystem::exists(ctest_file);
    result->fields["ctest_discovery_normalized_build_dir"] = normalized.string();
    result->fields["ctest_discovery_cmake_cache_path"] = cache_path.string();
    result->fields["ctest_discovery_ctest_file_path"] = ctest_file.string();
    result->fields["ctest_discovery_configured"] = cache_exists ? "true" : "false";
    result->fields["ctest_discovery_ctest_registered"] = ctest_registered ? "true" : "false";
    result->fields["ctest_discovery_next_action"] =
        !cache_exists
            ? "run lan_agent_configure_project for this build_dir before rerunning ctest"
            : (!ctest_registered
                ? "configure completed but no CTest registration was found; check add_test registration or choose the correct build_dir"
                : "ctest is registered; rerun with a narrower test_regex or inspect the generated test list");
}
