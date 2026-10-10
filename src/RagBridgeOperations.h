#pragma once

std::string DeriveRagBridgeBaseUrl(const AgentConfig & config) {
    const std::string endpoint = config.generation_endpoint;
    if (endpoint.empty()) {
        return std::string();
    }

    const std::string marker = "/v1/chat/completions";
    const std::size_t marker_pos = endpoint.find(marker);
    if (marker_pos != std::string::npos) {
        return endpoint.substr(0, marker_pos);
    }

    const std::size_t scheme_pos = endpoint.find("://");
    if (scheme_pos == std::string::npos) {
        return endpoint;
    }

    const std::size_t path_pos = endpoint.find('/', scheme_pos + 3);
    return path_pos == std::string::npos
        ? endpoint
        : endpoint.substr(0, path_pos);
}

void CopyRagStatusField(
    CommandResult * result,
    const std::string & response_body,
    const std::string & key,
    bool raw = true) {
    if (result == nullptr) {
        return;
    }
    const std::string value = raw
        ? ExtractJsonRawValue(response_body, key)
        : ExtractJsonString(response_body, key);
    if (!value.empty()) {
        result->fields[key] = value;
    }
}

CommandResult BuildRagIndexStatusResult(const AgentConfig & config) {
    CommandResult result;
    const std::string base_url = DeriveRagBridgeBaseUrl(config);
    if (base_url.empty()) {
        result.ok = false;
        result.exit_code = 91;
        result.fields["error"] = "generation_endpoint is not configured";
        result.fields["next_action"] = "configure generation_endpoint before querying rag bridge status";
        result.fields["clips_meta"] = "false";
        result.fields["capabilities"] = "[\"rag_add\",\"rag_search\",\"rag_status\"]";
        return result;
    }

    const std::string endpoint = base_url + "/rag/index/status";
    const codex_lan_agent::HttpResponse response = codex_lan_agent::GetUrl(endpoint, 10000);
    const std::string log_path = BuildLogPath(config, "rag_index_status");
    std::ofstream output(log_path, std::ios::out | std::ios::trunc);
    output << "endpoint=" << endpoint << "\n";
    output << "status_code=" << response.status_code << "\n";
    output << "ok=" << (response.ok ? "true" : "false") << "\n";
    output << "error=" << response.error_message << "\n";
    output << "body=\n" << response.body << "\n";

    result.ok = response.ok;
    result.exit_code = response.ok ? 0 : 92;
    result.fields["upstream_path"] = "/rag/index/status";
    result.fields["rag_bridge_base_url"] = base_url;
    result.fields["status_code"] = std::to_string(response.status_code);
    result.fields["log_path"] = log_path;
    result.fields["body"] = response.body;
    result.fields["clips_meta"] = "false";
    result.fields["capabilities"] = "[\"rag_add\",\"rag_search\",\"rag_status\"]";
    result.fields["semantic_outcome"] = response.ok ? "rag_status_ready" : "rag_status_unavailable";
    result.fields["next_action"] = response.ok
        ? "clips meta is not advertised by default; query lan_agent_rag_clips_meta or implement upstream /rag/clips/meta"
        : "inspect log_path and upstream rag service availability";
    if (!response.error_message.empty()) {
        result.fields["error"] = response.error_message;
    }

    if (!response.body.empty()) {
        CopyRagStatusField(&result, response.body, "enabled");
        CopyRagStatusField(&result, response.body, "ready");
        CopyRagStatusField(&result, response.body, "status", false);
        CopyRagStatusField(&result, response.body, "pending");
        CopyRagStatusField(&result, response.body, "active");
        CopyRagStatusField(&result, response.body, "jobs_completed");
        CopyRagStatusField(&result, response.body, "docs_completed");
        CopyRagStatusField(&result, response.body, "metadata_completed");
        CopyRagStatusField(&result, response.body, "chunk_count");
        CopyRagStatusField(&result, response.body, "last_job_kind", false);
        CopyRagStatusField(&result, response.body, "last_reset_before_add");
        CopyRagStatusField(&result, response.body, "last_error", false);
        const std::string upstream_capabilities = ExtractJsonRawValue(response.body, "capabilities");
        if (!upstream_capabilities.empty()) {
            result.fields["capabilities"] = upstream_capabilities;
        }
        const std::string upstream_clips_meta = ExtractJsonRawValue(response.body, "clips_meta");
        if (!upstream_clips_meta.empty()) {
            result.fields["clips_meta"] = upstream_clips_meta;
        }
        const std::string clips_rule_domains = ExtractJsonRawValue(response.body, "clips_rule_domains");
        if (!clips_rule_domains.empty()) {
            result.fields["clips_rule_domains"] = clips_rule_domains;
        }
        const std::string embedded_fallback = ExtractJsonRawValue(response.body, "embedded_clips_fallback");
        if (!embedded_fallback.empty()) {
            result.fields["embedded_clips_fallback"] = embedded_fallback;
        }
    }

    if (GetFieldOrDefault(result, "clips_meta", "false") != "true") {
        result.fields["clips_meta_supported"] = "false";
        result.fields["clips_meta_reason"] = "upstream rag index status does not advertise clips_meta capability";
    } else {
        result.fields["clips_meta_supported"] = "true";
    }
    return result;
}

