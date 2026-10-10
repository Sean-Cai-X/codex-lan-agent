#pragma once

#include "AgentConfig.h"
#include "JsonRequestView.h"
#include "types.h"

#include <string>
#include <vector>

namespace codex_lan_agent {

// Resolve the codebase-memory-mcp binary path using AgentConfig and fallbacks.
std::string ResolveCmmBinaryPath(const AgentConfig & config);

// Run a single CMM tool via its CLI one-shot mode and return a CommandResult.
// args_json is the raw JSON object passed to the tool as arguments.
CommandResult RunCmmToolCli(
    const AgentConfig & config,
    const std::string & tool_name,
    const std::string & args_json,
    int timeout_ms);

// Build a CMM tool arguments JSON object.
// If params contains a non-empty "args_json" raw JSON value, it is used verbatim.
// Otherwise, simple scalar keys listed in simple_keys are extracted from params.
std::string BuildCmmArgsJson(
    const JsonRequestView & params,
    const std::vector<std::string> & simple_keys);

}  // namespace codex_lan_agent
