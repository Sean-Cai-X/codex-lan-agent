#ifndef CODEX_LAN_AGENT_THREAD_SIGNAL_OPERATIONS_H
#define CODEX_LAN_AGENT_THREAD_SIGNAL_OPERATIONS_H

#include "AgentConfig.h"
#include "JsonRequestView.h"
#include "types.h"

// Thread-signal tool stubs. The upstream repo does not ship the concrete
// implementations yet. These return a clean error so the MCP server compiles
// and runs; actual signal handling can be added later.

inline CommandResult BuildThreadSignalSetResult(
    const AgentConfig & /*config*/,
    const JsonRequestView & /*params*/)
{
    CommandResult r;
    r.ok = false;
    r.exit_code = 1;
    r.fields["error"] = "lan_agent_thread_signal_set: not yet implemented on this platform";
    return r;
}

inline CommandResult BuildThreadSignalGetResult(
    const AgentConfig & /*config*/,
    const JsonRequestView & /*params*/)
{
    CommandResult r;
    r.ok = false;
    r.exit_code = 1;
    r.fields["error"] = "lan_agent_thread_signal_get: not yet implemented on this platform";
    return r;
}

inline CommandResult BuildThreadSignalListResult(
    const AgentConfig & /*config*/,
    const JsonRequestView & /*params*/)
{
    CommandResult r;
    r.ok = false;
    r.exit_code = 1;
    r.fields["error"] = "lan_agent_thread_signal_list: not yet implemented on this platform";
    return r;
}

#endif  // CODEX_LAN_AGENT_THREAD_SIGNAL_OPERATIONS_H
