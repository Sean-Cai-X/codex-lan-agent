#pragma once

#include <string>

namespace codex_lan_agent {

struct ProcessRunResult {
    int exit_code = -1;
    bool timed_out = false;
    bool stalled = false;
    bool process_output_observed = false;
    unsigned long process_id = 0;
    unsigned long heartbeat_count = 0;
    long long runtime_sec = 0;
    long long quiet_sec_at_finish = 0;
    std::string log_path;
    std::string started_at;
    std::string finished_at;
    std::string last_output_at;
    std::string completion_reason;
};

bool RunCommandWithLog(
    const std::string & command_line,
    const std::string & working_directory,
    const std::string & log_path,
    int timeout_sec,
    int stall_timeout_sec,
    ProcessRunResult * result,
    std::string * error_message);

}  // namespace codex_lan_agent