CommandResult BuildRagClipsMetaResult(
    const AgentConfig & config,
    const std::string & query,
    int top_k) {
    CommandResult result;
    const std::string base_url = DeriveRagBridgeBaseUrl(config);
    if (base_url.empty()) {
        result.ok = false;
        result.exit_code = 93;
        result.fields["error"] = "generation_endpoint is not configured";
        result.fields["clips_meta"] = "false";
        result.fields["next_action"] = "configure generation_endpoint before querying rag clips meta";
        return result;
    }

    const std::string endpoint = base_url + "/rag/clips/meta";
    std::ostringstream body;
    body << "{"
         << "\"query\":\"" << codex_lan_agent::JsonEscape(query) << "\","
         << "\"top_k\":" << std::max(1, top_k)
         << "}";

    const codex_lan_agent::HttpResponse response =
        codex_lan_agent::PostJson(endpoint, body.str(), 10000);
    const std::string log_path = BuildLogPath(config, "rag_clips_meta");
    std::ofstream output(log_path, std::ios::out | std::ios::trunc);
    output << "endpoint=" << endpoint << "\n";
    output << "request_body=\n" << body.str() << "\n";
    output << "status_code=" << response.status_code << "\n";
    output << "ok=" << (response.ok ? "true" : "false") << "\n";
    output << "error=" << response.error_message << "\n";
    output << "body=\n" << response.body << "\n";

    result.ok = response.ok;
    result.exit_code = response.ok ? 0 : 94;
    result.fields["upstream_path"] = "/rag/clips/meta";
    result.fields["rag_bridge_base_url"] = base_url;
    result.fields["status_code"] = std::to_string(response.status_code);
    result.fields["query"] = query;
    result.fields["top_k"] = std::to_string(std::max(1, top_k));
    result.fields["log_path"] = log_path;
    result.fields["body"] = response.body;
    result.fields["fact_schema_id"] = "mcp_fact_schema_v1";
    result.fields["decision_schema_id"] = "clips_decision_schema_v1";

    if (!response.error_message.empty()) {
        result.fields["error"] = response.error_message;
    }

    if (response.status_code == 404) {
        result.ok = false;
        result.exit_code = 95;
        result.fields["clips_meta"] = "false";
        result.fields["semantic_outcome"] = "clips_meta_unavailable";
        result.fields["fact_bundle"] = "{}";
        result.fields["serialized_assertions"] = "[]";
        result.fields["capabilities"] = "[\"rag_add\",\"rag_search\",\"rag_status\"]";
        result.fields["next_action"] = "upstream /rag/clips/meta is not implemented; expose clips_meta=false in /rag/index/status or implement the route";
        if (result.fields["error"].empty()) {
            result.fields["error"] = "upstream /rag/clips/meta returned 404";
        }
        return result;
    }

    result.fields["clips_meta"] = response.ok ? "true" : "false";
    result.fields["semantic_outcome"] = response.ok ? "clips_meta_ready" : "clips_meta_failed";
    result.fields["next_action"] = response.ok
        ? "consume fact_bundle and serialized_assertions"
        : "inspect log_path and upstream rag service implementation";

    if (!response.body.empty()) {
        const std::vector<std::string> string_fields = {
            "decision", "verification", "reason_code", "matched_rule",
            "route_target", "next_action"
        };
        for (const std::string & key : string_fields) {
            const std::string value = ExtractJsonString(response.body, key);
            if (!value.empty()) {
                result.fields[key] = value;
            }
        }

        const std::vector<std::string> raw_fields = {
            "fact_bundle", "serialized_assertions", "clips_rule_domains",
            "capabilities"
        };
        for (const std::string & key : raw_fields) {
            const std::string value = ExtractJsonRawValue(response.body, key);
            if (!value.empty()) {
                result.fields[key] = value;
            }
        }
    }

    if (GetFieldOrDefault(result, "fact_bundle", "").empty()) {
        result.fields["fact_bundle"] = "{}";
    }
    if (GetFieldOrDefault(result, "serialized_assertions", "").empty()) {
        result.fields["serialized_assertions"] = "[]";
    }
    return result;
}

bool RagBridgePathExists(const std::string & path) {
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::exists(std::filesystem::path(path), ec) && !ec;
}

