#pragma once

bool CxParserTextHasNonAscii(const std::string & value) {
    for (unsigned char ch : value) {
        if (ch > 0x7F) {
            return true;
        }
    }
    return false;
}

void ApplyCxParserEncodingAdvisory(CommandResult * result, const std::string & input_text) {
    if (result == nullptr || input_text.empty()) {
        return;
    }
    result->fields["encoding_contract"] = "utf8_preferred_ascii_safe";
    result->fields["encoding_input_field"] = "test_statement";
    result->fields["encoding_input_contains_non_ascii"] =
        CxParserTextHasNonAscii(input_text) ? "true" : "false";
    if (CxParserTextHasNonAscii(input_text)) {
        result->fields["encoding_warning"] =
            "non-ASCII test_statement detected; verify remote logs and echoes remain UTF-8 clean";
    }
}

std::string ResolveCxParserRuntimeBindingSourceForFlow(
    const AgentConfig & config,
    const std::string & flow_id) {
    if (flow_id == "cxparser_ext_cxscript_cli") {
        std::string runtime_source;
        if (HasCxParserRuntimeBinding(config, flow_id, &runtime_source) && !runtime_source.empty()) {
            return runtime_source;
        }
        return std::string();
    }

    const auto runtime_it = config.cxparser_runtime_commands.find(flow_id);
    if (runtime_it != config.cxparser_runtime_commands.end() && !runtime_it->second.empty()) {
        return "cxparser_runtime_command";
    }
    const auto profile_it = config.profiles.find(flow_id);
    if (profile_it != config.profiles.end() && !profile_it->second.empty()) {
        return "profile_fallback";
    }
    return std::string();
}

std::string ResolveCxParserRuntimeBindingEntrypointForFlow(
    const AgentConfig & config,
    const std::string & flow_id) {
    if (flow_id == "cxparser_ext_cxscript_cli") {
        const std::string cli_path = FindCxParserCxScriptCliExecutablePath(config);
        if (!cli_path.empty()) {
            return cli_path;
        }
        return std::string();
    }

    const auto runtime_it = config.cxparser_runtime_commands.find(flow_id);
    if (runtime_it != config.cxparser_runtime_commands.end() && !runtime_it->second.empty()) {
        return "cxparser_runtime." + flow_id;
    }
    const auto profile_it = config.profiles.find(flow_id);
    if (profile_it != config.profiles.end() && !profile_it->second.empty()) {
        return "profile." + flow_id;
    }
    return std::string();
}

struct CxParserFlowSpec {
    const char * flow_id;
    const char * backend_kind;
    const char * safety_class;
    const char * entry_script;
    const char * description;
};

const std::vector<CxParserFlowSpec> & GetCxParserFlowSpecs() {
    static const std::vector<CxParserFlowSpec> specs = {
        {
            "read_text_file_page",
            "builtin_readonly",
            "READ_ONLY",
            "embedded:file.read_text_file_page",
            "Read one page from one text file and return continuation facts."
        },
        {
            "list_directory",
            "builtin_readonly",
            "READ_ONLY",
            "embedded:file.list_directory",
            "List one directory and prepare a stable read manifest."
        },
        {
            "read_directory_files",
            "builtin_readonly",
            "READ_ONLY",
            "embedded:file.read_directory_files",
            "Read directory files one page at a time using cursor facts."
        },
        {
            "cxparser_ext_cxscript_cli",
            "cxparser_runtime",
            "QUEUED_TASK",
            "runtime:cxparser_ext_cxscript_cli",
            "Public cxparser entry. Queue cxparser_ext_cxscript_cli for --script, --script-dir, or --kind --layer --module --case execution."
        },
        {
            "review_cxparser_ext_tests",
            "cxparser_runtime",
            "QUEUED_TASK",
            "runtime:review_cxparser_ext_tests",
            "Legacy/internal compatibility flow. Prefer cxparser_ext_cxscript_cli for public cxparser execution."
        },
        {
            "review_cxparser_ext_library",
            "cxparser_runtime",
            "QUEUED_TASK",
            "runtime:review_cxparser_ext_library",
            "Legacy/internal compatibility flow. Prefer cxparser_ext_cxscript_cli for public cxparser execution."
        },
        {
            "cxparser_ext_clang_tu",
            "clang_indexer",
            "QUEUED_TASK",
            "runtime:cxparser_ext_clang_tu",
            "Clang-based TU semantic indexer. Executes cxparser_clang_indexer for per-TU preprocessing, parsing, semantic analysis, template instantiation, and overload resolution."
        }
    };
    return specs;
}

