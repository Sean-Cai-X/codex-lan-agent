#include "TaskMemoryOperations.h"

#if defined(CODEX_LAN_AGENT_WITH_ROCKSDB) && CODEX_LAN_AGENT_WITH_ROCKSDB
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#endif

namespace codex_lan_agent {

CommandResult BuildTaskMemoryRocksDbMirrorResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string goal_id = params.GetString("goal_id");
    if (goal_id.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "goal_id is required";
        return result;
    }

    const std::filesystem::path root = BuildTaskMemoryRoot(config, goal_id);
    const std::filesystem::path index_path = root / "kv_snapshot" / "index.jsonl";
    const std::filesystem::path kv_manifest_path = root / "kv_snapshot" / "manifest.json";
    const std::filesystem::path rocksdb_path = TaskMemoryRocksDbPath(config, goal_id, params);
    const std::filesystem::path rocksdb_manifest_path = TaskMemoryRocksDbManifestPath(config, goal_id);
    const std::string index_text = ReadTaskMemoryTextFile(index_path);
    if (index_text.empty()) {
        result.ok = false;
        result.exit_code = 404;
        result.fields["error"] = "kv snapshot index not found";
        result.fields["kv_index_path"] = index_path.string();
        result.fields["next_action"] = "call lan_agent_task_memory_build_kv_snapshot first";
        return result;
    }

#if defined(CODEX_LAN_AGENT_WITH_ROCKSDB) && CODEX_LAN_AGENT_WITH_ROCKSDB
    std::error_code ec;
    std::filesystem::create_directories(rocksdb_path.parent_path(), ec);
    if (ec) {
        result.ok = false;
        result.exit_code = 501;
        result.fields["error"] = "failed to create RocksDB parent directory: " + ec.message();
        result.fields["rocksdb_path"] = rocksdb_path.string();
        return result;
    }

    rocksdb::Options options;
    options.create_if_missing = true;
    std::unique_ptr<rocksdb::DB> db;
    const rocksdb::Status open_status = rocksdb::DB::Open(options, rocksdb_path.string(), &db);
    if (!open_status.ok() || db == nullptr) {
        result.ok = false;
        result.exit_code = 502;
        result.fields["error"] = "failed to open RocksDB: " + open_status.ToString();
        result.fields["rocksdb_path"] = rocksdb_path.string();
        return result;
    }

    std::istringstream lines(index_text);
    std::string line;
    int mirrored_count = 0;
    std::string first_error;
    while (std::getline(lines, line)) {
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        const std::string key = ExtractJsonString(line, "key");
        if (key.empty()) {
            continue;
        }
        const rocksdb::Status put_status = db->Put(rocksdb::WriteOptions(), key, line);
        if (!put_status.ok()) {
            first_error = put_status.ToString();
            break;
        }
        ++mirrored_count;
    }

    const bool mirror_complete = first_error.empty();
    const std::string kv_manifest = ReadTaskMemoryTextFile(kv_manifest_path);
    const int kv_record_count = TaskMemoryExtractIntField(kv_manifest, "record_count", 0);
    std::ostringstream manifest;
    manifest
        << "{\n"
        << "  \"record_model\":\"mcp_task_memory_rocksdb_mirror_manifest_v1\",\n"
        << "  \"goal_id\":\"" << JsonEscape(goal_id) << "\",\n"
        << "  \"created_at\":\"" << JsonEscape(IsoTimestampNow()) << "\",\n"
        << "  \"source_of_truth\":\"file_object_store\",\n"
        << "  \"native_backend_role\":\"mirror_read_backend\",\n"
        << "  \"rocksdb_path\":\"" << JsonEscape(rocksdb_path.string()) << "\",\n"
        << "  \"kv_index_path\":\"" << JsonEscape(index_path.string()) << "\",\n"
        << "  \"kv_record_count\":" << kv_record_count << ",\n"
        << "  \"mirrored_count\":" << mirrored_count << ",\n"
        << "  \"mirror_complete\":" << (mirror_complete ? "true" : "false") << ",\n"
        << "  \"safe_to_replace_source_of_truth\":false\n"
        << "}\n";