std::vector<std::string> ExtractTopLevelJsonArrayItemsLocal(const std::string & array_text) {
    std::vector<std::string> items;
    const std::string trimmed = Trim(array_text);
    if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']') {
        return items;
    }

    bool in_string = false;
    bool escaping = false;
    int object_depth = 0;
    int array_depth = 0;
    std::size_t item_start = std::string::npos;
    for (std::size_t index = 1; index + 1 < trimmed.size(); ++index) {
        const char current = trimmed[index];
        if (escaping) {
            escaping = false;
            continue;
        }
        if (current == '\\' && in_string) {
            escaping = true;
            continue;
        }
        if (current == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (current == '{') {
            if (object_depth == 0 && array_depth == 0 && item_start == std::string::npos) {
                item_start = index;
            }
            ++object_depth;
            continue;
        }
        if (current == '[') {
            if (object_depth == 0 && array_depth == 0 && item_start == std::string::npos) {
                item_start = index;
            }
            ++array_depth;
            continue;
        }
        if (current == '}') {
            --object_depth;
        } else if (current == ']') {
            --array_depth;
        }
        if (object_depth == 0 && array_depth == 0) {
            if (current == ',' && item_start != std::string::npos) {
                items.push_back(Trim(trimmed.substr(item_start, index - item_start)));
                item_start = std::string::npos;
            } else if (item_start == std::string::npos && !std::isspace(static_cast<unsigned char>(current))) {
                item_start = index;
            }
        }
    }
    if (item_start != std::string::npos && item_start + 1 < trimmed.size()) {
        items.push_back(Trim(trimmed.substr(item_start, trimmed.size() - 1 - item_start)));
    }
    return items;
}

void CopyJsonStringFieldIfPresent(
    CommandResult * result,
    const std::string & response_body,
    const std::string & key) {
    if (result == nullptr) {
        return;
    }
    const std::string value = ExtractJsonString(response_body, key);
    if (!value.empty()) {
        result->fields[key] = value;
    }
}

void CopyJsonRawFieldIfPresent(
    CommandResult * result,
    const std::string & response_body,
    const std::string & key) {
    if (result == nullptr) {
        return;
    }
    const std::string value = ExtractJsonRawValue(response_body, key);
    if (!value.empty()) {
        result->fields[key] = value;
    }
}

