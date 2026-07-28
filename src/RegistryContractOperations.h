#pragma once

CommandResult BuildMcpCapabilityRegistryResult(const std::string & capability_filter) {
    CommandResult result;
    result.fields["registry_version"] = "mcp_capability_registry_v1";
    result.fields["routing_mode"] = "heterogeneous_multi_provider";
    result.fields["primary_rule"] = "do_not_merge_all_capabilities_into_one_model_service";
    result.fields["schema"] = "capability_id,provider_id,provider,input_contract,output_contract,storage,return_format,role,notes";
    int index = 0;
    for (const McpCapabilitySpec & spec : GetMcpCapabilitySpecs()) {
        if (!capability_filter.empty() && capability_filter != spec.capability_id) {
            continue;
        }
        const std::string prefix = capability_filter.empty()
            ? ("capability_" + std::to_string(index) + "_")
            : "";
        result.fields[prefix + "capability_id"] = spec.capability_id;
        result.fields[prefix + "provider_id"] = spec.provider_id;
        result.fields[prefix + "provider"] = spec.provider;
        result.fields[prefix + "input_contract"] = spec.input_contract;
        result.fields[prefix + "output_contract"] = spec.output_contract;
        result.fields[prefix + "storage"] = spec.storage;
        result.fields[prefix + "return_format"] = spec.return_format;
        result.fields[prefix + "role"] = spec.role;
        result.fields[prefix + "notes"] = spec.notes;
        ++index;
    }
    result.fields["capability_count"] = std::to_string(index);
    if (!capability_filter.empty() && index == 0) {
        result.ok = false;
        result.exit_code = 54;
        result.fields["error"] = "unknown capability_id";
        result.fields["capability_id"] = capability_filter;
    }
    return result;
}

CommandResult BuildRagMemorySliceContractResult(const std::string & field_group) {
    CommandResult result;
    result.fields["contract_version"] = "rag_memory_slice_v1";
    result.fields["ingest_style"] = "low_coupling_glue_first";
    result.fields["primary_acceptance"] = "new_error_recalls_similar_verified_error_and_solution";
    result.fields["schema"] = "group,field,meaning,required";
    int index = 0;
    const auto append_field =
        [&result, &index, &field_group](
            const std::string & group_name,
            const std::string & field_name,
            const std::string & meaning,
            const std::string & required_flag) {
            if (!field_group.empty() && field_group != group_name) {
                return;
            }
            const std::string prefix = "field_" + std::to_string(index++) + "_";
            result.fields[prefix + "group"] = group_name;
            result.fields[prefix + "field"] = field_name;
            result.fields[prefix + "meaning"] = meaning;
            result.fields[prefix + "required"] = required_flag;
        };

    append_field("identity", "slice_id", "unique slice identifier", "true");
    append_field("identity", "slice_version", "contract version, currently rag_memory_slice_v1", "true");
    append_field("identity", "slice_type", "remote_session|ventriloquist|manual_webui|codex_arrangement|error_case|solution_case|retrieval_hit|retrieval_writeback", "true");
    append_field("identity", "created_at", "slice creation time", "true");
    append_field("identity", "provider_id", "stable provider identifier such as llama_cpp_b8851_remote_session", "true");
    append_field("identity", "capability_id", "stable capability identifier from mcp_capability_registry_v1", "true");
    append_field("identity", "source_provider", "llama.cpp-b8851|llama.cpp-b8212|codex-lan-agent|manual_webui", "true");
    append_field("binding", "task_id", "task binding for audit and recall grouping", "false");
    append_field("binding", "session_id", "remote-session binding when conversation-visible", "false");
    append_field("binding", "turn_id", "remote-session turn binding when conversation-visible", "false");
    append_field("binding", "task_group_id", "higher-level grouping key across slices", "false");
    append_field("binding", "strategy_key", "reusable strategy or solution key", "false");
    append_field("content", "user_text", "business-side source text", "false");
    append_field("content", "assistant_text", "model or manual reply text", "false");
    append_field("content", "slice_summary", "compact summary for retrieval", "true");
    append_field("content", "error_signature", "normalized error signature for Faiss recall", "false");
    append_field("content", "solution_summary", "normalized verified solution summary", "false");
    append_field("content", "expression_keys", "compact lexical keys for routing and lookup", "false");
    append_field("control", "reasoning_level", "low|medium|high", "false");
    append_field("control", "primary_intent", "main semantic intent", "false");
    append_field("control", "secondary_intents", "supporting intents", "false");
    append_field("control", "confidence", "confirmed|likely|unclear|blocked", "false");
    append_field("audit", "result_ref", "result pointer such as log or remote turn ref", "false");
    append_field("audit", "evidence_ref", "supporting evidence pointer", "false");
    append_field("audit", "audit_ref", "primary audit anchor, recommended session:<session_id>/turn:<turn_id>", "true");
    append_field("audit", "slice_refs", "related slice identifiers used for recall or writeback", "false");
    append_field("audit", "storage_refs", "provider/storage-specific references for Faiss/sqlite-vss/RocksDB/Milvus", "false");
    append_field("audit", "source_type", "codex|manual|mixed", "true");
    append_field("audit", "write_mode", "new|append|append_turn|new_turn", "false");
    append_field("storage", "vector_payload", "text chunk to embed for Faiss/sqlite-vss", "true");
    append_field("storage", "vector_ready", "true when vector_payload is suitable for embedding", "false");
    append_field("storage", "vector_skip_reason", "why vectorization was skipped or blocked", "false");
    append_field("storage", "metadata_json", "task/session/strategy metadata for RocksDB later", "false");
    append_field("storage", "dedup_hash", "content hash for dedup", "false");
    append_field("storage", "canonical_slice_id", "canonical slice id used for dedup grouping", "false");
    append_field("storage", "dedup_status", "canonical|duplicate|read_only|pending", "false");
    append_field("storage", "dedup_reason", "dedup explanation for operators and consumers", "false");
    append_field("storage", "dup_of", "canonical slice id when this slice is a duplicate", "false");

    result.fields["field_count"] = std::to_string(index);
    if (!field_group.empty() && index == 0) {
        result.ok = false;
        result.exit_code = 55;
        result.fields["error"] = "unknown rag_memory_slice_v1 field_group";
        result.fields["field_group"] = field_group;
    }
    return result;
}
