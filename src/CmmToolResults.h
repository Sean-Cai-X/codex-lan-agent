#pragma once

#include "AgentConfig.h"
#include "JsonRequestView.h"
#include "types.h"

namespace codex_lan_agent {

CommandResult BuildCmmIndexRepositoryResult(
    const AgentConfig & config, const JsonRequestView & params);
CommandResult BuildCmmSearchCodeResult(
    const AgentConfig & config, const JsonRequestView & params);
CommandResult BuildCmmSearchGraphResult(
    const AgentConfig & config, const JsonRequestView & params);
CommandResult BuildCmmQueryGraphResult(
    const AgentConfig & config, const JsonRequestView & params);
CommandResult BuildCmmTracePathResult(
    const AgentConfig & config, const JsonRequestView & params);
CommandResult BuildCmmGetCodeSnippetResult(
    const AgentConfig & config, const JsonRequestView & params);
CommandResult BuildCmmGetGraphSchemaResult(
    const AgentConfig & config, const JsonRequestView & params);
CommandResult BuildCmmGetArchitectureResult(
    const AgentConfig & config, const JsonRequestView & params);
CommandResult BuildCmmListProjectsResult(
    const AgentConfig & config, const JsonRequestView & params);
CommandResult BuildCmmIndexStatusResult(
    const AgentConfig & config, const JsonRequestView & params);
CommandResult BuildCmmDetectChangesResult(
    const AgentConfig & config, const JsonRequestView & params);
CommandResult BuildCmmDeleteProjectResult(
    const AgentConfig & config, const JsonRequestView & params);
CommandResult BuildCmmEnsureIndexedResult(
    const AgentConfig & config, const JsonRequestView & params);

}  // namespace codex_lan_agent