const CxParserFlowSpec * FindCxParserFlowSpec(const std::string & flow_id) {
    for (const CxParserFlowSpec & spec : GetCxParserFlowSpecs()) {
        if (flow_id == spec.flow_id) {
            return &spec;
        }
    }
    return nullptr;
}

std::string GetDefaultCxParserTestFlowId() {
    return "cxparser_ext_cxscript_cli";
}

bool IsCxParserRuntimeFlow(const std::string & flow_id) {
    return flow_id == "cxparser_ext_cxscript_cli"
        || flow_id == "review_cxparser_ext_tests"
        || flow_id == "review_cxparser_ext_library"
        || flow_id == "cxparser_ext_clang_tu";
}

bool IsLegacyCxParserRuntimeFlow(const std::string & flow_id) {
    return flow_id == "review_cxparser_ext_tests"
        || flow_id == "review_cxparser_ext_library";
}

std::string ExtractCxParserSimpleTestStatement(const std::string & json_text) {
    const std::vector<std::string> keys = {
        "test_statement",
        "test_text",
        "input_text",
        "arguments_text"
    };
    for (const std::string & key : keys) {
        const std::string value = ExtractJsonString(json_text, key);
        if (!value.empty()) {
            return value;
        }
    }
    return std::string();
}

std::string BuildCxParserSimpleTestParamsJson(const std::string & test_statement) {
    std::ostringstream output;
    output << "{"
           << "\"args\":\"" << codex_lan_agent::JsonEscape(test_statement) << "\","
           << "\"arguments_text\":\"" << codex_lan_agent::JsonEscape(test_statement) << "\","
           << "\"test_statement\":\"" << codex_lan_agent::JsonEscape(test_statement) << "\""
           << "}";
    return output.str();
}

std::string AppendCxParserCliOption(
    const std::string & args,
    const std::string & flag,
    const std::string & value) {
    if (value.empty()) {
        return args;
    }
    std::string output = args;
    if (!output.empty()) {
        output += " ";
    }
    output += flag;
    output += " ";
    output += "\"";
    for (const char ch : value) {
        if (ch == '"') {
            output += "\\\"";
        } else {
            output.push_back(ch);
        }
    }
    output += "\"";
    return output;
}

std::string BuildCxParserCxScriptCliArgs(const std::string & params_json) {
    const std::string explicit_args = ExtractJsonString(params_json, "args");
    if (!explicit_args.empty()) {
        return explicit_args;
    }
    const std::string arguments_text = ExtractJsonString(params_json, "arguments_text");
    if (!arguments_text.empty()) {
        return arguments_text;
    }

    std::string args;
    args = AppendCxParserCliOption(
        args,
        "--script",
        FirstNonEmpty(ExtractJsonString(params_json, "script"), ExtractJsonString(params_json, "script_path")));
    args = AppendCxParserCliOption(
        args,
        "--script-dir",
        FirstNonEmpty(ExtractJsonString(params_json, "script_dir"), ExtractJsonString(params_json, "script_directory")));
    args = AppendCxParserCliOption(args, "--kind", ExtractJsonString(params_json, "kind"));
    args = AppendCxParserCliOption(args, "--layer", ExtractJsonString(params_json, "layer"));
    args = AppendCxParserCliOption(args, "--module", ExtractJsonString(params_json, "module"));
    args = AppendCxParserCliOption(args, "--case", ExtractJsonString(params_json, "case"));
    args = AppendCxParserCliOption(args, "--mode", ExtractJsonString(params_json, "mode"));
    args = AppendCxParserCliOption(args, "--route", ExtractJsonString(params_json, "route"));
    args = AppendCxParserCliOption(args, "--report", ExtractJsonString(params_json, "report"));
    args = AppendCxParserCliOption(args, "--trace-id", ExtractJsonString(params_json, "trace_id"));
    if (ExtractJsonBool(params_json, "debug", false)) {
        if (!args.empty()) {
            args += " ";
        }
        args += "--debug";
    }
    return args;
}

