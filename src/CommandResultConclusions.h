#pragma once

bool TextContainsCaseInsensitive(const std::string & text, const std::string & needle) {
    return ToLowerAscii(text).find(ToLowerAscii(needle)) != std::string::npos;
}

std::string ExpectedMarkerForProfile(const std::string & profile_name) {
    if (profile_name == "configure_project") {
        return "configure_action_completed";
    }
    if (profile_name == "run_ctest_target") {
        return "ctest_action_completed";
    }
    if (profile_name == "prepare_build_dir") {
        return "prepare_action_completed";
    }
    if (profile_name == "build_target") {
        return "build_exit_code_0";
    }
    if (profile_name == "check_build_dir") {
        return "check_action_completed";
    }
    if (profile_name == "run_case") {
        return "case_previewed";
    }
    return profile_name.empty() ? "exit_code_0" : (profile_name + "_exit_code_0");
}

std::string AnalyzeSemanticOutcome(
    const std::string & profile_name,
    const CommandResult & result,
    const std::string & log_content) {
    if (profile_name == "configure_project" &&
        TextContainsCaseInsensitive(log_content, "unable to find a build program corresponding to")) {
        return "cmake_generator_missing";
    }
    if (profile_name == "configure_project" &&
        TextContainsCaseInsensitive(log_content, "CMAKE_CXX_COMPILER not set")) {
        return "cmake_compiler_missing";
    }
    if (profile_name == "configure_project" &&
        TextContainsCaseInsensitive(log_content, "\"semantic_outcome\":\"configure_ready\"")) {
        return "configure_ready";
    }
    if (profile_name == "prepare_build_dir" &&
        TextContainsCaseInsensitive(log_content, "\"result\":\"prepared\"")) {
        return "prepare_build_dir_ready";
    }
    if (profile_name == "check_build_dir" &&
        TextContainsCaseInsensitive(log_content, "\"result\":\"checked\"")) {
        return "check_build_dir_ready";
    }
    if (profile_name == "run_case" &&
        TextContainsCaseInsensitive(log_content, "\"result\":\"previewed\"")) {
        return "case_previewed";
    }
    const bool has_nonzero_exit_code =
        TextContainsCaseInsensitive(log_content, "exit_code=") &&
        !TextContainsCaseInsensitive(log_content, "exit_code=0");
    if (TextContainsCaseInsensitive(log_content, "fatal:") ||
        TextContainsCaseInsensitive(log_content, "fatal error") ||
        TextContainsCaseInsensitive(log_content, "error:") ||
        TextContainsCaseInsensitive(log_content, "exception") ||
        TextContainsCaseInsensitive(log_content, "exit_code=1") ||
        TextContainsCaseInsensitive(log_content, "exit code 1") ||
        has_nonzero_exit_code ||
        TextContainsCaseInsensitive(log_content, "nonzero exit") ||
        TextContainsCaseInsensitive(log_content, "non-zero exit")) {
        return "failure";
    }
    if (TextContainsCaseInsensitive(log_content, "CMake Error")) {
        return "cmake_error";
    }
    if (TextContainsCaseInsensitive(log_content, "No tests were found")) {
        return "ctest_no_tests_found";
    }
    if (TextContainsCaseInsensitive(log_content, "The following tests FAILED")) {
        return "ctest_tests_failed";
    }
    if (profile_name == "run_ctest_target" &&
        TextContainsCaseInsensitive(log_content, "100% tests passed")) {
        return "ctest_tests_passed";
    }
    if (profile_name == "configure_project" &&
        TextContainsCaseInsensitive(log_content, "Configuring incomplete, errors occurred")) {
        return "cmake_configure_incomplete";
    }
    return result.ok && result.exit_code == 0 ? "succeeded" : "failed";
}

bool VerifyExpectedMarker(
    const std::string & profile_name,
    const CommandResult & result,
    const std::string & semantic_outcome,
    const std::string & log_content) {
    (void) log_content;
    if (!result.ok || result.exit_code != 0) {
        return false;
    }
    if (profile_name == "configure_project") {
        return semantic_outcome == "configure_ready";
    }
    if (profile_name == "run_ctest_target") {
        return semantic_outcome == "ctest_tests_passed";
    }
    if (profile_name == "prepare_build_dir") {
        return semantic_outcome == "prepare_build_dir_ready";
    }
    if (profile_name == "check_build_dir") {
        return semantic_outcome == "check_build_dir_ready";
    }
    if (profile_name == "run_case") {
        return semantic_outcome == "case_previewed";
    }
    return semantic_outcome == "succeeded";
}

bool IsInvalidSmokeSemanticOutcome(const std::string & semantic_outcome) {
    return semantic_outcome == "ctest_no_tests_found";
}

std::string InvalidSmokeNextAction(const std::string & semantic_outcome) {
    if (semantic_outcome == "ctest_no_tests_found") {
        return "ctest found no tests; first run configure_project, select the correct build_dir, or rerun lan_agent_run_ctest_target with a specific test_regex";
    }
    return "inspect smoke setup and rerun verification with a concrete target";
}

void ApplyVerificationFields(
    CommandResult * result,
    const std::string & semantic_outcome) {
    if (result == nullptr) {
        return;
    }
    if (IsInvalidSmokeSemanticOutcome(semantic_outcome)) {
        result->fields["verification"] = "invalid";
        result->fields["verification_status"] = "invalid";
        result->fields["smoke_status"] = "invalid";
        result->fields["expected_marker_verified"] = "false";
        result->fields["invalid_verification_reason"] = semantic_outcome;
        result->fields["summary"] = "ctest completed but no tests were found; this is not an acceptable smoke pass";
        result->fields["next_action"] = InvalidSmokeNextAction(semantic_outcome);
        return;
    }
    if (GetFieldOrDefault(*result, "verification", "").empty()) {
        result->fields["verification"] =
            GetFieldOrDefault(*result, "expected_marker_verified", "") == "true" ? "pass" : "failed";
    }
    if (GetFieldOrDefault(*result, "verification_status", "").empty()) {
        result->fields["verification_status"] = result->fields["verification"];
    }
    if (GetFieldOrDefault(*result, "smoke_status", "").empty()) {
        result->fields["smoke_status"] = result->fields["verification"];
    }
}