CommandResult BuildRagClipsRunResult(
    const AgentConfig & config,
    const std::string & query,
    int top_k,
    const std::string & request_id,
    const std::string & trace_id,
    const std::string & query_id,
    int timeout_ms) {
    CommandResult result;
    const std::string base_url = DeriveRagBridgeBaseUrl(config);
    if (base_url.empty()) {
        result.ok = false;
        result.exit_code = 96;
        result.fields["error"] = "generation_endpoint is not configured";
        result.fields["next_action"] = "configure generation_endpoint before running rag clips";
        return result;
    }

    const std::string endpoint = base_url + "/rag/clips/run";
    std::ostringstream body;
    body << "{"
         << "\"query\":\"" << codex_lan_agent::JsonEscape(query) << "\","
         << "\"top_k\":" << std::max(1, top_k);
    if (!request_id.empty()) {
        body << ",\"request_id\":\"" << codex_lan_agent::JsonEscape(request_id) << "\"";
    }
    if (!trace_id.empty()) {
        body << ",\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\"";
    }
    if (!query_id.empty()) {
        body << ",\"query_id\":\"" << codex_lan_agent::JsonEscape(query_id) << "\"";
    }
    body << "}";

    const codex_lan_agent::HttpResponse response =
        codex_lan_agent::PostJson(endpoint, body.str(), std::max(1000, timeout_ms));
    const std::string log_path = BuildLogPath(config, "rag_clips_run");
    std::ofstream output(log_path, std::ios::out | std::ios::trunc);
    output << "endpoint=" << endpoint << "\n";
    output << "request_body=\n" << body.str() << "\n";
    output << "status_code=" << response.status_code << "\n";
    output << "ok=" << (response.ok ? "true" : "false") << "\n";
    output << "error=" << response.error_message << "\n";
    output << "body=\n" << response.body << "\n";

    result.ok = response.ok;
    result.exit_code = response.ok ? 0 : 97;
    result.fields["upstream_path"] = "/rag/clips/run";
    result.fields["rag_clips_run_contract"] =
        "direct_post_to_upstream_rag_clips_run_with_verified_store_refs";
    result.fields["rag_clips_run_profile_dependency"] = "false";
    result.fields["rag_bridge_base_url"] = base_url;
    result.fields["status_code"] = std::to_string(response.status_code);
    result.fields["query"] = query;
    result.fields["top_k"] = std::to_string(std::max(1, top_k));
    result.fields["log_path"] = log_path;
    result.fields["body"] = response.body;
    result.fields["request_id"] = FirstNonEmpty(
        ExtractJsonString(response.body, "request_id"),
        request_id);
    result.fields["trace_id"] = FirstNonEmpty(
        ExtractJsonString(response.body, "trace_id"),
        trace_id);
    result.fields["query_id"] = FirstNonEmpty(
        ExtractJsonString(response.body, "query_id"),
        query_id);
    if (!response.error_message.empty()) {
        result.fields["error"] = response.error_message;
    }

    if (!response.ok) {
        result.fields["semantic_outcome"] = "rag_clips_run_failed";
        result.fields["next_action"] = "inspect log_path and upstream /rag/clips/run availability";
        return result;
    }

    result.fields["semantic_outcome"] = "rag_clips_run_ready";
    result.fields["next_action"] = "consume store_refs and query trace snapshot or query index if downstream validation is needed";
    const std::vector<std::string> string_fields = {
        "backend", "message", "rules_dir", "manifest_path", "rule_set_id"
    };
    for (const std::string & key : string_fields) {
        const std::string value = ExtractJsonString(response.body, key);
        if (!value.empty()) {
            result.fields[key] = value;
        }
    }
    const std::vector<std::string> raw_fields = {
        "admission_report", "admission_layer_input", "admission_summary",
        "runner_diagnostics", "store_refs"
    };
    for (const std::string & key : raw_fields) {
        const std::string value = ExtractJsonObjectRaw(response.body, key);
        if (!value.empty()) {
            result.fields[key] = value;
        }
    }

    const std::string store_refs = GetFieldOrDefault(result, "store_refs", "");
    result.fields["store_refs_present"] = store_refs.empty() ? "false" : "true";
    bool run_snapshot_exists = false;
    bool query_index_exists = false;
    int slice_index_ref_count = 0;
    int slice_index_verified_count = 0;
    std::ostringstream verified_slice_refs;
    std::ostringstream missing_slice_refs;
    bool first_verified_slice = true;
    bool first_missing_slice = true;
    if (!store_refs.empty()) {
        const std::string run_snapshot_path = ExtractJsonString(store_refs, "run_snapshot_path");
        const std::string fact_page_path = ExtractJsonString(store_refs, "fact_page_path");
        const std::string query_index_path = ExtractJsonString(store_refs, "query_index_path");
        const std::string clips_runs_log_path = ExtractJsonString(store_refs, "clips_runs_log_path");
        const std::string slice_index_refs = ExtractJsonObjectRaw(store_refs, "slice_index_refs");
        result.fields["run_snapshot_path"] = run_snapshot_path;
        result.fields["fact_page_path"] = fact_page_path;
        result.fields["query_index_path"] = query_index_path;
        result.fields["clips_runs_log_path"] = clips_runs_log_path;

        run_snapshot_exists = RagBridgePathExists(run_snapshot_path);
        query_index_exists = RagBridgePathExists(query_index_path);
        result.fields["run_snapshot_exists"] = run_snapshot_exists ? "true" : "false";
        result.fields["query_index_exists"] = query_index_exists ? "true" : "false";
        result.fields["fact_page_exists"] = RagBridgePathExists(fact_page_path) ? "true" : "false";
        result.fields["clips_runs_log_exists"] = RagBridgePathExists(clips_runs_log_path) ? "true" : "false";

        const std::vector<std::string> slice_items = ExtractTopLevelJsonArrayItemsLocal(slice_index_refs);
        slice_index_ref_count = static_cast<int>(slice_items.size());
        for (const std::string & item : slice_items) {
            const std::string index_path = ExtractJsonString(item, "index_path");
            if (index_path.empty()) {
                continue;
            }
            const bool exists = RagBridgePathExists(index_path);
            if (exists) {
                ++slice_index_verified_count;
                if (!first_verified_slice) {
                    verified_slice_refs << ",";
                }
                verified_slice_refs << item;
                first_verified_slice = false;
            } else {
                if (!first_missing_slice) {
                    missing_slice_refs << ",";
                }
                missing_slice_refs << item;
                first_missing_slice = false;
            }
        }
        result.fields["slice_index_refs_json"] = slice_index_refs.empty() ? "[]" : slice_index_refs;
        result.fields["slice_index_ref_count"] = std::to_string(slice_index_ref_count);
        result.fields["slice_index_verified_count"] = std::to_string(slice_index_verified_count);
        result.fields["slice_index_missing_count"] =
            std::to_string(std::max(0, slice_index_ref_count - slice_index_verified_count));
        result.fields["verified_slice_index_refs_json"] =
            first_verified_slice ? "[]" : ("[" + verified_slice_refs.str() + "]");
        result.fields["missing_slice_index_refs_json"] =
            first_missing_slice ? "[]" : ("[" + missing_slice_refs.str() + "]");
        result.fields["store_refs_verified"] =
            (run_snapshot_exists && query_index_exists && slice_index_verified_count == slice_index_ref_count)
                ? "true"
                : "false";
        result.fields["store_refs_verification_summary"] =
            "run_snapshot=" + std::string(run_snapshot_exists ? "true" : "false")
            + ",query_index=" + std::string(query_index_exists ? "true" : "false")
            + ",slice_index_verified_count=" + std::to_string(slice_index_verified_count)
            + "/" + std::to_string(slice_index_ref_count);
        result.fields["result_ref"] = !run_snapshot_path.empty() ? run_snapshot_path : log_path;
        result.fields["evidence_ref"] = !query_index_path.empty() ? query_index_path : log_path;
    } else {
        result.fields["slice_index_refs_json"] = "[]";
        result.fields["verified_slice_index_refs_json"] = "[]";
        result.fields["missing_slice_index_refs_json"] = "[]";
        result.fields["store_refs_verified"] = "false";
        result.fields["store_refs_verification_summary"] = "store_refs missing from upstream /rag/clips/run response";
        result.fields["result_ref"] = log_path;
        result.fields["evidence_ref"] = log_path;
    }

    if (GetFieldOrDefault(result, "store_refs_verified", "false") != "true") {
        result.fields["next_action"] = "inspect store_refs_verification_summary and verify upstream rag storage paths";
    }
    return result;
}