bool HasCxParserCxScriptCliRequestShape(const std::string & request_body) {
    return !ExtractJsonString(request_body, "args").empty()
        || !ExtractJsonString(request_body, "arguments_text").empty()
        || !ExtractJsonString(request_body, "script").empty()
        || !ExtractJsonString(request_body, "script_path").empty()
        || !ExtractJsonString(request_body, "script_dir").empty()
        || !ExtractJsonString(request_body, "script_directory").empty()
        || !ExtractJsonString(request_body, "kind").empty()
        || !ExtractJsonString(request_body, "layer").empty()
        || !ExtractJsonString(request_body, "module").empty()
        || !ExtractJsonString(request_body, "case").empty();
}

std::string ResolveRequestedCxParserFlowId(
    const std::string & explicit_flow_id,
    const std::string & request_body) {
    if (HasCxParserCxScriptCliRequestShape(request_body)) {
        return GetDefaultCxParserTestFlowId();
    }
    const std::string test_statement = ExtractCxParserSimpleTestStatement(request_body);
    if (!test_statement.empty()) {
        return GetDefaultCxParserTestFlowId();
    }
    if (!explicit_flow_id.empty()) {
        return IsLegacyCxParserRuntimeFlow(explicit_flow_id)
            ? GetDefaultCxParserTestFlowId()
            : explicit_flow_id;
    }
    return std::string();
}

std::string BuildCxParserFlowFactsJson(const CommandResult & result) {
    std::ostringstream output;
    output << "{"
           << "\"status\":\"" << codex_lan_agent::JsonEscape(GetFieldOrDefault(result, "status", "")) << "\","
           << "\"task_completion\":\"" << codex_lan_agent::JsonEscape(GetFieldOrDefault(result, "task_completion", "")) << "\","
           << "\"read_complete\":\"" << codex_lan_agent::JsonEscape(GetFieldOrDefault(result, "read_complete", "")) << "\","
           << "\"file_complete\":\"" << codex_lan_agent::JsonEscape(GetFieldOrDefault(result, "file_complete", "")) << "\","
           << "\"has_more\":\"" << codex_lan_agent::JsonEscape(GetFieldOrDefault(result, "has_more", "")) << "\","
           << "\"batch_completion\":\"" << codex_lan_agent::JsonEscape(GetFieldOrDefault(result, "batch_completion", "")) << "\","
           << "\"remaining_batch_file_count\":\"" << codex_lan_agent::JsonEscape(GetFieldOrDefault(result, "remaining_batch_file_count", "")) << "\","
           << "\"next_call_json\":\"" << codex_lan_agent::JsonEscape(GetFieldOrDefault(result, "next_call_json", "")) << "\""
           << "}";
    return output.str();
}

std::string BuildCxParserFlowRunCallJson(
    const std::string & flow_id,
    const std::string & params_json,
    const std::string & trace_id,
    const std::string & goal_id) {
    std::ostringstream output;
    output << "{\"name\":\"lan_agent_run_cxparser_flow\",\"arguments\":{"
           << "\"flow_id\":\"" << codex_lan_agent::JsonEscape(flow_id) << "\","
           << "\"params_json\":\"" << codex_lan_agent::JsonEscape(params_json) << "\"";
    if (!trace_id.empty()) {
        output << ",\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\"";
    }
    if (!goal_id.empty()) {
        output << ",\"goal_id\":\"" << codex_lan_agent::JsonEscape(goal_id) << "\"";
    }
    output << "}}";
    return output.str();
}

