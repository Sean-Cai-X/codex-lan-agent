#pragma once

#include <string>

namespace codex_lan_agent {

struct ProcessRunResult {
    int exit_code = -1;
    bool timed_out = false;
    bool stalled = false;
    unsigned long process_id = 0;
    std::string log_path;
    std::string started_at;
    std::string finished_at;
    std::string last_output_at;
};

bool RunCommandWithLog(
    const std::string & command_line,
    const std::string & working_directory,
    const std::string & log_path,
    int timeout_sec,
    ProcessRunResult * result,
    std::string * error_message);

}  // namespace codex_lan_agent