    std::string write_error;
    if (!WriteTaskMemoryTextFile(rocksdb_manifest_path, manifest.str(), &write_error)) {
        result.ok = false;
        result.exit_code = 503;
        result.fields["error"] = write_error;
        result.fields["failed_path"] = rocksdb_manifest_path.string();
        return result;
    }
    const rocksdb::Status meta_status = db->Put(
        rocksdb::WriteOptions(),
        "meta/" + goal_id + "/manifest",
        manifest.str());

    result.ok = mirror_complete && meta_status.ok();
    result.exit_code = result.ok ? 0 : 504;
    result.fields["record_model"] = "mcp_task_memory_rocksdb_mirror_response_v1";
    result.fields["goal_id"] = goal_id;
    result.fields["task_memory_root"] = root.string();
    result.fields["source_of_truth"] = "file_object_store";
    result.fields["native_backend_role"] = "mirror_read_backend";
    result.fields["rocksdb_status"] = result.ok ? "enabled" : "mirror_incomplete";
    result.fields["rocksdb_path"] = rocksdb_path.string();
    result.fields["rocksdb_manifest_path"] = rocksdb_manifest_path.string();
    result.fields["kv_index_path"] = index_path.string();
    result.fields["kv_manifest_path"] = kv_manifest_path.string();
    result.fields["kv_record_count"] = std::to_string(kv_record_count);
    result.fields["mirrored_count"] = std::to_string(mirrored_count);
    result.fields["mirror_complete"] = mirror_complete ? "true" : "false";
    result.fields["safe_to_replace_source_of_truth"] = "false";
    result.fields["semantic_outcome"] = result.ok
        ? "task_memory_rocksdb_mirror_ready"
        : "task_memory_rocksdb_mirror_incomplete";
    result.fields["next_action"] = result.ok
        ? "call lan_agent_task_memory_rocksdb_lookup or lan_agent_task_memory_rocksdb_parity_check"
        : "inspect RocksDB mirror error before using native lookup";
    result.fields["error"] = result.ok ? "" : (first_error.empty() ? meta_status.ToString() : first_error);
    result.fields["result_ref"] = rocksdb_manifest_path.string();
    result.fields["evidence_ref"] = rocksdb_manifest_path.string();
    return result;
#else
    result.ok = false;
    result.exit_code = 503;
    result.fields["record_model"] = "mcp_task_memory_rocksdb_mirror_response_v1";
    result.fields["goal_id"] = goal_id;
    result.fields["source_of_truth"] = "file_object_store";
    result.fields["native_backend_role"] = "mirror_read_backend";
    result.fields["rocksdb_status"] = "not_compiled";
    result.fields["rocksdb_path"] = rocksdb_path.string();
    result.fields["kv_index_path"] = index_path.string();
    result.fields["compile_required"] = "true";
    result.fields["safe_to_replace_source_of_truth"] = "false";
    result.fields["error"] = "codex_lan_agent was built without CODEX_LAN_AGENT_WITH_ROCKSDB";
    result.fields["next_action"] = "reconfigure with CODEX_LAN_AGENT_WITH_ROCKSDB=ON and rebuild codex_lan_agent";
    return result;
#endif
}

CommandResult BuildTaskMemoryRocksDbLookupResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    CommandResult result;
    const std::string goal_id = params.GetString("goal_id");
    if (goal_id.empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "goal_id is required";
        return result;
    }

    bool prefix_match = false;
    const std::string lookup_key = TaskMemoryBuildLookupKey(goal_id, params, &prefix_match);
    if (Trim(lookup_key).empty()) {
        result.ok = false;
        result.exit_code = 400;
        result.fields["error"] = "key or lookup selector is required";
        result.fields["next_action"] = "provide key, kind=latest, kind=goal, kind=trace with trace_id, kind=slice with slice_id, or kind=budget with budget_run_id";
        return result;
    }

    const std::filesystem::path root = BuildTaskMemoryRoot(config, goal_id);
    const std::filesystem::path rocksdb_path = TaskMemoryRocksDbPath(config, goal_id, params);

