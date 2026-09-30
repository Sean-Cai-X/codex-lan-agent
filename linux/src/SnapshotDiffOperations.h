#pragma once

bool IsSafeLightProfile(const std::string & profile) {
    return profile == "check_build_dir" ||
           profile == "run_script" ||
           profile == "run_local_chat";
}

std::string FindGitRootUnderWorkspace(const AgentConfig & config) {
    std::filesystem::path workspace(config.workspace_root);
    std::error_code ec;
    if (std::filesystem::exists(workspace / ".git", ec)) {
        return workspace.string();
    }
    for (const auto & entry : std::filesystem::directory_iterator(workspace, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_directory()) {
            continue;
        }
        const std::filesystem::path candidate = entry.path();
        if (std::filesystem::exists(candidate / ".git", ec) && !ec) {
            return candidate.string();
        }
        ec.clear();
    }
    return config.workspace_root;
}

std::string QuoteSnapshotDiffArgument(const std::string & value) {
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

std::string BuildSnapshotDiffArtifactBase(
    const AgentConfig & config,
    const std::string & working_directory) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const long long millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return codex_lan_agent::JoinPath(
        config.log_root,
        "snapshot_diff_"
            + SanitizeDispatchToken(working_directory, "repo")
            + "_" + std::to_string(millis));
}

std::vector<std::filesystem::path> BuildGitSnapshotHelperCandidates(const AgentConfig & config) {
#ifdef _WIN32
    const std::string executable_name = "git_snapshot.exe";
#else
    const std::string executable_name = "git_snapshot";
#endif
    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path config_dir(config.config_dir);
    const std::filesystem::path workspace_root(config.workspace_root);
    candidates.push_back(config_dir / executable_name);
    candidates.push_back(config_dir / "Release" / executable_name);
    candidates.push_back(config_dir / "Debug" / executable_name);
    candidates.push_back(config_dir / "RelWithDebInfo" / executable_name);
    candidates.push_back(config_dir / "build" / executable_name);
    candidates.push_back(config_dir / "build" / "Release" / executable_name);
    candidates.push_back(config_dir / "build" / "Debug" / executable_name);
    candidates.push_back(config_dir / "build" / "RelWithDebInfo" / executable_name);
    candidates.push_back(workspace_root / "codex-lan-agent" / executable_name);
    candidates.push_back(workspace_root / "codex-lan-agent" / "Release" / executable_name);
    candidates.push_back(workspace_root / "codex-lan-agent" / "Debug" / executable_name);
    candidates.push_back(workspace_root / "codex-lan-agent" / "RelWithDebInfo" / executable_name);
    candidates.push_back(workspace_root / "codex-lan-agent" / "build" / executable_name);
    candidates.push_back(workspace_root / "codex-lan-agent" / "build" / "Release" / executable_name);
    candidates.push_back(workspace_root / "codex-lan-agent" / "build" / "Debug" / executable_name);
    candidates.push_back(workspace_root / "codex-lan-agent" / "build" / "RelWithDebInfo" / executable_name);
    return candidates;
}