CommandResult BuildRagStorageLookupResult(
    const AgentConfig & config,
    const std::string & kind,
    const std::string & id,
    const std::string & slice_id,
    const std::string & trace_id,
    const std::string & query_id,
    const std::string & node_id,
    const std::string & edge_id,
    int limit,
    int timeout_ms) {
    CommandResult result;
    const std::string base_url = DeriveRagBridgeBaseUrl(config);
    if (base_url.empty()) {
        result.ok = false;
        result.exit_code = 98;
        result.fields["error"] = "generation_endpoint is not configured";
        result.fields["next_action"] = "configure generation_endpoint before querying rag storage";
        return result;
    }

    std::ostringstream body;
    body << "{"
         << "\"kind\":\"" << codex_lan_agent::JsonEscape(kind) << "\","
         << "\"limit\":" << std::max(1, limit);
    if (!id.empty()) {
        body << ",\"id\":\"" << codex_lan_agent::JsonEscape(id) << "\"";
    }
    if (!slice_id.empty()) {
        body << ",\"slice_id\":\"" << codex_lan_agent::JsonEscape(slice_id) << "\"";
    }
    if (!trace_id.empty()) {
        body << ",\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\"";
    }
    if (!query_id.empty()) {
        body << ",\"query_id\":\"" << codex_lan_agent::JsonEscape(query_id) << "\"";
    }
    if (!node_id.empty()) {
        body << ",\"node_id\":\"" << codex_lan_agent::JsonEscape(node_id) << "\"";
    }
    if (!edge_id.empty()) {
        body << ",\"edge_id\":\"" << codex_lan_agent::JsonEscape(edge_id) << "\"";
    }
    body << "}";

    const std::string endpoint = base_url + "/rag/storage/lookup";
    const codex_lan_agent::HttpResponse response =
        codex_lan_agent::PostJson(endpoint, body.str(), std::max(1000, timeout_ms));
    const std::string log_path = BuildLogPath(config, "rag_storage_lookup");
    std::ofstream output(log_path, std::ios::out | std::ios::trunc);
    output << "endpoint=" << endpoint << "\n";
    output << "request_body=\n" << body.str() << "\n";
    output << "status_code=" << response.status_code << "\n";
    output << "ok=" << (response.ok ? "true" : "false") << "\n";
    output << "error=" << response.error_message << "\n";
    output << "body=\n" << response.body << "\n";

    result.ok = response.ok;
    result.exit_code = response.ok ? 0 : 99;
    result.fields["upstream_path"] = "/rag/storage/lookup";
    result.fields["rag_bridge_base_url"] = base_url;
    result.fields["status_code"] = std::to_string(response.status_code);
    result.fields["kind"] = kind;
    result.fields["limit"] = std::to_string(std::max(1, limit));
    result.fields["log_path"] = log_path;
    result.fields["body"] = response.body;
    if (!id.empty()) {
        result.fields["id"] = id;
    }
    if (!slice_id.empty()) {
        result.fields["slice_id"] = slice_id;
    }
    if (!trace_id.empty()) {
        result.fields["trace_id"] = trace_id;
    }
    if (!query_id.empty()) {
        result.fields["query_id"] = query_id;
    }
    if (!node_id.empty()) {
        result.fields["node_id"] = node_id;
    }
    if (!edge_id.empty()) {
        result.fields["edge_id"] = edge_id;
    }
    if (!response.error_message.empty()) {
        result.fields["error"] = response.error_message;
    }
    if (!response.ok) {
        result.fields["semantic_outcome"] = "rag_storage_lookup_failed";
        result.fields["next_action"] = "inspect log_path and upstream /rag/storage/lookup availability";
        result.fields["result_ref"] = log_path;
        result.fields["evidence_ref"] = log_path;
        return result;
    }

    result.fields["semantic_outcome"] = "rag_storage_lookup_ready";
    result.fields["next_action"] = "inspect primary and related storage records";
    CopyJsonStringFieldIfPresent(&result, response.body, "record_model");
    CopyJsonStringFieldIfPresent(&result, response.body, "message");
    CopyJsonRawFieldIfPresent(&result, response.body, "ok");
    CopyJsonRawFieldIfPresent(&result, response.body, "found");
    CopyJsonRawFieldIfPresent(&result, response.body, "allowed_kinds");
    CopyJsonRawFieldIfPresent(&result, response.body, "primary");
    CopyJsonRawFieldIfPresent(&result, response.body, "related");
    CopyJsonStringFieldIfPresent(&result, response.body, "storage_base_path");
    CopyJsonStringFieldIfPresent(&result, response.body, "rocksdb_path");
    CopyJsonStringFieldIfPresent(&result, response.body, "kv_snapshot_path");
    result.fields["result_ref"] = FirstNonEmpty(
        GetFieldOrDefault(result, "kv_snapshot_path", ""),
        GetFieldOrDefault(result, "storage_base_path", ""),
        log_path);
    result.fields["evidence_ref"] = FirstNonEmpty(
        GetFieldOrDefault(result, "rocksdb_path", ""),
        GetFieldOrDefault(result, "kv_snapshot_path", ""),
        log_path);
    return result;
}