std::string BuildCxParserReadTextParamsJson(
    const std::string & file_path,
    int max_lines,
    int start_line) {
    std::ostringstream output;
    output << "{\"file_path\":\"" << codex_lan_agent::JsonEscape(file_path) << "\","
           << "\"max_lines\":" << max_lines << ","
           << "\"start_line\":" << start_line << "}";
    return output.str();
}

std::string BuildCxParserReadDirectoryParamsJson(
    const std::string & directory_path,
    const std::string & file_extensions_csv,
    int max_files,
    int max_lines_per_file,
    int max_files_per_call,
    int max_total_lines,
    int file_index,
    int start_line) {
    std::ostringstream output;
    output << "{\"directory_path\":\"" << codex_lan_agent::JsonEscape(directory_path) << "\","
           << "\"file_extensions_csv\":\"" << codex_lan_agent::JsonEscape(file_extensions_csv) << "\","
           << "\"max_files\":" << max_files << ","
           << "\"max_lines_per_file\":" << max_lines_per_file << ","
           << "\"max_files_per_call\":" << max_files_per_call << ","
           << "\"max_total_lines\":" << max_total_lines << ","
           << "\"file_index\":" << file_index << ","
           << "\"start_line\":" << start_line << "}";
    return output.str();
}

CommandResult BuildCxParserFlowCatalogResult(const AgentConfig * config) {
    CommandResult result;
    int index = 0;
    int runtime_flow_count = 0;
    int runtime_binding_ready_count = 0;
    std::ostringstream flow_ids;
    flow_ids << "[";
    for (const CxParserFlowSpec & spec : GetCxParserFlowSpecs()) {
        if (index != 0) {
            flow_ids << ",";
        }
        flow_ids << "\"" << codex_lan_agent::JsonEscape(spec.flow_id) << "\"";
        const std::string prefix = "flow_" + std::to_string(index) + "_";
        result.fields[prefix + "flow_id"] = spec.flow_id;
        result.fields[prefix + "backend_kind"] = spec.backend_kind;
        result.fields[prefix + "safety_class"] = spec.safety_class;
        result.fields[prefix + "entry_script"] = spec.entry_script;
        result.fields[prefix + "description"] = spec.description;
        if (std::string(spec.backend_kind) == "cxparser_runtime") {
            ++runtime_flow_count;
            if (config != nullptr) {
                const std::string binding_source =
                    ResolveCxParserRuntimeBindingSourceForFlow(*config, spec.flow_id);
                const std::string binding_entrypoint =
                    ResolveCxParserRuntimeBindingEntrypointForFlow(*config, spec.flow_id);
                const bool binding_available = !binding_source.empty();
                result.fields[prefix + "runtime_binding_available"] = binding_available ? "true" : "false";
                result.fields[prefix + "runtime_binding_source"] = binding_source;
                result.fields[prefix + "runtime_binding_entrypoint"] = binding_entrypoint;
                if (binding_available) {
                    ++runtime_binding_ready_count;
                }
            }
        }
        ++index;
    }
    flow_ids << "]";
    result.fields["flow_count"] = std::to_string(index);
    result.fields["runtime_flow_count"] = std::to_string(runtime_flow_count);
    if (config != nullptr) {
        result.fields["runtime_binding_ready_count"] = std::to_string(runtime_binding_ready_count);
    }
    result.fields["flow_ids_json"] = flow_ids.str();
    result.fields["public_entry_flow_id"] = "cxparser_ext_cxscript_cli";
    result.fields["public_entry_contract"] = "--script | --script-dir | --kind --layer --module --case";
    result.fields["result"] = "cxparser_flow_catalog";
    result.fields["summary"] = "cxparser flow catalog returned";
    result.fields["cxparser_status"] = "success";
    result.fields["status"] = "success";
    return result;
}

