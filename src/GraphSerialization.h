#ifndef CODEX_LAN_AGENT_GRAPH_SERIALIZATION_H
#define CODEX_LAN_AGENT_GRAPH_SERIALIZATION_H

#include <string>
#include <vector>

namespace codex_lan_agent
{

struct CallGraphEdgeInfo
{
    std::string caller;
    std::string callee;
    std::string first_source_file;
    int first_source_line = 0;
    int first_source_col = 0;
    int count = 0;
};

struct DfgEdgeInfo
{
    std::string source;
    std::string target;
    std::string kind;
    int count = 0;
    int first_source_line = 0;
};

std::string SerializeCallGraphJson(
    const std::vector<std::string> & nodes,
    const std::vector<CallGraphEdgeInfo> & edges);

std::string SerializeCallGraphDot(
    const std::vector<std::string> & nodes,
    const std::vector<CallGraphEdgeInfo> & edges);

std::string SerializeDfgJson(
    const std::vector<std::string> & nodes,
    const std::vector<DfgEdgeInfo> & edges);

std::string SerializeDfgDot(
    const std::vector<std::string> & nodes,
    const std::vector<DfgEdgeInfo> & edges);

std::string SerializeProgramSliceJson(
    const std::string & symbol,
    const std::string & direction,
    const std::vector<std::string> & nodes,
    const std::vector<DfgEdgeInfo> & edges,
    const std::vector<int> & lines);

std::string SerializeProgramSliceDot(
    const std::vector<std::string> & nodes,
    const std::vector<DfgEdgeInfo> & edges);

std::string SerializeSourceLinesJson(
    const std::vector<int> & line_numbers,
    const std::vector<std::string> & source_lines);

} // namespace codex_lan_agent

#endif