CommandResult BuildRagStoragePageResult(
    const AgentConfig & config,
    const std::string & kind,
    const std::string & trace_id,
    const std::string & query_id,
    const std::string & test_bucket,
    const std::string & coverage_gap,
    const std::string & result_stage,
    const std::string & coverage_status,
    const std::string & run_kind,
    const std::string & fact_type,
    int limit,
    int offset,
    int timeout_ms) {
    CommandResult result;
    const std::string base_url = DeriveRagBridgeBaseUrl(config);
    if (base_url.empty()) {
        result.ok = false;
        result.exit_code = 100;
        result.fields["error"] = "generation_endpoint is not configured";
        result.fields["next_action"] = "configure generation_endpoint before paging rag storage";
        return result;
    }

    std::ostringstream body;
    body << "{"
         << "\"kind\":\"" << codex_lan_agent::JsonEscape(kind) << "\","
         << "\"limit\":" << std::max(1, limit) << ","
         << "\"offset\":" << std::max(0, offset);
    if (!trace_id.empty()) {
        body << ",\"trace_id\":\"" << codex_lan_agent::JsonEscape(trace_id) << "\"";
    }
    if (!query_id.empty()) {
        body << ",\"query_id\":\"" << codex_lan_agent::JsonEscape(query_id) << "\"";
    }
    if (!test_bucket.empty()) {
        body << ",\"test_bucket\":\"" << codex_lan_agent::JsonEscape(test_bucket) << "\"";
    }
    if (!coverage_gap.empty()) {
        body << ",\"coverage_gap\":\"" << codex_lan_agent::JsonEscape(coverage_gap) << "\"";
    }
    if (!result_stage.empty()) {
        body << ",\"result_stage\":\"" << codex_lan_agent::JsonEscape(result_stage) << "\"";
    }
    if (!coverage_status.empty()) {
        body << ",\"coverage_status\":\"" << codex_lan_agent::JsonEscape(coverage_status) << "\"";
    }
    if (!run_kind.empty()) {
        body << ",\"run_kind\":\"" << codex_lan_agent::JsonEscape(run_kind) << "\"";
    }
    if (!fact_type.empty()) {
        body << ",\"fact_type\":\"" << codex_lan_agent::JsonEscape(fact_type) << "\"";
    }
    body << "}";

    const std::string endpoint = base_url + "/rag/storage/page";
    const codex_lan_agent::HttpResponse response =
        codex_lan_agent::PostJson(endpoint, body.str(), std::max(1000, timeout_ms));
    const std::string log_path = BuildLogPath(config, "rag_storage_page");
    std::ofstream output(log_path, std::ios::out | std::ios::trunc);
    output << "endpoint=" << endpoint << "\n";
    output << "request_body=\n" << body.str() << "\n";
    output << "status_code=" << response.status_code << "\n";
    output << "ok=" << (response.ok ? "true" : "false") << "\n";
    output << "error=" << response.error_message << "\n";
    output << "body=\n" << response.body << "\n";

    result.ok = response.ok;
    result.exit_code = response.ok ? 0 : 101;
    result.fields["upstream_path"] = "/rag/storage/page";
    result.fields["rag_bridge_base_url"] = base_url;
    result.fields["status_code"] = std::to_string(response.status_code);
    result.fields["kind"] = kind;
    result.fields["trace_id"] = trace_id;
    result.fields["query_id"] = query_id;
    result.fields["test_bucket"] = test_bucket;
    result.fields["coverage_gap"] = coverage_gap;
    result.fields["result_stage"] = result_stage;
    result.fields["coverage_status"] = coverage_status;
    result.fields["run_kind"] = run_kind;
    result.fields["fact_type"] = fact_type;
    result.fields["limit"] = std::to_string(std::max(1, limit));
    result.fields["offset"] = std::to_string(std::max(0, offset));
    result.fields["log_path"] = log_path;
    result.fields["body"] = response.body;
    if (!response.error_message.empty()) {
        result.fields["error"] = response.error_message;
    }
    if (!response.ok) {
        result.fields["semantic_outcome"] = "rag_storage_page_failed";
        result.fields["next_action"] = "inspect log_path and upstream /rag/storage/page availability";
        result.fields["result_ref"] = log_path;
        result.fields["evidence_ref"] = log_path;
        return result;
    }

    result.fields["semantic_outcome"] = "rag_storage_page_ready";
    result.fields["next_action"] = "consume paged storage records and continue with next_offset when has_more=true";
    CopyJsonStringFieldIfPresent(&result, response.body, "record_model");
    CopyJsonStringFieldIfPresent(&result, response.body, "message");
    CopyJsonStringFieldIfPresent(&result, response.body, "backend");
    CopyJsonRawFieldIfPresent(&result, response.body, "ok");
    CopyJsonRawFieldIfPresent(&result, response.body, "returned_count");
    CopyJsonRawFieldIfPresent(&result, response.body, "total_count");
    CopyJsonRawFieldIfPresent(&result, response.body, "has_more");
    CopyJsonRawFieldIfPresent(&result, response.body, "next_offset");
    CopyJsonRawFieldIfPresent(&result, response.body, "batch_fact_limit");
    CopyJsonRawFieldIfPresent(&result, response.body, "facts_rejected_by_limit");
    CopyJsonRawFieldIfPresent(&result, response.body, "trace_lookup");
    CopyJsonRawFieldIfPresent(&result, response.body, "records");
    CopyJsonStringFieldIfPresent(&result, response.body, "test_bucket");
    CopyJsonStringFieldIfPresent(&result, response.body, "coverage_gap");
    CopyJsonStringFieldIfPresent(&result, response.body, "result_stage");
    CopyJsonStringFieldIfPresent(&result, response.body, "coverage_status");
    CopyJsonStringFieldIfPresent(&result, response.body, "key_prefix");
    result.fields["result_ref"] = log_path;
    result.fields["evidence_ref"] = log_path;
    return result;
}