CommandResult ValidateCxParserFlowResult(
    const AgentConfig * config,
    const std::string & flow_id,
    const std::string & params_json) {
    CommandResult result;
    result.fields["flow_id"] = flow_id;
    const CxParserFlowSpec * spec = FindCxParserFlowSpec(flow_id);
    if (spec == nullptr) {
        result.ok = false;
        result.exit_code = 72;
        result.fields["cxparser_status"] = "failed";
        result.fields["status"] = "failed";
        result.fields["error"] = "cxparser flow is not registered";
        result.fields["result"] = "cxparser_flow_not_registered";
        return result;
    }
    result.fields["cxparser_status"] = "success";
    result.fields["status"] = "success";
    result.fields["flow_registered"] = "true";
    result.fields["backend_kind"] = spec->backend_kind;
    result.fields["safety_class"] = spec->safety_class;
    result.fields["flow_safety_class"] = spec->safety_class;
    result.fields["entry_script"] = spec->entry_script;
    result.fields["params_json"] = params_json;
    if (std::string(spec->backend_kind) == "cxparser_runtime" && config != nullptr) {
        const std::string binding_source =
            ResolveCxParserRuntimeBindingSourceForFlow(*config, flow_id);
        result.fields["runtime_binding_available"] = binding_source.empty() ? "false" : "true";
        result.fields["runtime_binding_source"] = binding_source;
        result.fields["runtime_binding_entrypoint"] =
            ResolveCxParserRuntimeBindingEntrypointForFlow(*config, flow_id);
    }
    result.fields["result"] = "cxparser_flow_valid";
    result.fields["summary"] = "cxparser flow validation passed";
    return result;
}

std::string SelectCxParserFlowParamsJson(
    const std::string & request_body,
    const std::string & flow_id) {
    const std::string params_json = ExtractJsonRawValue(request_body, "params");
    if (!params_json.empty()) {
        return params_json;
    }
    const std::string params_json_text = ExtractJsonString(request_body, "params_json");
    if (!params_json_text.empty()) {
        return params_json_text;
    }
    if (IsCxParserRuntimeFlow(flow_id)) {
        const std::string test_statement = ExtractCxParserSimpleTestStatement(request_body);
        if (!test_statement.empty()) {
            return BuildCxParserSimpleTestParamsJson(test_statement);
        }
    }
    return request_body;
}

std::string ExtractQueuedCxParserFlowArgs(const std::string & params_json) {
    const std::string cli_args = BuildCxParserCxScriptCliArgs(params_json);
    if (!cli_args.empty()) {
        return cli_args;
    }
    const std::string args = ExtractJsonString(params_json, "args");
    if (!args.empty()) {
        return args;
    }
    const std::string arguments_text = ExtractJsonString(params_json, "arguments_text");
    if (!arguments_text.empty()) {
        return arguments_text;
    }
    const std::string test_statement = ExtractCxParserSimpleTestStatement(params_json);
    if (!test_statement.empty()) {
        return test_statement;
    }
    return std::string();
}