std::string FindGitSnapshotHelperPath(
    const AgentConfig & config,
    std::string * searched_paths_json) {
    const std::vector<std::filesystem::path> candidates = BuildGitSnapshotHelperCandidates(config);
    std::ostringstream searched;
    searched << "[";
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (index > 0) {
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

std::string SnapshotDiffGitProgram() {
#ifdef _WIN32
    return "git.exe";
#else
    return "git";
#endif
}

bool AppendSnapshotDiffLogLine(
    const std::string & log_path,
    const std::string & line,
    std::string * error_message) {
    std::ofstream output(log_path, std::ios::out | std::ios::app);
    if (!output.is_open()) {
        if (error_message != nullptr) {
            *error_message = "failed to open snapshot diff log";
        }
        return false;
    }
    output << line << "\n";
    return output.good();
}

std::string BuildNonGitSnapshotRepoPath(
    const AgentConfig & config,
    const std::string & source_directory) {
    const std::string hash = StableContentChecksum(source_directory).substr(0, 16);
    return (std::filesystem::path(config.log_root)
        / "snapshot_repos"
        / ("snap_" + hash)).string();
}

bool IsSnapshotDiffExcludedDirectoryName(const std::string & name) {
    return name == ".git" ||
           name == ".vs" ||
           name == ".vscode" ||
           name == "build" ||
           name == "out" ||
           name == "logs" ||
           name == "node_modules" ||
           name == "__pycache__" ||
           name == ".cache" ||
           name == "snapshot_repos";
}

bool PathContainsSnapshotDiffExcludedDirectory(const std::filesystem::path & relative_path) {
    for (const auto & part : relative_path) {
        if (IsSnapshotDiffExcludedDirectoryName(part.string())) {
            return true;
        }
    }
    return false;
}

bool ClearNonGitSnapshotMirrorWorkingTree(
    const std::filesystem::path & mirror_root,
    std::string * error_message) {
    std::error_code ec;
    std::filesystem::create_directories(mirror_root, ec);
    if (ec) {
        if (error_message != nullptr) {
            *error_message = "failed to create snapshot mirror directory: " + ec.message();
        }
        return false;
    }
    for (const auto & entry : std::filesystem::directory_iterator(mirror_root, ec)) {
        if (ec) {
            if (error_message != nullptr) {
                *error_message = "failed to list snapshot mirror directory: " + ec.message();
            }
            return false;
        }
        if (entry.path().filename().string() == ".git") {
            continue;
        }
        std::filesystem::remove_all(entry.path(), ec);
        if (ec) {
            if (error_message != nullptr) {
                *error_message = "failed to clear snapshot mirror content: " + ec.message();
            }
            return false;
        }
    }
    return true;
}

bool CopyDirectoryToNonGitSnapshotMirror(
    const std::filesystem::path & source_root,
    const std::filesystem::path & mirror_root,
    std::uintmax_t * copied_file_count,
    std::uintmax_t * copied_byte_count,
    std::uintmax_t * skipped_file_count,
    std::string * error_message) {
    std::error_code ec;
    std::filesystem::create_directories(mirror_root, ec);
    if (ec) {
        if (error_message != nullptr) {
            *error_message = "failed to create snapshot mirror directory: " + ec.message();
        }
        return false;
    }
    std::uintmax_t files = 0;
    std::uintmax_t bytes = 0;
    std::uintmax_t skipped = 0;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (auto it = std::filesystem::recursive_directory_iterator(source_root, options, ec);
         it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        const std::filesystem::path relative_path = std::filesystem::relative(it->path(), source_root, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        if (PathContainsSnapshotDiffExcludedDirectory(relative_path)) {
            if (it->is_directory(ec)) {
                it.disable_recursion_pending();
            }
            ec.clear();
            continue;
        }
        const std::filesystem::path target_path = mirror_root / relative_path;
        if (it->is_directory(ec)) {
            std::filesystem::create_directories(target_path, ec);
            if (ec) {
                ++skipped;
                it.disable_recursion_pending();
                ec.clear();
            }
            continue;
        }
        if (!it->is_regular_file(ec)) {
            ec.clear();
            continue;
        }
        std::filesystem::create_directories(target_path.parent_path(), ec);
        if (ec) {
            ++skipped;
            ec.clear();
            continue;
        }
        std::filesystem::copy_file(
            it->path(),
            target_path,
            std::filesystem::copy_options::overwrite_existing,
            ec);
        if (ec) {
            ++skipped;
            ec.clear();
            continue;
        }
        ++files;
        bytes += it->file_size(ec);
        ec.clear();
    }
    if (copied_file_count != nullptr) {
        *copied_file_count = files;
    }
    if (copied_byte_count != nullptr) {
        *copied_byte_count = bytes;
    }
    if (skipped_file_count != nullptr) {
        *skipped_file_count = skipped;
    }
    return true;
}

bool RunSnapshotMirrorGitStep(
    const std::string & label,
    const std::string & mirror_root,
    const std::string & git_arguments,
    const std::string & log_path,
    int timeout_sec,
    codex_lan_agent::ProcessRunResult * run_result,
    std::string * command_line,
    std::string * error_message) {
    AppendSnapshotDiffLogLine(log_path, "[mirror_git_step]", nullptr);
    AppendSnapshotDiffLogLine(log_path, "label=" + label, nullptr);
    const std::string line =
        SnapshotDiffGitProgram()
        + " -C " + QuoteSnapshotDiffArgument(mirror_root)
        + " " + git_arguments;
    if (command_line != nullptr) {
        *command_line = line;
    }
    if (!codex_lan_agent::RunCommandWithLog(
            line,
            std::string(),
            log_path,
            std::max(timeout_sec, 300),
            120,
            run_result,
            error_message)) {
        return false;
    }
    AppendSnapshotDiffLogLine(
        log_path,
        "step_exit_code=" + std::to_string(run_result != nullptr ? run_result->exit_code : -1),
        nullptr);
    AppendSnapshotDiffLogLine(log_path, "", nullptr);
    return true;
}

bool ClearNonGitSnapshotStaleIndexLock(
    const std::string & mirror_root,
    bool * removed,
    std::string * error_message) {
    if (removed != nullptr) {
        *removed = false;
    }
    const std::filesystem::path lock_path = std::filesystem::path(mirror_root) / ".git" / "index.lock";
    std::error_code ec;
    if (!std::filesystem::exists(lock_path, ec) || ec) {
        return true;
    }
    std::filesystem::remove(lock_path, ec);
    if (ec) {
        if (error_message != nullptr) {
            *error_message = "failed to remove stale snapshot index.lock: " + ec.message();
        }
        return false;
    }
    if (removed != nullptr) {
        *removed = true;
    }
    return true;
}

bool IsSnapshotBaselineRefreshAction(const std::string & snapshot_action) {
    const std::string action = ToLowerAscii(snapshot_action);
    return action == "refresh_baseline" ||
           action == "accept_baseline" ||
           action == "commit_baseline";
}

CommandResult RunSnapshotDiffNonGitMirrorFallback(
    const AgentConfig & config,
    const std::string & requested_repo_root,
    const std::string & source_directory,
    const std::string & helper_search_paths,
    const std::string & log_path,
    int timeout_sec,
    const std::string & snapshot_action) {
    CommandResult result;
    const std::string mirror_root = BuildNonGitSnapshotRepoPath(config, source_directory);
    result.fields["requested_repo_root"] = requested_repo_root;
    result.fields["repo_root"] = source_directory;
    result.fields["working_directory"] = source_directory;
    result.fields["source_directory"] = source_directory;
    result.fields["snapshot_repo_path"] = mirror_root;
    result.fields["snapshot_repository_mode"] = "non_git_mirror_repo";
    result.fields["non_git_snapshot"] = "true";
    result.fields["helper_search_paths_json"] = helper_search_paths;
    result.fields["helper_missing_fallback"] = "true";
    result.fields["helper_path"] = "";
    result.fields["snapshot_action"] = snapshot_action.empty() ? "diff" : snapshot_action;
    result.fields["log_path"] = log_path;
    result.fields["excluded_directories"] =
        ".git,.vs,.vscode,build,out,logs,node_modules,__pycache__,.cache,snapshot_repos";

    std::string log_error;
    AppendSnapshotDiffLogLine(log_path, "[snapshot_start]", &log_error);
    AppendSnapshotDiffLogLine(log_path, "source_directory=" + source_directory, &log_error);
    AppendSnapshotDiffLogLine(log_path, "snapshot_repo_path=" + mirror_root, &log_error);
    AppendSnapshotDiffLogLine(log_path, "execution_mode=non_git_mirror_repo", &log_error);
    AppendSnapshotDiffLogLine(log_path, "", &log_error);
    if (!log_error.empty()) {
        result.ok = false;
        result.exit_code = 52;
        result.fields["error"] = log_error;
        result.fields["semantic_outcome"] = "snapshot_diff_log_write_failed";
        return result;
    }

    std::error_code ec;
    const bool repo_already_initialized = std::filesystem::exists(
        std::filesystem::path(mirror_root) / ".git",
        ec);
    codex_lan_agent::ProcessRunResult run_result;
    std::string command_line;
    std::string run_error;

    if (!repo_already_initialized) {
        std::filesystem::create_directories(mirror_root, ec);
        if (ec) {
            result.ok = false;
            result.exit_code = 52;
            result.fields["error"] = "failed to create snapshot repo: " + ec.message();
            result.fields["semantic_outcome"] = "non_git_snapshot_repo_create_failed";
            return result;
        }
        if (!RunSnapshotMirrorGitStep(
                "init",
                mirror_root,
                "init",
                log_path,
                timeout_sec,
                &run_result,
                &command_line,
                &run_error) || run_result.exit_code != 0) {
            result.ok = false;
            result.exit_code = run_result.exit_code == 0 ? 52 : run_result.exit_code;
            result.fields["error"] = run_error;
            result.fields["command_line"] = command_line;
            result.fields["semantic_outcome"] = "non_git_snapshot_repo_init_failed";
            return result;
        }
    }

    std::string copy_error;
    if (!ClearNonGitSnapshotMirrorWorkingTree(std::filesystem::path(mirror_root), &copy_error)) {
        result.ok = false;
        result.exit_code = 52;
        result.fields["error"] = copy_error;
        result.fields["semantic_outcome"] = "non_git_snapshot_sync_failed";
        return result;
    }
    std::uintmax_t copied_files = 0;
    std::uintmax_t copied_bytes = 0;
    std::uintmax_t skipped_files = 0;
    if (!CopyDirectoryToNonGitSnapshotMirror(
            std::filesystem::path(source_directory),
            std::filesystem::path(mirror_root),
            &copied_files,
            &copied_bytes,
            &skipped_files,
            &copy_error)) {
        result.ok = false;
        result.exit_code = 52;
        result.fields["error"] = copy_error;
        result.fields["semantic_outcome"] = "non_git_snapshot_sync_failed";
        return result;
    }
    result.fields["copied_file_count"] = std::to_string(copied_files);
    result.fields["copied_byte_count"] = std::to_string(copied_bytes);
    result.fields["skipped_file_count"] = std::to_string(skipped_files);

    bool stale_lock_removed = false;
    std::string lock_error;
    if (!ClearNonGitSnapshotStaleIndexLock(mirror_root, &stale_lock_removed, &lock_error)) {
        result.ok = false;
        result.exit_code = 52;
        result.fields["error"] = lock_error;
        result.fields["semantic_outcome"] = "non_git_snapshot_stale_lock_clear_failed";
        return result;
    }
    result.fields["stale_index_lock_removed"] = stale_lock_removed ? "true" : "false";

    if (!RunSnapshotMirrorGitStep(
            "add_all",
            mirror_root,
            "add -A",
            log_path,
            timeout_sec,
            &run_result,
            &command_line,
            &run_error) || run_result.exit_code != 0) {
        result.ok = false;
        result.exit_code = run_result.exit_code == 0 ? 52 : run_result.exit_code;
        result.fields["error"] = run_error;
        result.fields["command_line"] = command_line;
        result.fields["semantic_outcome"] = "non_git_snapshot_add_failed";
        return result;
    }

    if (!RunSnapshotMirrorGitStep(
            "baseline_exists",
            mirror_root,
            "rev-parse --verify HEAD",
            log_path,
            timeout_sec,
            &run_result,
            &command_line,
            &run_error)) {
        result.ok = false;
        result.exit_code = 52;
        result.fields["error"] = run_error;
        result.fields["command_line"] = command_line;
        result.fields["semantic_outcome"] = "non_git_snapshot_baseline_check_failed";
        return result;
    }
    const bool baseline_exists = run_result.exit_code == 0;
    result.fields["baseline_exists_before_call"] = baseline_exists ? "true" : "false";

    if (!baseline_exists) {
        if (!RunSnapshotMirrorGitStep(
                "create_baseline",
                mirror_root,
                "-c user.name=\"codex-lan-agent\" -c user.email=\"codex-lan-agent@example.invalid\" commit --allow-empty -m \"snapshot baseline\"",
                log_path,
                timeout_sec,
                &run_result,
                &command_line,
                &run_error) || run_result.exit_code != 0) {
            result.ok = false;
            result.exit_code = run_result.exit_code == 0 ? 52 : run_result.exit_code;
            result.fields["error"] = run_error;
            result.fields["command_line"] = command_line;
            result.fields["semantic_outcome"] = "non_git_snapshot_baseline_create_failed";
            return result;
        }
        result.fields["baseline_created"] = "true";
    }

    const std::vector<std::pair<std::string, std::string>> diff_steps = {
        {"status_short", "status --short"},
        {"diff_stat", "diff --cached --no-ext-diff --stat"},
        {"diff_full", "diff --cached --no-ext-diff --"}
    };
    int final_exit_code = 0;
    for (const auto & step : diff_steps) {
        if (!RunSnapshotMirrorGitStep(
                step.first,
                mirror_root,
                step.second,
                log_path,
                timeout_sec,
                &run_result,
                &command_line,
                &run_error)) {
            result.ok = false;
            result.exit_code = 52;
            result.fields["error"] = run_error;
            result.fields["command_line"] = command_line;
            result.fields["failed_step"] = step.first;
            result.fields["semantic_outcome"] = "non_git_snapshot_diff_failed";
            return result;
        }
        final_exit_code = run_result.exit_code;
        if (run_result.exit_code != 0) {
            result.fields["failed_step"] = step.first;
            break;
        }
    }

    if (final_exit_code == 0 && baseline_exists && IsSnapshotBaselineRefreshAction(snapshot_action)) {
        if (!RunSnapshotMirrorGitStep(
                "refresh_baseline",
                mirror_root,
                "-c user.name=\"codex-lan-agent\" -c user.email=\"codex-lan-agent@example.invalid\" commit --allow-empty -m \"snapshot baseline refresh\"",
                log_path,
                timeout_sec,
                &run_result,
                &command_line,
                &run_error)) {
            result.ok = false;
            result.exit_code = 52;
            result.fields["error"] = run_error;
            result.fields["command_line"] = command_line;
            result.fields["semantic_outcome"] = "non_git_snapshot_baseline_refresh_failed";
            return result;
        }
        if (run_result.exit_code != 0) {
            result.fields["baseline_refresh_exit_code"] = std::to_string(run_result.exit_code);
        } else {
            result.fields["baseline_refreshed"] = "true";
        }
    }

    result.ok = final_exit_code == 0;
    result.exit_code = final_exit_code;
    result.fields["command_line"] = command_line;
    result.fields["timed_out"] = run_result.timed_out ? "true" : "false";
    result.fields["stalled"] = run_result.stalled ? "true" : "false";

    std::string log_content;
    std::string read_error;
    if (ReadWholeFile(log_path, &log_content, &read_error)) {
        if (!baseline_exists) {
            result.fields["semantic_outcome"] = "non_git_snapshot_baseline_created";
        } else if (GetFieldOrDefault(result, "baseline_refreshed", "") == "true") {
            result.fields["semantic_outcome"] = "non_git_snapshot_baseline_refreshed";
        } else {
            result.fields["semantic_outcome"] =
                result.ok ? "non_git_snapshot_diff_ready" : "non_git_snapshot_diff_failed";
        }
        result.fields["content"] = log_content;
    } else {
        result.fields["semantic_outcome"] =
            result.ok ? "non_git_snapshot_diff_ready" : "non_git_snapshot_diff_failed";
        result.fields["log_read_error"] = read_error;
    }
    result.fields["expected_marker"] = "non_git_mirror_repo_snapshot_diff_log";
    return result;
}

CommandResult RunSnapshotDiffGitFallback(
    const AgentConfig & config,
    const std::string & repo_root,
    const std::string & working_directory,
    const std::string & helper_search_paths,
    const std::string & log_path,
    int timeout_sec,
    const std::string & non_git_strategy,
    const std::string & snapshot_action) {
    CommandResult result;
    result.fields["requested_repo_root"] = repo_root;
    result.fields["working_directory"] = working_directory;
    result.fields["helper_search_paths_json"] = helper_search_paths;
    result.fields["helper_missing_fallback"] = "true";
    result.fields["helper_path"] = "";
    result.fields["log_path"] = log_path;

    std::string log_error;
    AppendSnapshotDiffLogLine(log_path, "[snapshot_start]", &log_error);
    AppendSnapshotDiffLogLine(log_path, "repo_root=" + working_directory, &log_error);
    AppendSnapshotDiffLogLine(log_path, "execution_mode=direct_git_fallback", &log_error);
    AppendSnapshotDiffLogLine(log_path, "", &log_error);
    if (!log_error.empty()) {
        result.ok = false;
        result.exit_code = 52;
        result.fields["error"] = log_error;
        result.fields["semantic_outcome"] = "snapshot_diff_log_write_failed";
        return result;
    }

    const std::vector<std::pair<std::string, std::string>> steps = {
        {"rev_parse", "rev-parse --show-toplevel"},
        {"status_short", "status --short"},
        {"diff_stat", "diff --no-ext-diff --stat"},
        {"diff_full", "diff --no-ext-diff --"}
    };

    int final_exit_code = 0;
    codex_lan_agent::ProcessRunResult last_run;
    std::string last_command_line;
    for (const auto & step : steps) {
        AppendSnapshotDiffLogLine(log_path, "[git_step]", nullptr);
        AppendSnapshotDiffLogLine(log_path, "label=" + step.first, nullptr);
        const std::string command_line =
            SnapshotDiffGitProgram()
            + " -C " + QuoteSnapshotDiffArgument(working_directory)
            + " " + step.second;
        last_command_line = command_line;
        codex_lan_agent::ProcessRunResult run_result;
        std::string run_error;
        if (!codex_lan_agent::RunCommandWithLog(
                command_line,
                std::string(),
                log_path,
                timeout_sec,
                30,
                &run_result,
                &run_error)) {
            result.ok = false;
            result.exit_code = 52;
            result.fields["error"] = run_error;
            result.fields["semantic_outcome"] = "snapshot_diff_fallback_start_failed";
            result.fields["failed_step"] = step.first;
            result.fields["command_line"] = command_line;
            return result;
        }
        last_run = run_result;
        final_exit_code = run_result.exit_code;
        AppendSnapshotDiffLogLine(log_path, "step_exit_code=" + std::to_string(run_result.exit_code), nullptr);
        AppendSnapshotDiffLogLine(log_path, "", nullptr);
        if (run_result.exit_code != 0) {
            result.fields["failed_step"] = step.first;
            if (step.first == "rev_parse" &&
                ToLowerAscii(non_git_strategy) != "fail" &&
                ToLowerAscii(non_git_strategy) != "git_only") {
                return RunSnapshotDiffNonGitMirrorFallback(
                    config,
                    repo_root,
                    working_directory,
                    helper_search_paths,
                    log_path,
                    timeout_sec,
                    snapshot_action);
            }
            break;
        }
    }

    result.ok = final_exit_code == 0;
    result.exit_code = final_exit_code;
    result.fields["repo_root"] = working_directory;
    result.fields["working_directory"] = working_directory;
    result.fields["command_line"] = last_command_line;
    result.fields["timed_out"] = last_run.timed_out ? "true" : "false";
    result.fields["stalled"] = last_run.stalled ? "true" : "false";

    std::string log_content;
    std::string read_error;
    if (ReadWholeFile(log_path, &log_content, &read_error)) {
        result.fields["semantic_outcome"] =
            result.ok ? "snapshot_diff_ready" : "git_root_or_diff_failed";
        result.fields["content"] = log_content;
    } else {
        result.fields["semantic_outcome"] =
            result.ok ? "snapshot_diff_ready" : "git_root_or_diff_failed";
        result.fields["log_read_error"] = read_error;
    }
    result.fields["expected_marker"] = "git_rev_parse_or_snapshot_diff_log";
    return result;
}

CommandResult SnapshotDiffResult(
    const AgentConfig & config,
    const std::string & repo_root,
    int timeout_sec,
    const std::string & non_git_strategy,
    const std::string & snapshot_action) {
    CommandResult result;
    std::string working_directory = repo_root.empty()
        ? FindGitRootUnderWorkspace(config)
        : config.workspace_root;
    if (!repo_root.empty()) {
        std::filesystem::path normalized_repo;
        std::string path_error;
        if (!TryResolveAllowedPath(config, repo_root, &normalized_repo, &path_error)) {
            result.ok = false;
            result.exit_code = 53;
            result.fields["error"] = path_error;
            result.fields["semantic_outcome"] = "git_root_invalid";
            result.fields["requested_repo_root"] = repo_root;
            return result;
        }
        working_directory = normalized_repo.string();
    }
    const std::string artifact_base = BuildSnapshotDiffArtifactBase(config, working_directory);
    const std::string log_path = artifact_base + ".log";
    std::string helper_search_paths;
    const std::string helper_path = FindGitSnapshotHelperPath(config, &helper_search_paths);
    std::error_code git_marker_error;
    const bool has_local_git_marker = std::filesystem::exists(
        std::filesystem::path(working_directory) / ".git",
        git_marker_error);
    if (!has_local_git_marker &&
        ToLowerAscii(non_git_strategy) != "fail" &&
        ToLowerAscii(non_git_strategy) != "git_only") {
        return RunSnapshotDiffNonGitMirrorFallback(
            config,
            repo_root,
            working_directory,
            helper_search_paths,
            log_path,
            timeout_sec,
            snapshot_action);
    }
    if (helper_path.empty()) {
        return RunSnapshotDiffGitFallback(
            config,
            repo_root,
            working_directory,
            helper_search_paths,
            log_path,
            timeout_sec,
            non_git_strategy,
            snapshot_action);
    }
    const std::string command_line =
        QuoteSnapshotDiffArgument(helper_path)
        + " --repo-root " + QuoteSnapshotDiffArgument(working_directory);
    result.fields["requested_repo_root"] = repo_root;
    result.fields["working_directory"] = working_directory;
    result.fields["helper_path"] = helper_path;
    result.fields["helper_search_paths_json"] = helper_search_paths;
    result.fields["command_line"] = command_line;
    result.fields["log_path"] = log_path;
    codex_lan_agent::ProcessRunResult run_result;
    std::string error_message;
    if (!codex_lan_agent::RunCommandWithLog(
            command_line,
            std::string(),
            log_path,
            timeout_sec,
            30,
            &run_result,
            &error_message)) {
        result.ok = false;
        result.exit_code = 52;
        result.fields["error"] = error_message;
        result.fields["semantic_outcome"] = "snapshot_diff_start_failed";
        return result;
    }

    result.ok = run_result.exit_code == 0;
    result.exit_code = run_result.exit_code;
    result.fields["repo_root"] = working_directory;
    result.fields["working_directory"] = working_directory;
    result.fields["command_line"] = command_line;
    result.fields["timed_out"] = run_result.timed_out ? "true" : "false";
    result.fields["stalled"] = run_result.stalled ? "true" : "false";

    std::string log_content;
    std::string read_error;
    if (ReadWholeFile(log_path, &log_content, &read_error)) {
        result.fields["semantic_outcome"] =
            result.ok ? "snapshot_diff_ready" : "git_root_or_diff_failed";
        result.fields["content"] = log_content;
    } else {
        result.fields["semantic_outcome"] = result.ok ? "snapshot_diff_ready" : "git_root_or_diff_failed";
        result.fields["log_read_error"] = read_error;
    }
    result.fields["expected_marker"] = "git_rev_parse_or_snapshot_diff_log";
    return result;
}