CommandResult BuildRagReviewObservationResult(
    const AgentConfig & config,
    const std::string & summary,
    const std::string & scenario,
    const std::string & train,
    const std::string & request_id,
    const std::string & trace_id,
    const std::string & query_id,
    const std::string & module,
    const std::string & task_layer,
    const std::string & task_case,
    const std::string & dataset_bridge,
    const std::string & test_bucket,
    const std::string & test_flow,
    const std::string & baseline_objective,
    const std::string & best_objective,
    const std::string & objective_delta,
    const std::string & comparison_status,
    const std::string & comparison_magnitude,
    const std::string & optimization_signal,
    const std::string & risk_axis,
    const std::string & bucket_coverage,
    const std::string & coverage_gap,
    const std::string & coverage_status,
    const std::string & next_review_action,
    const std::string & review_scope,
    const std::string & result_stage,
    const std::string & primary_review_ref,
    const std::string & summary_ref,
    const std::string & compare_ref,
    const std::string & replay_ref,
    const std::string & best_params_ref,
    bool human_review_required,
    const std::string & conclusion_id,
    const std::string & short_conclusion,
    const std::string & why_it_matters,
    const std::string & next_observation,
    const std::string & source_refs,
    const std::string & tags,
    int timeout_ms) {
    CommandResult result;
    const std::string base_url = DeriveRagBridgeBaseUrl(config);
    if (base_url.empty()) {
        result.ok = false;
        result.exit_code = 102;
        result.fields["error"] = "generation_endpoint is not configured";
        result.fields["next_action"] = "configure generation_endpoint before writing rag review observations";
        return result;
    }

    std::ostringstream body;
    body << "{";
    bool first = true;
    const auto append_string = [&](const char * key, const std::string & value) {
        if (value.empty()) {
            return;
        }
        if (!first) {
            body << ",";
        }
        first = false;
        body << "\"" << key << "\":\"" << codex_lan_agent::JsonEscape(value) << "\"";
    };
    const auto append_raw = [&](const char * key, const std::string & value) {
        if (value.empty()) {
            return;
        }
        if (!first) {
            body << ",";
        }
        first = false;
        body << "\"" << key << "\":" << value;
    };

    append_string("summary", summary);
    append_string("scenario", scenario);
    append_string("train", train);
    append_string("request_id", request_id);
    append_string("trace_id", trace_id);
    append_string("query_id", query_id);
    append_string("module", module);
    append_string("task_layer", task_layer);
    append_string("task_case", task_case);
    append_string("dataset_bridge", dataset_bridge);
    append_string("test_bucket", test_bucket);
    append_string("test_flow", test_flow);
    append_string("baseline_objective", baseline_objective);
    append_string("best_objective", best_objective);
    append_raw("objective_delta", objective_delta);
    append_string("comparison_status", comparison_status);
    append_string("comparison_magnitude", comparison_magnitude);
    append_string("optimization_signal", optimization_signal);
    append_string("risk_axis", risk_axis);
    append_string("bucket_coverage", bucket_coverage);
    append_string("coverage_gap", coverage_gap);
    append_string("coverage_status", coverage_status);
    append_string("next_review_action", next_review_action);
    append_string("review_scope", review_scope);
    append_string("result_stage", result_stage);
    append_string("primary_review_ref", primary_review_ref);
    append_string("summary_ref", summary_ref);
    append_string("compare_ref", compare_ref);
    append_string("replay_ref", replay_ref);
    append_string("best_params_ref", best_params_ref);
    append_raw("human_review_required", human_review_required ? "true" : "false");
    append_string("conclusion_id", conclusion_id);
    append_string("short_conclusion", short_conclusion);
    append_string("why_it_matters", why_it_matters);
    append_string("next_observation", next_observation);
    append_raw("source_refs", source_refs);
    append_raw("tags", tags);
    body << "}";

    const std::string endpoint = base_url + "/rag/review/observe";
    const codex_lan_agent::HttpResponse response =
        codex_lan_agent::PostJson(endpoint, body.str(), std::max(1000, timeout_ms));
    const std::string log_path = BuildLogPath(config, "rag_review_observe");
    std::ofstream output(log_path, std::ios::out | std::ios::trunc);
    output << "endpoint=" << endpoint << "\n";
    output << "request_body=\n" << body.str() << "\n";
    output << "status_code=" << response.status_code << "\n";
    output << "ok=" << (response.ok ? "true" : "false") << "\n";
    output << "error=" << response.error_message << "\n";
    output << "body=\n" << response.body << "\n";

    result.ok = response.ok;
    result.exit_code = response.ok ? 0 : 103;
    result.fields["upstream_path"] = "/rag/review/observe";
    result.fields["rag_bridge_base_url"] = base_url;
    result.fields["status_code"] = std::to_string(response.status_code);
    result.fields["log_path"] = log_path;
    result.fields["body"] = response.body;
    result.fields["module"] = module;
    result.fields["task_case"] = task_case;
    result.fields["test_bucket"] = test_bucket;
    result.fields["bucket_coverage"] = bucket_coverage;
    result.fields["coverage_gap"] = coverage_gap;
    result.fields["coverage_status"] = coverage_status;
    result.fields["review_scope"] = review_scope;
    result.fields["result_stage"] = result_stage;
    result.fields["best_params_ref"] = best_params_ref;
    result.fields["human_review_required"] = human_review_required ? "true" : "false";
    result.fields["conclusion_id"] = conclusion_id;
    if (!request_id.empty()) {
        result.fields["request_id"] = request_id;
    }
    if (!trace_id.empty()) {
        result.fields["trace_id"] = trace_id;
    }
    if (!query_id.empty()) {
        result.fields["query_id"] = query_id;
    }
    if (!response.error_message.empty()) {
        result.fields["error"] = response.error_message;
    }
    if (!response.ok) {
        result.fields["semantic_outcome"] = "rag_review_observe_failed";
        result.fields["next_action"] = "inspect log_path and upstream /rag/review/observe availability";
        result.fields["result_ref"] = log_path;
        result.fields["evidence_ref"] = log_path;
        return result;
    }

    result.fields["semantic_outcome"] = "rag_review_observe_ready";
    result.fields["next_action"] = "inspect observation_id or page review observations by bucket, gap, or trace";
    CopyJsonStringFieldIfPresent(&result, response.body, "record_model");
    CopyJsonRawFieldIfPresent(&result, response.body, "ok");
    CopyJsonStringFieldIfPresent(&result, response.body, "observation_id");
    CopyJsonRawFieldIfPresent(&result, response.body, "record");
    CopyJsonStringFieldIfPresent(&result, response.body, "review_store_path");
    CopyJsonStringFieldIfPresent(&result, response.body, "review_index_path");
    CopyJsonStringFieldIfPresent(&result, response.body, "review_trace_index_path");
    CopyJsonStringFieldIfPresent(&result, response.body, "review_bucket_index_path");
    CopyJsonStringFieldIfPresent(&result, response.body, "rocksdb_path");
    CopyJsonStringFieldIfPresent(&result, response.body, "kv_snapshot_path");
    result.fields["result_ref"] = FirstNonEmpty(
        GetFieldOrDefault(result, "review_index_path", ""),
        GetFieldOrDefault(result, "review_store_path", ""),
        log_path);
    result.fields["evidence_ref"] = FirstNonEmpty(
        GetFieldOrDefault(result, "review_bucket_index_path", ""),
        GetFieldOrDefault(result, "review_trace_index_path", ""),
        GetFieldOrDefault(result, "rocksdb_path", ""),
        log_path);
    return result;
}
