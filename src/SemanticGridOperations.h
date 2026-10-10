#pragma once

#include "AgentConfig.h"
#include "JsonRequestView.h"
#include "types.h"

namespace codex_lan_agent {

CommandResult BuildSemanticGridIngestTextResult(
    const AgentConfig & config,
    const JsonRequestView & params);

CommandResult BuildSemanticGridBuildResult(
    const AgentConfig & config,
    const JsonRequestView & params);

CommandResult BuildSemanticGridQueryResult(
    const AgentConfig & config,
    const JsonRequestView & params);

CommandResult BuildSemanticGridTraceSourceResult(
    const AgentConfig & config,
    const JsonRequestView & params);

CommandResult BuildSemanticGridContextBundleResult(
    const AgentConfig & config,
    const JsonRequestView & params);

CommandResult BuildSemanticGridIncrementalUpdateResult(
    const AgentConfig & config,
    const JsonRequestView & params);

}  // namespace codex_lan_agent
