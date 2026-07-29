#ifndef CODEX_LAN_AGENT_CFG_BUILDER_H
#define CODEX_LAN_AGENT_CFG_BUILDER_H

#include "ClangIndexerAdapter.h"

#include <string>
#include <vector>

namespace codex_lan_agent
{

struct CfGBlockInfo {
    int block_id = 0;
    std::string block_type;
    std::string label;
    std::vector<std::string> statements;
    std::vector<int> successor_ids;
    std::vector<int> predecessor_ids;
    int source_line = 0;
    int source_col = 0;
    std::string source_file;
    bool is_entry = false;
    bool is_exit = false;
    bool is_reachable = true;
};

struct CfgFunctionInfo {
    std::string function_name;
    std::string qualified_name;
    std::string namespace_name;
    std::vector<CfGBlockInfo> blocks;
    int entry_block_id = 0;
    int exit_block_id = 0;
    int block_count = 0;
    int edge_count = 0;
    int branch_count = 0;
    float cyclomatic_complexity = 1.0f;
    bool has_cycle = false;
    std::string source_file;
    int source_line = 0;
};

struct CfgBuildResult {
    bool success = false;
    std::string error;
    std::string source_file;
    std::vector<CfgFunctionInfo> functions;
    int total_functions = 0;
    int total_blocks = 0;
    int total_edges = 0;
    double build_time_ms = 0.0;
};

CfgBuildResult RunCfgBuilder(
    const ClangIndexerOptions & options);

std::string SerializeCfgBuildResultToJson(
    const CfgBuildResult & result);

std::string SerializeCfgToDot(
    const CfgBuildResult & result,
    const std::string & function_name = "");

}

#endif