#if defined(CODEX_LAN_AGENT_WITH_ROCKSDB) && CODEX_LAN_AGENT_WITH_ROCKSDB
    rocksdb::Options options;
    options.create_if_missing = false;
    std::unique_ptr<rocksdb::DB> db;
    const rocksdb::Status open_status = rocksdb::DB::Open(options, rocksdb_path.string(), &db);
    if (!open_status.ok() || db == nullptr) {
        result.ok = false;
        result.exit_code = 404;
        result.fields["error"] = "failed to open RocksDB mirror: " + open_status.ToString();
        result.fields["rocksdb_path"] = rocksdb_path.string();
        result.fields["next_action"] = "call lan_agent_task_memory_rocksdb_mirror first";
        return result;
    }

    const int limit = std::max(1, params.GetInt("limit", 16));
    const int offset = std::max(0, params.GetInt("offset", 0));
    const bool include_value = params.GetBool("include_value", false);
    std::ostringstream matches;
    int total_matches = 0;
    int emitted = 0;
    std::string first_value;
    std::string first_value_ref;

    if (prefix_match) {
        std::unique_ptr<rocksdb::Iterator> it(db->NewIterator(rocksdb::ReadOptions()));
        for (it->Seek(lookup_key); it->Valid(); it->Next()) {
            const std::string key = it->key().ToString();
            if (key.rfind(lookup_key, 0) != 0) {
                break;
            }
            const std::string value = it->value().ToString();
            if (total_matches >= offset && emitted < limit) {
                matches << value << "\n";
                ++emitted;
                if (include_value && first_value.empty()) {
                    first_value_ref = ExtractJsonString(value, "value_ref");
                    first_value = ReadTaskMemoryTextFile(std::filesystem::path(first_value_ref));
                }
            }
            ++total_matches;
        }
        if (!it->status().ok()) {
            result.ok = false;
            result.exit_code = 505;
            result.fields["error"] = "RocksDB iterator failed: " + it->status().ToString();
            result.fields["rocksdb_path"] = rocksdb_path.string();
            return result;
        }
    } else {
        std::string value;
        const rocksdb::Status get_status = db->Get(rocksdb::ReadOptions(), lookup_key, &value);
        if (get_status.ok()) {
            total_matches = 1;
            if (offset == 0 && limit > 0) {
                matches << value << "\n";
                emitted = 1;
                if (include_value) {
                    first_value_ref = ExtractJsonString(value, "value_ref");
                    first_value = ReadTaskMemoryTextFile(std::filesystem::path(first_value_ref));
                }
            }
        } else if (!get_status.IsNotFound()) {
            result.ok = false;
            result.exit_code = 506;
            result.fields["error"] = "RocksDB get failed: " + get_status.ToString();
            result.fields["rocksdb_path"] = rocksdb_path.string();
            return result;
        }
    }

    result.fields["record_model"] = "mcp_task_memory_rocksdb_lookup_response_v1";
    result.fields["goal_id"] = goal_id;
    result.fields["task_memory_root"] = root.string();
    result.fields["lookup_key"] = lookup_key;
    result.fields["prefix_match"] = prefix_match ? "true" : "false";
    result.fields["kv_backend"] = "rocksdb_native_mirror";
    result.fields["source_of_truth"] = "file_object_store";
    result.fields["native_backend_role"] = "mirror_read_backend";
    result.fields["rocksdb_status"] = "enabled";
    result.fields["rocksdb_path"] = rocksdb_path.string();
    result.fields["limit"] = std::to_string(limit);
    result.fields["offset"] = std::to_string(offset);
    result.fields["matched_count"] = std::to_string(total_matches);
    result.fields["returned_count"] = std::to_string(emitted);
    result.fields["has_more"] = (offset + emitted < total_matches) ? "true" : "false";
    result.fields["next_offset"] = std::to_string(offset + emitted);
    result.fields["matches_jsonl"] = matches.str();
    result.fields["include_value"] = include_value ? "true" : "false";
    result.fields["value_ref"] = first_value_ref;
    result.fields["value_text"] = first_value;
    result.fields["semantic_outcome"] = total_matches > 0
        ? "task_memory_rocksdb_lookup_hit"
        : "task_memory_rocksdb_lookup_miss";
    result.fields["next_action"] = (offset + emitted < total_matches)
        ? "repeat lan_agent_task_memory_rocksdb_lookup with next_offset"
        : "run lan_agent_task_memory_rocksdb_parity_check for validation when needed";
    result.fields["result_ref"] = TaskMemoryRocksDbManifestPath(config, goal_id).string();
    result.fields["evidence_ref"] = rocksdb_path.string();
    return result;
