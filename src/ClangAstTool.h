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

CommandResult BuildQueryCfgArtifactResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params);

CommandResult BuildRunCallGraphResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params);

CommandResult BuildRunDfgResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params);

CommandResult BuildQueryCallGraphArtifactResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params);

CommandResult BuildQueryDfgArtifactResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params);

CommandResult BuildRunProgramSliceResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params);

CommandResult BuildQueryProgramSliceArtifactResult(
    const ::codex_lan_agent::AgentConfig & config,
    const ::JsonRequestView & params);

#endif