CommandResult RunCxParserFlowResult(
    const AgentConfig & config,
    const std::string & flow_id,
    const std::string & request_body,
    const std::string & trace_id,
    const std::string & goal_id) {
    const std::string resolved_flow_id =
        ResolveRequestedCxParserFlowId(flow_id, request_body);
    const std::string simple_test_statement =
        ExtractCxParserSimpleTestStatement(request_body);
    if (resolved_flow_id.empty()) {
        CommandResult result;
        result.ok = false;
        result.exit_code = 400;
        result.fields["status"] = "failed";
        result.fields["cxparser_status"] = "failed";
        result.fields["error"] = "flow_id or test_statement is required";
        result.fields["result"] = "cxparser_flow_request_invalid";
        result.fields["summary"] = "cxparser request is missing both flow_id and test_statement";
        result.fields["assistant_response_allowed"] = "false";
        result.fields["final_answer_allowed"] = "false";
        result.fields["supervision_status"] = "alarm";
        result.fields["supervision_alarm"] = "true";
        result.fields["supervision_alarm_code"] = "CXPARSER_FLOW_REQUEST_INVALID";
        result.fields["supervision_alarm_message"] = "cxparser request is missing both flow_id and test_statement.";
        ApplyCxParserEncodingAdvisory(&result, simple_test_statement);
        return result;
    }
    const CxParserFlowSpec * spec = FindCxParserFlowSpec(resolved_flow_id);
    if (spec == nullptr) {
        CommandResult result = ValidateCxParserFlowResult(nullptr, resolved_flow_id, std::string());
        result.fields["assistant_response_allowed"] = "false";
        result.fields["final_answer_allowed"] = "false";
        result.fields["supervision_status"] = "alarm";
        result.fields["supervision_alarm"] = "true";
        result.fields["supervision_alarm_code"] = "CXPARSER_FLOW_NOT_REGISTERED";
        result.fields["supervision_alarm_message"] = "cxparser flow is not registered.";
        if (!simple_test_statement.empty()) {
            result.fields["test_statement"] = simple_test_statement;
            result.fields["test_input_mode"] = "simple_test_statement";
        }
        ApplyCxParserEncodingAdvisory(&result, simple_test_statement);
        return result;
    }

    const std::string params_json =
        SelectCxParserFlowParamsJson(request_body, resolved_flow_id);
    CommandResult result;
    int max_lines = 500;
    int start_line = 1;
    int max_files = 200;
    int max_lines_per_file = 500;
    int max_files_per_call = 1;
    int max_total_lines = 500;
    int file_index = 0;
    const std::string directory_path = ExtractJsonString(params_json, "directory_path");
    const std::string file_extensions_csv = ExtractJsonString(params_json, "file_extensions_csv");

    if (IsCxParserRuntimeFlow(resolved_flow_id)) {
        if (g_task_manager == nullptr) {
            result.ok = false;
            result.exit_code = 41;
            result.fields["status"] = "failed";
            result.fields["error"] = "task manager is not active";
            result.fields["result"] = "cxparser_runtime_task_manager_unavailable";
            result.fields["summary"] = "cxparser runtime flow could not be queued";
        } else {
            std::string runtime_source;
            if (!HasCxParserRuntimeBinding(config, resolved_flow_id, &runtime_source)) {
                result.ok = false;
                result.exit_code = 74;
                result.fields["status"] = "failed";
                result.fields["error"] = "cxparser runtime binding is not configured";
                result.fields["result"] = "cxparser_runtime_binding_missing";
                result.fields["summary"] = "cxparser runtime flow is registered but its runtime binding is not configured";
                result.fields["next_action"] = "load cxparser_runtime." + resolved_flow_id + " or profile." + resolved_flow_id + " into codex-lan-agent config and retry";
            } else {
                const std::string queued_args = ExtractQueuedCxParserFlowArgs(params_json);
                const std::string task_id = g_task_manager->EnqueueCxParserRuntime(
                    resolved_flow_id,
                    queued_args);
                result = BuildQueuedTaskResult(task_id);
                result.fields["status"] = "success";
                result.fields["task_completion"] = "complete";
                result.fields["continue_required"] = "false";
                result.fields["auto_continue_required"] = "false";
                result.fields["analysis_allowed"] = "true";
                result.fields["result"] = "cxparser_runtime_task_queued";
                result.fields["summary"] = simple_test_statement.empty()
                    ? "cxparser runtime flow queued"
                    : "cxparser test statement queued";
                result.fields["cxparser_runtime_source"] = runtime_source;
                result.fields["cxparser_runtime_arguments"] = queued_args;
                result.fields["cxparser_runtime_arguments_source"] =
                    resolved_flow_id == "cxparser_ext_cxscript_cli"
                        ? "cxscript_cli_mapped_args"
                        : "legacy_runtime_args";
            }
        }
    } else if (resolved_flow_id == "read_text_file_page") {
        const std::string max_lines_raw = ExtractJsonRawValue(params_json, "max_lines");
        if (!max_lines_raw.empty()) {
            const int parsed = std::atoi(max_lines_raw.c_str());
            max_lines = parsed > 0 ? parsed : 1;
        }
        const std::string start_line_raw = ExtractJsonRawValue(params_json, "start_line");
        if (!start_line_raw.empty()) {
            const int parsed = std::atoi(start_line_raw.c_str());
            start_line = parsed > 0 ? parsed : 1;
        }
        std::size_t start_byte_offset = 0;
        const std::string start_byte_offset_raw = ExtractJsonRawValue(params_json, "start_byte_offset");
        if (!start_byte_offset_raw.empty()) {
            const long long parsed = std::atoll(start_byte_offset_raw.c_str());
            start_byte_offset = parsed > 0 ? static_cast<std::size_t>(parsed) : static_cast<std::size_t>(0);
        }
        result = ReadTextFileResult(
            config,
            ExtractJsonString(params_json, "file_path"),
            max_lines,
            start_line,
            trace_id,
            start_byte_offset,
            ExtractJsonString(params_json, "probe_ref"));
    } else if (resolved_flow_id == "list_directory") {
        int max_entries = 200;
        const std::string max_entries_raw = ExtractJsonRawValue(params_json, "max_entries");
        if (!max_entries_raw.empty()) {
            const int parsed = std::atoi(max_entries_raw.c_str());
            max_entries = parsed > 0 ? parsed : 1;
        }
        result = ListDirectoryResult(
            config,
            ExtractJsonString(params_json, "directory_path"),
            max_entries,
            trace_id);
    } else if (resolved_flow_id == "read_directory_files") {
        const std::string max_files_raw = ExtractJsonRawValue(params_json, "max_files");
        if (!max_files_raw.empty()) {
            const int parsed = std::atoi(max_files_raw.c_str());
            max_files = parsed > 0 ? parsed : 1;
        }
        const std::string max_lines_raw = ExtractJsonRawValue(params_json, "max_lines_per_file");
        if (!max_lines_raw.empty()) {
            const int parsed = std::atoi(max_lines_raw.c_str());
            max_lines_per_file = parsed > 0 ? parsed : 1;
        }
        const std::string max_files_per_call_raw = ExtractJsonRawValue(params_json, "max_files_per_call");
        if (!max_files_per_call_raw.empty()) {
            const int parsed = std::atoi(max_files_per_call_raw.c_str());
            max_files_per_call = parsed > 0 ? parsed : 1;
        }
        const std::string max_total_lines_raw = ExtractJsonRawValue(params_json, "max_total_lines");
        if (!max_total_lines_raw.empty()) {
            const int parsed = std::atoi(max_total_lines_raw.c_str());
            max_total_lines = parsed > 0 ? parsed : 1;
        }
        const std::string file_index_raw = ExtractJsonRawValue(params_json, "file_index");
        if (!file_index_raw.empty()) {
            file_index = std::max(0, std::atoi(file_index_raw.c_str()));
        }
        const std::string start_line_raw = ExtractJsonRawValue(params_json, "start_line");
        if (!start_line_raw.empty()) {
            const int parsed = std::atoi(start_line_raw.c_str());
            start_line = parsed > 0 ? parsed : 1;
        }
        std::size_t start_byte_offset = 0;
        const std::string start_byte_offset_raw = ExtractJsonRawValue(params_json, "start_byte_offset");
        if (!start_byte_offset_raw.empty()) {
            const long long parsed = std::atoll(start_byte_offset_raw.c_str());
            start_byte_offset = parsed > 0 ? static_cast<std::size_t>(parsed) : static_cast<std::size_t>(0);
        }
        result = ReadDirectoryFilesResult(
            config,
            directory_path,
            file_extensions_csv,
            max_files,
            max_lines_per_file,
            max_files_per_call,
            max_total_lines,
            file_index,
            start_line,
            trace_id,
            start_byte_offset);
    }

    result.fields["flow_id"] = resolved_flow_id;
    result.fields["public_entry_flow_id"] = "cxparser_ext_cxscript_cli";
    result.fields["public_entry_contract"] = "--script | --kind --layer --module --case";
    result.fields["cxparser_public_build_root"] =
        (std::filesystem::path(config.workspace_root) / "cxparser" / "build").string();
    result.fields["cxparser_public_build_contract"] = "single_public_build_directory";
    result.fields["cxparser_public_entry"] =
        resolved_flow_id == "cxparser_ext_cxscript_cli" ? "true" : "false";
    result.fields["cxparser_status"] = result.ok ? "success" : "failed";
    result.fields["cxparser_flow_registered"] = "true";
    result.fields["cxparser_backend_kind"] = spec->backend_kind;
    result.fields["backend_kind"] = spec->backend_kind;
    result.fields["cxparser_safety_class"] = spec->safety_class;
    result.fields["flow_safety_class"] = spec->safety_class;
    result.fields["cxparser_entry_script"] = spec->entry_script;
    result.fields["cxparser_params_json"] = params_json;
    if (!simple_test_statement.empty()) {
        result.fields["test_statement"] = simple_test_statement;
        result.fields["test_input_mode"] = "simple_test_statement";
        result.fields["flow_resolution"] = flow_id.empty()
            ? "default_from_test_statement"
            : "explicit_flow_with_test_statement";
    }
    ApplyCxParserEncodingAdvisory(&result, simple_test_statement);
    result.fields["facts_json"] = BuildCxParserFlowFactsJson(result);
    result.fields["candidate_next_action_json"] = GetFieldOrDefault(result, "next_call_json", "");
    if (IsCxParserRuntimeFlow(resolved_flow_id)) {
        const std::string binding_source =
            ResolveCxParserRuntimeBindingSourceForFlow(config, resolved_flow_id);
        result.fields["cxparser_task_kind"] = "cxparser_runtime";
        result.fields["runtime_binding_available"] = binding_source.empty() ? "false" : "true";
        result.fields["runtime_binding_source"] = binding_source;
        result.fields["runtime_binding_entrypoint"] =
            ResolveCxParserRuntimeBindingEntrypointForFlow(config, resolved_flow_id);
        result.fields["runtime_binding_contract"] = "cxparser_public_build_only";
        result.fields["task_profile"] = resolved_flow_id;
        if (GetFieldOrDefault(result, "result_ref", "").empty()) {
            result.fields["result_ref"] = GetFieldOrDefault(result, "resolved_log_path", "");
        }
        if (GetFieldOrDefault(result, "evidence_ref", "").empty()) {
            result.fields["evidence_ref"] = FirstNonEmpty(
                GetFieldOrDefault(result, "trace_log_path", ""),
                GetFieldOrDefault(result, "resolved_log_path", ""));
        }
    }
    if (resolved_flow_id == "read_text_file_page"
        && GetFieldOrDefault(result, "has_more", "false") == "true") {
        const int next_start_line = std::max(1, std::atoi(GetFieldOrDefault(result, "next_start_line", "1").c_str()));
        const std::string next_params = BuildCxParserReadTextParamsJson(
            ExtractJsonString(params_json, "file_path"),
            max_lines,
            next_start_line);
        result.fields["next_call_json"] = BuildCxParserFlowRunCallJson(resolved_flow_id, next_params, trace_id, goal_id);
        result.fields["candidate_next_action_json"] = result.fields["next_call_json"];
    } else if (resolved_flow_id == "read_directory_files"
               && GetFieldOrDefault(result, "batch_completion", "") == "incomplete") {
        const int next_file_index = std::max(0, std::atoi(GetFieldOrDefault(result, "next_file_index", "0").c_str()));
        const int next_start_line = std::max(1, std::atoi(GetFieldOrDefault(result, "next_start_line", "1").c_str()));
        const std::string next_params = BuildCxParserReadDirectoryParamsJson(
            directory_path,
            file_extensions_csv,
            max_files,
            max_lines_per_file,
            max_files_per_call,
            max_total_lines,
            next_file_index,
            next_start_line);
        result.fields["next_call_json"] = BuildCxParserFlowRunCallJson(resolved_flow_id, next_params, trace_id, goal_id);
        result.fields["candidate_next_action_json"] = result.fields["next_call_json"];
    }
    if (!trace_id.empty() && GetFieldOrDefault(result, "trace_id", "").empty()) {
        result.fields["trace_id"] = trace_id;
    }
    if (!goal_id.empty()) {
        result.fields["goal_id"] = goal_id;
    }
    return result;
}