#else
    result.ok = false;
    result.exit_code = 503;
    result.fields["record_model"] = "mcp_task_memory_rocksdb_lookup_response_v1";
    result.fields["goal_id"] = goal_id;
    result.fields["lookup_key"] = lookup_key;
    result.fields["source_of_truth"] = "file_object_store";
    result.fields["native_backend_role"] = "mirror_read_backend";
    result.fields["rocksdb_status"] = "not_compiled";
    result.fields["rocksdb_path"] = rocksdb_path.string();
    result.fields["compile_required"] = "true";
    result.fields["error"] = "codex_lan_agent was built without CODEX_LAN_AGENT_WITH_ROCKSDB";
    result.fields["next_action"] = "reconfigure with CODEX_LAN_AGENT_WITH_ROCKSDB=ON and rebuild codex_lan_agent";
    return result;
#endif
}

CommandResult BuildTaskMemoryRocksDbParityCheckResult(
    const AgentConfig & config,
    const JsonRequestView & params) {
    CommandResult result;
    const CommandResult file_lookup = BuildTaskMemoryKvLookupResult(config, params);
    const CommandResult rocks_lookup = BuildTaskMemoryRocksDbLookupResult(config, params);

    const std::string goal_id = params.GetString("goal_id");
    const std::string file_matches = file_lookup.fields.count("matches_jsonl") == 0
        ? std::string()
        : file_lookup.fields.at("matches_jsonl");
    const std::string rocks_matches = rocks_lookup.fields.count("matches_jsonl") == 0
        ? std::string()
        : rocks_lookup.fields.at("matches_jsonl");
    const std::string file_count = file_lookup.fields.count("matched_count") == 0
        ? std::string()
        : file_lookup.fields.at("matched_count");
    const std::string rocks_count = rocks_lookup.fields.count("matched_count") == 0
        ? std::string()
        : rocks_lookup.fields.at("matched_count");
    const std::string lookup_key = file_lookup.fields.count("lookup_key") != 0
        ? file_lookup.fields.at("lookup_key")
        : (rocks_lookup.fields.count("lookup_key") != 0 ? rocks_lookup.fields.at("lookup_key") : std::string());
    const bool parity_ok =
        file_lookup.ok &&
        rocks_lookup.ok &&
        file_count == rocks_count &&
        file_matches == rocks_matches;

    result.ok = parity_ok;
    result.exit_code = parity_ok ? 0 : 507;
    result.fields["record_model"] = "mcp_task_memory_rocksdb_parity_check_response_v1";
    result.fields["goal_id"] = goal_id;
    result.fields["source_of_truth"] = "file_object_store";
    result.fields["native_backend_role"] = "mirror_read_backend";
    result.fields["lookup_key"] = lookup_key;
    result.fields["file_lookup_ok"] = file_lookup.ok ? "true" : "false";
    result.fields["rocksdb_lookup_ok"] = rocks_lookup.ok ? "true" : "false";
    result.fields["file_matched_count"] = file_count;
    result.fields["rocksdb_matched_count"] = rocks_count;
    result.fields["file_matches_hash"] = TaskMemoryStableChecksum(file_matches);
    result.fields["rocksdb_matches_hash"] = TaskMemoryStableChecksum(rocks_matches);
    result.fields["parity_ok"] = parity_ok ? "true" : "false";
    result.fields["safe_to_replace_source_of_truth"] = "false";
    result.fields["rocksdb_status"] = rocks_lookup.fields.count("rocksdb_status") == 0
        ? "unknown"
        : rocks_lookup.fields.at("rocksdb_status");
    result.fields["semantic_outcome"] = parity_ok
        ? "task_memory_rocksdb_parity_pass"
        : "task_memory_rocksdb_parity_fail";
    result.fields["next_action"] = parity_ok
        ? "RocksDB mirror can be used as native read backend for this key; file_object_store remains source of truth"
        : "rebuild kv snapshot, rerun rocksdb mirror, then repeat parity check";
    result.fields["result_ref"] = TaskMemoryRocksDbManifestPath(config, goal_id).string();
    result.fields["evidence_ref"] = BuildTaskMemoryRoot(config, goal_id).string();
    result.fields["file_error"] = file_lookup.fields.count("error") == 0 ? "" : file_lookup.fields.at("error");
    result.fields["rocksdb_error"] = rocks_lookup.fields.count("error") == 0 ? "" : rocks_lookup.fields.at("error");
    return result;
}

}  // namespace codex_lan_agent
