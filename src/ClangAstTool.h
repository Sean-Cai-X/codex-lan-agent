#ifndef CODEX_LAN_AGENT_CLANG_AST_TOOL_H
#define CODEX_LAN_AGENT_CLANG_AST_TOOL_H

#include "CmmToolResults.h"
#include "JsonRequestView.h"

CommandResult BuildRunClangAstParserResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params);

CommandResult BuildRunCfgResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params);

#endif
